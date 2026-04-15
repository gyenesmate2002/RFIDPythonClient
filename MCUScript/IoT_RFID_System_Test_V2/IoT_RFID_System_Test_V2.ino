#include <SPI.h>
#include <MFRC522.h>
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>

// ================= CONFIG =================

#define MODE_RFID true
#define MODE_IOT false

const char* PLATFORM = "arduino_r4";
const char* TEST_ID = "test_1";

char SSID[] = "TP-Link_3838";
char PASS[] = "24701570";
char SERVER_IP[] = "192.168.0.163";
int SERVER_PORT = 8000;

#define SS_PIN 10
#define RST_PIN 9
#define LED_RED 3
#define LED_BLUE 4
#define BUTTON_PIN 2

// ================= OBJECTS =================

MFRC522 rfid(SS_PIN, RST_PIN);

WiFiClient wifi;
HttpClient httpClient(wifi, SERVER_IP, SERVER_PORT);

WiFiServer server(80);
#define MAX_CLIENTS 10
WiFiClient clients[MAX_CLIENTS];

// ================= STATE =================

bool RFIDScanLocked = false;

// ================= STATS =================

unsigned long wifiReconnects = 0;
unsigned long startTime;

// ================= RAM =================

int minFreeRam = 999999;

extern "C" char* sbrk(int incr);

int freeMemory() {
  char stack_dummy = 0;
  return &stack_dummy - sbrk(0);
}

void updateMinRam() {
  int current = freeMemory();
  if (current < minFreeRam) minFreeRam = current;
}

// ================= WIFI =================

void ensureWiFi() {

  static uint8_t lastStatus = WL_CONNECTED;
  static unsigned long lastAttempt = 0;

  uint8_t current = WiFi.status();

  if (current != WL_CONNECTED && millis() - lastAttempt > 5000) {
    WiFi.begin(SSID, PASS);
    lastAttempt = millis();
  }

  if (lastStatus == WL_CONNECTED && current != WL_CONNECTED) {
    wifiReconnects++;
  }

  lastStatus = current;
}

// ================= RFID =================

String readUID() {
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();
  return uid;
}

// ================= SEND RFID =================

void sendRFIDMeasurement(
  float rfid_time,
  float http_time,
  float led_time,
  float total_time,
  const String& status,
  int rssi
) {

  String payload = "{";
  payload += "\"platform\":\"" + String(PLATFORM) + "\",";
  payload += "\"test_id\":\"" + String(TEST_ID) + "\",";
  payload += "\"rfid_reaction_time\":" + String(rfid_time) + ",";
  payload += "\"http_latency\":" + String(http_time) + ",";
  payload += "\"led_response_time\":" + String(led_time) + ",";
  payload += "\"total_time\":" + String(total_time) + ",";
  payload += "\"up_time\":" + String((millis() - startTime) / 1000) + ",";
  payload += "\"wifi_rssi\":" + String(rssi) + ",";
  payload += "\"wifi_reconnects\":" + String(wifiReconnects) + ",";
  payload += "\"free_ram\":" + String(freeMemory()) + ",";
  payload += "\"min_free_ram\":" + String(minFreeRam) + ",";
  payload += "\"status\":\"" + status + "\",";
  payload += "\"error\":false,";
  payload += "\"error_type\":null";
  payload += "}";

  httpClient.setTimeout(2000);

  httpClient.beginRequest();
  httpClient.post("/arduino/rfid-save-measurements/");
  httpClient.sendHeader("Content-Type", "application/json");
  httpClient.sendHeader("Content-Length", payload.length());
  httpClient.beginBody();
  httpClient.print(payload);
  httpClient.endRequest();

  httpClient.responseStatusCode();
  httpClient.stop();

  // reset min RAM per RFID event (important)
  minFreeRam = freeMemory();
}

// ================= RFID HANDLER =================

void handleRFID() {

  if (RFIDScanLocked) {
    if (digitalRead(BUTTON_PIN) == LOW) { // fixed (pullup logic)
      delay(50); // debounce
      RFIDScanLocked = false;
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_BLUE, LOW);
    }
    return;
  }

  if (!rfid.PICC_IsNewCardPresent()) return;

  unsigned long totalStart = millis();
  unsigned long rfidStart = micros();

  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = readUID();

  unsigned long rfidTime = micros() - rfidStart;

  // ---------- HTTP ----------
  unsigned long httpStart = millis();

  String url = "/arduino/rfid_scan/?uid=" + uid;

  httpClient.setTimeout(2000);
  httpClient.get(url);

  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();

  httpClient.stop();

  unsigned long httpTime = millis() - httpStart;

  // ---------- STATUS ----------
  String status = "error";

  if (response.indexOf("accepted") != -1) status = "accepted";
  else if (response.indexOf("denied") != -1) status = "denied";

  // ---------- LED ----------
  unsigned long ledStart = micros();

  if (status == "accepted") digitalWrite(LED_BLUE, HIGH);
  else digitalWrite(LED_RED, HIGH);

  unsigned long ledTime = micros() - ledStart;

  unsigned long totalTime = millis() - totalStart;

  int rssi = WiFi.RSSI();

  // ---------- SEND ----------
  sendRFIDMeasurement(
    rfidTime,
    httpTime,
    ledTime,
    totalTime,
    status,
    rssi
  );

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  RFIDScanLocked = true;
}

// ================= IOT =================

unsigned long simulateComputation(int complexity) {

  volatile long dummy = 0;

  unsigned long start = micros();

  for (int i = 0; i < complexity * 1000; i++) {
    dummy += i % 7;
  }

  return micros() - start;
}

// Add alongside your globals:
unsigned long clientLastActivity[MAX_CLIENTS] = {0};

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

  const int maxAcceptPerLoop = 2; // fairness
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

    if (!stored) {
      // IMPORTANT: actively reject
      newClient.stop();
      break;
    }
  }
}

// Read until end of HTTP headers or timeout; returns bytes in buf, null-terminated.
int readHttpHeaders(WiFiClient &client, char *buf, int bufSize, unsigned long timeoutMs) {
  int n = 0;
  unsigned long start = millis();

  while (millis() - start < timeoutMs && n < bufSize - 1) {
    while (client.available() && n < bufSize - 1) {
      buf[n++] = (char)client.read();
      buf[n] = '\0';

      // Stop when we have full headers
      if (n >= 4 && strstr(buf, "\r\n\r\n")) return n;
    }
  }
  return n; // may be partial
}

int extractComplexityFromRequestLine(const char *req) {
  // Parse only the first line: "GET /path?complexity=3 HTTP/1.1"
  const char *lineEnd = strstr(req, "\r\n");
  if (!lineEnd) return 1;

  // Find "complexity=" before lineEnd
  const char *p = strstr(req, "complexity=");
  if (!p || p > lineEnd) return 1;

  int v = atoi(p + 11);
  if (v < 1) v = 1;
  if (v > 10) v = 10;
  return v;
}

void respondJson(WiFiClient &client, unsigned long compUs, int freeRam, int rssi) {
  char payload[256];

  // NOTE: compUs is micros; decide if you want it in us or ms in JSON
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
  if (written > (int)sizeof(payload)) written = sizeof(payload);

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
  const unsigned long idleTimeoutMs = 3000;
  const unsigned long headerTimeoutMs = 1000;

  for (int i = 0; i < MAX_CLIENTS; i++) {
    WiFiClient &client = clients[i];
    if (!client) continue;

    // Idle timeout
    if (clientLastActivity[i] && (millis() - clientLastActivity[i] > idleTimeoutMs)) {
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

    unsigned long start = millis();
    clientLastActivity[i] = start;

    char headerBuf[256];
    int n = readHttpHeaders(client, headerBuf, sizeof(headerBuf), headerTimeoutMs);

    // If we didn't receive full headers, treat as bad/slow client and drop
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

    client.stop();
    client = WiFiClient();
    clientLastActivity[i] = 0;
  }
}

// ================= SETUP =================

void setup() {

  Serial.begin(115200);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP); // fixed

  SPI.begin();
  rfid.PCD_Init();

  WiFi.begin(SSID, PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  server.begin();

  startTime = millis();
}

// ================= LOOP =================

void loop() {

  ensureWiFi();
  updateMinRam();

  if (MODE_RFID) handleRFID();
  if (MODE_IOT) {
    acceptClients();
    handleClients();
  }
}