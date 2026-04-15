#include <SPI.h>
#include <MFRC522.h>
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <string.h>
#include <stdio.h>

// ============================================================
//                       CONFIG / MODES
// ============================================================
// Enable exactly one mode for benchmarking, or both if desired.
#define MODE_RFID true
#define MODE_IOT false

// Metadata that is included in every measurement payload.
static const char* PLATFORM = "arduino_r4";
static const char* TEST_ID = "RFID_SIM_1";

#define ERR_NONE      "none"
#define ERR_UID       "uid"
#define ERR_HTTP_GET  "http_get"
#define ERR_HTTP_READ "http_read"
#define ERR_LED       "led"
#define ERR_STATUS    "status"

// WiFi + backend server configuration (HTTP, not HTTPS).
static char SSID[] = "TP-Link_3838";
static char PASS[] = "24701570";
static char SERVER_IP[] = "192.168.0.163";
static int  SERVER_PORT = 8000;

// Pins
#define SS_PIN 10
#define RST_PIN 9
#define LED_RED 3
#define LED_BLUE 4
#define BUTTON_PIN 2

// IoT HTTP server
static const uint16_t LISTEN_PORT = 80;
#define MAX_CLIENTS 10

// Tuning knobs
static const unsigned long WIFI_RETRY_MS = 5000;
static const unsigned long HTTP_TIMEOUT_MS = 2000;
static const unsigned long RFID_LOCK_DEBOUNCE_MS = 50;

// IoT server timeouts
static const unsigned long CLIENT_IDLE_TIMEOUT_MS = 3000;
static const unsigned long HEADER_TIMEOUT_MS = 1000;

// ============================================================
//                           OBJECTS
// ============================================================

MFRC522 rfid(SS_PIN, RST_PIN);

WiFiClient wifi;
HttpClient httpClient(wifi, SERVER_IP, SERVER_PORT);

WiFiServer server(LISTEN_PORT);
WiFiClient clients[MAX_CLIENTS];
unsigned long clientLastActivity[MAX_CLIENTS] = {0};

// ============================================================
//                           STATE
// ============================================================

// RFID lock prevents repeated scans until the user presses the button.
static bool RFIDScanLocked = false;

// Non-blocking button debounce state
static bool lastButtonReading = HIGH;
static unsigned long lastButtonChangeMs = 0;

// ============================================================
//                           STATS
// ============================================================

static unsigned long wifiReconnects = 0;
static unsigned long startTimeMs = 0;

// ============================================================
//                       RAM MEASUREMENT
// ============================================================
// NOTE: freeMemory() via sbrk() is a common trick, but it is platform-dependent.
// On some cores it may be approximate. Keep it for instrumentation only.

static int minFreeRam = 999999;

extern "C" char* sbrk(int incr);

int freeMemory() {
  char stack_dummy = 0;
  return &stack_dummy - sbrk(0);
}

void updateMinRam() {
  int current = freeMemory();
  if (current < minFreeRam) minFreeRam = current;
}

// ============================================================
//                           WIFI
// ============================================================
// Keep WiFi alive without blocking.
// - If disconnected, retry WiFi.begin() every WIFI_RETRY_MS.
// - Count reconnect events when transitioning from connected -> disconnected.

void ensureWiFi() {
  static uint8_t lastStatus = WL_IDLE_STATUS;
  static unsigned long lastAttemptMs = 0;

  uint8_t current = WiFi.status();

  if (current != WL_CONNECTED && (millis() - lastAttemptMs) > WIFI_RETRY_MS) {
    WiFi.begin(SSID, PASS);
    lastAttemptMs = millis();
  }

  if (lastStatus == WL_CONNECTED && current != WL_CONNECTED) {
    wifiReconnects++;
  }

  lastStatus = current;
}

// ============================================================
//                           RFID
// ============================================================
// Convert MFRC522 UID bytes to uppercase hex string.
// Avoids Arduino String allocations by writing into a char buffer.
//
// uidOut must have space for: 2*uid.size + 1 (null terminator).
// MFRC522 typical UID sizes are 4/7/10 bytes -> max 21 chars including '\0'.
void uidToHex(char* uidOut, size_t uidOutSize) {
  // Defensive: ensure buffer is not tiny.
  if (uidOutSize == 0) return;

  size_t pos = 0;
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (pos + 2 >= uidOutSize) break;

    uint8_t b = rfid.uid.uidByte[i];
    const char hex[] = "0123456789ABCDEF";
    uidOut[pos++] = hex[(b >> 4) & 0x0F];
    uidOut[pos++] = hex[b & 0x0F];
  }
  uidOut[pos] = '\0';
}

// Non-blocking debounce check for the "unlock" button.
// Returns true once per stable press (LOW) after debounce.
bool buttonPressedDebounced() {
  bool reading = (digitalRead(BUTTON_PIN) == LOW); // INPUT_PULLUP => pressed == LOW
  unsigned long now = millis();

  if (reading != lastButtonReading) {
    lastButtonChangeMs = now;
    lastButtonReading = reading;
  }

  if ((now - lastButtonChangeMs) >= RFID_LOCK_DEBOUNCE_MS) {
    // Stable state for debounce window
    if (reading) {
      return true;
    } else {
      return false;
    }
  }

  return false;
}

void appendError(char* buffer, size_t size, const char* err) {
  if (buffer[0] != '\0') {
    strncat(buffer, ",", size - strlen(buffer) - 1);
  }
  strncat(buffer, err, size - strlen(buffer) - 1);
}

// ============================================================
//                SEND RFID MEASUREMENT (HTTP POST)
// ============================================================
// Build JSON into a fixed buffer using snprintf to avoid heap fragmentation.
// Then POST to /arduino/rfid-save-measurements/.
//
// Note: Using float formatting on embedded can be heavy depending on core.
// If you want smaller code size, send integers (micros/millis) instead of float.

void sendRFIDMeasurement(
  unsigned long rfid_us,
  unsigned long http_ms,
  unsigned long led_us,
  unsigned long total_ms,
  const char* status,
  int rssi,
  bool error,
  const char* error_type
) {
  char payload[512];

  // Compose JSON with integers to keep it lightweight and deterministic.
  // (You can rename keys or units as you like, but this preserves your meaning.)
  unsigned long uptime_s = (millis() - startTimeMs) / 1000UL;

  int written = snprintf(
    payload, sizeof(payload),
    "{"
      "\"platform\":\"%s\","
      "\"test_id\":\"%s\","
      "\"rfid_reaction_time\":%lu,"     // us
      "\"http_latency\":%lu,"           // ms
      "\"led_response_time\":%lu,"      // us
      "\"total_time\":%lu,"             // ms
      "\"up_time\":%lu,"
      "\"wifi_rssi\":%d,"
      "\"wifi_reconnects\":%lu,"
      "\"free_ram\":%d,"
      "\"min_free_ram\":%d,"
      "\"status\":\"%s\","
      "\"error\":\"%s\","
      "\"error_type\":\"%s\""
    "}",
    PLATFORM, TEST_ID,
    rfid_us, http_ms, led_us, total_ms,
    uptime_s,
    rssi,
    wifiReconnects,
    freeMemory(),
    minFreeRam,
    status,
    error ? "true" : "false",
    error_type
  );

  if (written <= 0) return;                 // encoding failed
  if (written >= (int)sizeof(payload)) {
    // payload was truncated; still try to send what we have, but you may want to detect this.
  }

  httpClient.setTimeout(HTTP_TIMEOUT_MS);

  httpClient.beginRequest();
  httpClient.post("/arduino/rfid-save-measurements/");
  httpClient.sendHeader("Content-Type", "application/json");
  httpClient.sendHeader("Content-Length", (int)strlen(payload));
  httpClient.beginBody();
  httpClient.print(payload);
  httpClient.endRequest();

  // Trigger the request and then close (we don't need the body here).
  httpClient.responseStatusCode();
  httpClient.stop();

  // Reset min RAM baseline after each RFID event (as in your original code).
  minFreeRam = freeMemory();
}

// ============================================================
//                       RFID MAIN HANDLER
// ============================================================
// Flow:
// - If scan is locked: wait for button press to unlock and turn LEDs off.
// - If new card present: read UID, GET /arduino/rfid_scan/?uid=..., parse result
// - Set LED based on accepted/denied, send measurement POST, lock scanning.

void handleRFID() {
  bool error = false;
  char error_type[64] = "";   // supports multiple errors

  if (RFIDScanLocked) {
    if (buttonPressedDebounced()) {
      RFIDScanLocked = false;
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_BLUE, LOW);
    }
    return;
  }

  if (!rfid.PICC_IsNewCardPresent()) return;

  unsigned long totalStartMs = millis();
  unsigned long rfidStartUs  = micros();

  if (!rfid.PICC_ReadCardSerial()) return;

  char uid[32];
  uidToHex(uid, sizeof(uid));

  // If your function has no return, validate result:
  if (uid[0] == '\0') {
    error = true;
    appendError(error_type, sizeof(error_type), ERR_UID);
  }

  unsigned long rfidTimeUs = micros() - rfidStartUs;

  // ---------------- HTTP GET: check UID ----------------
  unsigned long httpStartMs = millis();
  
  // Avoid String concatenation; build URL in a fixed buffer.
  char url[128];
  snprintf(url, sizeof(url), "/arduino/rfid_scan/?uid=%s", uid);

  httpClient.setTimeout(HTTP_TIMEOUT_MS);
  httpClient.get(url);

  int statusCode = httpClient.responseStatusCode();

  if (statusCode <= 0) {
    error = true;
    appendError(error_type, sizeof(error_type), ERR_HTTP_GET);
  }

  // Instead of reading the entire response String (heap), read a small chunk.
  // We only care if it contains "accepted" or "denied".
  char respSnippet[128];
  int n = httpClient.readBytes(respSnippet, sizeof(respSnippet) - 1);
  respSnippet[(n > 0) ? n : 0] = '\0';

  if (n <= 0) {
    error = true;
    appendError(error_type, sizeof(error_type), ERR_HTTP_READ);
  }

  httpClient.stop();

  unsigned long httpTimeMs = millis() - httpStartMs;

  // ---------------- STATUS DECISION ----------------
  // Default to "error". Only mark accepted/denied when detected AND statusCode looks OK.
  const char* status = "error";

  if (statusCode >= 200 && statusCode < 300) {
    if (strstr(respSnippet, "accepted")) status = "accepted";
    else if (strstr(respSnippet, "denied")) status = "denied";
  }

  if (strstr(status, "error")) {
    error = true;
    appendError(error_type, sizeof(error_type), ERR_STATUS);
  }

  // ---------------- LED RESPONSE TIME ----------------
  unsigned long ledStartUs = micros();

  if (strcmp(status, "accepted") == 0) {
    digitalWrite(LED_BLUE, HIGH);

    if (digitalRead(LED_BLUE) != HIGH) {
      error = true;
      appendError(error_type, sizeof(error_type), ERR_LED);
    }

  } else {
    digitalWrite(LED_RED, HIGH);

    if (digitalRead(LED_RED) != HIGH) {
      error = true;
      appendError(error_type, sizeof(error_type), ERR_LED);
    }
  }

  unsigned long ledTimeUs = micros() - ledStartUs;

  unsigned long totalTimeMs = millis() - totalStartMs;
  int rssi = WiFi.RSSI();

  if (!error) {
    strncpy(error_type, ERR_NONE, sizeof(error_type));
  }

  // ---------------- SEND MEASUREMENT POST ----------------
  sendRFIDMeasurement(
    rfidTimeUs,
    httpTimeMs,
    ledTimeUs,
    totalTimeMs,
    status,
    rssi,
    error,
    error_type
  );

  // Tell the RFID reader we're done with this card.
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  RFIDScanLocked = true;
}

// ============================================================
//                          IOT MODE
// ============================================================
// This section implements a tiny HTTP server that:
// - accepts multiple clients into a fixed-size array,
// - reads request headers with timeout,
// - extracts "complexity=" parameter,
// - runs simulated computation, then responds with JSON.

unsigned long simulateComputation(int complexity) {
  volatile long dummy = 0;
  unsigned long startUs = micros();

  // Keep it deterministic; complexity 1..10
  for (int i = 0; i < complexity * 1000; i++) {
    dummy += i % 7;
  }

  return micros() - startUs;
}

void reapClients() {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i] && !clients[i].connected()) {
      clients[i].stop();
      clients[i] = WiFiClient();
      clientLastActivity[i] = 0;
    }
  }
}

void acceptClients() {
  reapClients();

  // Fairness: don't accept unlimited clients in one loop iteration.
  const int maxAcceptPerLoop = 2;
  int accepted = 0;

  while (accepted < maxAcceptPerLoop) {
    WiFiClient newClient = server.available();
    if (!newClient) break;

    bool stored = false;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i]) {
        clients[i] = newClient;
        clientLastActivity[i] = millis();
        stored = true;
        accepted++;
        break;
      }
    }

    // If no slot available: actively reject to free sockets.
    if (!stored) {
      newClient.stop();
      break;
    }
  }
}

// Read until "\r\n\r\n" (end of headers) or timeout or buffer full.
int readHttpHeaders(WiFiClient &client, char *buf, int bufSize, unsigned long timeoutMs) {
  int n = 0;
  unsigned long startMs = millis();

  while ((millis() - startMs) < timeoutMs && n < bufSize - 1) {
    while (client.available() && n < bufSize - 1) {
      buf[n++] = (char)client.read();
      buf[n] = '\0';
      if (n >= 4 && strstr(buf, "\r\n\r\n")) return n;
    }
  }
  return n;
}

// Extract complexity= from request line only.
// Example: "GET /?complexity=3 HTTP/1.1"
int extractComplexityFromRequestLine(const char *req) {
  const char *lineEnd = strstr(req, "\r\n");
  if (!lineEnd) return 1;

  const char *p = strstr(req, "complexity=");
  if (!p || p > lineEnd) return 1;

  int v = atoi(p + 11);
  if (v < 1) v = 1;
  if (v > 10) v = 10;
  return v;
}

void respondJson(WiFiClient &client, unsigned long compUs, int freeRam, int rssi) {
  char payload[256];

  int written = snprintf(
    payload, sizeof(payload),
    "{"
      "\"platform\":\"%s\","
      "\"test_id\":\"%s\","
      "\"computation_time\":%lu,"
      "\"free_ram\":%d,"
      "\"min_free_ram\":%d,"
      "\"wifi_rssi\":%d,"
      "\"error\":false,"
      "\"error_type\":null"
    "}",
    PLATFORM, TEST_ID, compUs,
    freeRam, minFreeRam, rssi
  );

  if (written < 0) written = 0;
  payload[sizeof(payload) - 1] = '\0';

  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Connection: close\r\n");
  client.print("Content-Length: ");
  client.print(strlen(payload));
  client.print("\r\n\r\n");
  client.print(payload);
}

void respond400(WiFiClient &client) {
  client.print("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
}

void handleClients() {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    WiFiClient &client = clients[i];
    if (!client) continue;

    // Drop idle clients that never finish sending a request.
    if (clientLastActivity[i] && (millis() - clientLastActivity[i] > CLIENT_IDLE_TIMEOUT_MS)) {
      client.stop();
      client = WiFiClient();
      clientLastActivity[i] = 0;
      continue;
    }

    if (!client.connected()) {
      client.stop();
      client = WiFiClient();
      clientLastActivity[i] = 0;
      continue;
    }

    if (!client.available()) continue;

    clientLastActivity[i] = millis();

    char headerBuf[256];
    int n = readHttpHeaders(client, headerBuf, sizeof(headerBuf), HEADER_TIMEOUT_MS);

    // Require full header delimiter to reduce partial/slowloris impact.
    if (n < 4 || !strstr(headerBuf, "\r\n\r\n")) {
      respond400(client);
      client.stop();
      client = WiFiClient();
      clientLastActivity[i] = 0;
      continue;
    }

    int complexity = extractComplexityFromRequestLine(headerBuf);
    unsigned long compTimeUs = simulateComputation(complexity);

    respondJson(client, compTimeUs, freeMemory(), WiFi.RSSI());

    // Close and free slot.
    client.stop();
    client = WiFiClient();
    clientLastActivity[i] = 0;
  }
}

// ============================================================
//                           SETUP
// ============================================================

void setup() {
  Serial.begin(115200);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  SPI.begin();
  rfid.PCD_Init();

  WiFi.begin(SSID, PASS);

  // Blocking connect at boot is OK for a benchmark sketch.
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  server.begin();

  startTimeMs = millis();
  minFreeRam = freeMemory();
}

// ============================================================
//                            LOOP
// ============================================================

void loop() {
  ensureWiFi();
  updateMinRam();

  if (MODE_RFID) handleRFID();

  if (MODE_IOT) {
    acceptClients();
    handleClients();
  }
}