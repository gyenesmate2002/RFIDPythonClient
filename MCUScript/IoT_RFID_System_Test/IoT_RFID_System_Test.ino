#include <SPI.h>
#include <MFRC522.h>
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <WiFiSSLClient.h>

// ---------- RFID ----------
#define SS_PIN 10
#define RST_PIN 9
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------- WiFi ----------
char SSID[] = "TP-Link_3838";
char PASS[] = "24701570";
char IP[] = "192.168.0.163";
int PORT = 8000;

WiFiClient wifi;
HttpClient client = HttpClient(wifi, IP, PORT);

// ---------- SERVER ----------
WiFiServer server(80);

// ---------- SYSTEM STATS ----------
unsigned long startTime;
unsigned long wifiReconnectCount = 0;
unsigned long errorCount = 0;
unsigned long tlsHandshakeTime = 0;

// ---------- HTTP STATS ----------
unsigned long httpTotal = 0;
unsigned long httpCount = 0;

// ---------- RFID STATS ----------
unsigned long rfidTotal = 0;
unsigned long rfidCount = 0;

// ---------- RAM ----------
int minFreeRam = 999999;

// ---------- RFID MEASUREMENT BUFFER ----------

struct RFIDMeasurement {
  float rfid_reaction_time;
  float http_latency;
  float tls_handshake_time;
  String status;
  int free_ram;
  int wifi_reconnects;
};

RFIDMeasurement buffer[10];
int bufferIndex = 0;

// ---------- IOT CLIENTS BUFFER ----------

#define MAX_CLIENTS 10

WiFiClient clients[MAX_CLIENTS];

const char RESPONSE[] =
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: application/json\r\n"
  "Connection: close\r\n\r\n"
  "{\"status\":\"ok\"}";

// ---------- MEASUREMENT MODE ----------

bool isIoT = true;
bool isRFID = false;

// -------------------------------------------------

extern "C" char* sbrk(int incr);

int freeMemory() {
  char stack_dummy = 0;
  return &stack_dummy - sbrk(0);
}

void updateRamStats() {
  int current = freeMemory();
  if (current < minFreeRam) minFreeRam = current;
}

// -------------------------------------------------
// CPU benchmark
// -------------------------------------------------

void cpuBenchmark() {

  const long iterations = 500000;
  volatile long sum = 0;

  unsigned long cpuBenchmarkstart = millis();

  for (long i = 0; i < iterations; i++) {
    sum += i;
  }

  unsigned long cpuBenchmarkduration = millis() - cpuBenchmarkstart;

  Serial.print("[CPU] Benchmark time (ms): ");
  Serial.println(cpuBenchmarkduration);
}

// -------------------------------------------------
// TLS handshake benchmark
// -------------------------------------------------

void measureTLSHandshake() {

  WiFiSSLClient ssl;

  unsigned long start = millis();

  if (!ssl.connect("example.com", 443)) {
    Serial.println("[TLS] Connection failed");
    return;
  }

  unsigned long handshakeTime = millis() - start;

  tlsHandshakeTime = handshakeTime;

  Serial.print("[TLS] Handshake time (ms): ");
  Serial.println(handshakeTime);

  ssl.stop();
}

// -------------------------------------------------
// IoT gateway tests handler
// -------------------------------------------------

void handleClients() {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    WiFiClient &client = clients[i];

    if (!client || !client.connected()) continue;

    if (client.available()) {
      char buffer[128];
      int len = client.read((uint8_t*)buffer, sizeof(buffer) - 1);  // non-blocking

      if (len <= 0) continue;

      buffer[len] = '\0';

      // Faster check (no full strstr scan)
      if (buffer[0] == 'G') { // "GET ..."
        if (strstr(buffer, "/benchmark")) {
          client.print(RESPONSE);
        }
      }

      // Give TCP time to send (critical fix)
      delay(1);

      client.stop();
    }
  }
}

// -------------------------------------------------
// IoT Clients Buffer Handler
// -------------------------------------------------

void acceptClients() {
  WiFiClient newClient = server.available();
  
  if (newClient) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i] || !clients[i].connected()) {
        clients[i] = newClient;
        return;
      }
    }
    // No free slot → reject
    newClient.stop();
  }
}

// -------------------------------------------------
// RFID system handler
// -------------------------------------------------

void handleRFID() {

  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  Serial.println("Card detected");

  if (!rfid.PICC_ReadCardSerial()) {
    Serial.println("Failed to read card");
    return;
  }

  unsigned long rfidStart = micros();

  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {

    if (rfid.uid.uidByte[i] < 0x10) uid += "0";

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();

  unsigned long rfidTime = micros() - rfidStart;

  unsigned long httpStart = millis();

  String url = "/arduino/rfid_scan/?uid=" + uid;
  client.get(url);

  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  client.stop();

  unsigned long httpTime = millis() - httpStart;
  unsigned long totalTime = (rfidTime / 1000) + httpTime;

  // Parse status from response
  String statusValue = "error";

  if (response.indexOf("accepted") != -1) {
    statusValue = "accepted";
  }
  else if (response.indexOf("denied") != -1) {
    statusValue = "denied";
  } 
  else {
    statusValue = "error";
  }

  // Save measurement locally

  if (bufferIndex < 10) {
    buffer[bufferIndex].rfid_reaction_time = rfidTime;
    buffer[bufferIndex].http_latency = httpTime;
    buffer[bufferIndex].tls_handshake_time = tlsHandshakeTime;
    buffer[bufferIndex].status = statusValue;
    buffer[bufferIndex].free_ram = freeMemory();
    buffer[bufferIndex].wifi_reconnects = wifiReconnectCount;

    bufferIndex++;
  }


  Serial.print("Buffered measurements: ");
  Serial.println(bufferIndex);

  httpTotal += httpTime;
  httpCount++;

  rfidTotal += totalTime;
  rfidCount++;

  Serial.println("\n--- RFID MEASUREMENT ---");

  Serial.print("HTTP response time (ms): ");
  Serial.println(httpTime);

  Serial.print("RFID reaction time (μs): ");
  Serial.println(rfidTime);

  Serial.print("Total functionality time (ms): ");
  Serial.println(totalTime);

  Serial.print("Free RAM after RFID: ");
  Serial.println(freeMemory());

  if (statusCode < 200 || statusCode >= 300) {
    errorCount++;
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(1500);
}

// -------------------------------------------------
// Saved RFID measurements handler
// -------------------------------------------------

void sendBufferedMeasurements() {

  Serial.println("Sending buffered RFID measurements...");

  for (int i = 0; i < bufferIndex; i++) {

    String payload = "{";
    payload += "\"rfid_reaction_time\":" + String(buffer[i].rfid_reaction_time) + ",";
    payload += "\"http_latency\":" + String(buffer[i].http_latency) + ",";
    payload += "\"tls_handshake_time\":" + String(buffer[i].tls_handshake_time) + ",";
    payload += "\"status\":\"" + buffer[i].status + "\",";
    payload += "\"free_ram\":" + String(buffer[i].free_ram) + ",";
    payload += "\"wifi_reconnects\":" + String(buffer[i].wifi_reconnects);
    payload += "}";

    client.beginRequest();
    client.post("/arduino/rfid-save-measurements/");
    client.sendHeader("Content-Type", "application/json");
    client.sendHeader("Content-Length", payload.length());
    client.beginBody();
    client.print(payload);
    client.endRequest();

    int statusCode = client.responseStatusCode();
    String response = client.responseBody();

    Serial.print("Saved measurement status: ");
    Serial.println(statusCode);

    delay(200);
  }

  bufferIndex = 0;
}

// -------------------------------------------------
// System stats
// -------------------------------------------------

void printSystemStats() {

  Serial.println("\n===== SYSTEM BENCHMARK =====");

  Serial.print("Uptime (s): ");
  Serial.println((millis() - startTime) / 1000);

  Serial.print("Free RAM: ");
  Serial.println(freeMemory());

  Serial.print("Min RAM observed: ");
  Serial.println(minFreeRam);

  Serial.print("WiFi reconnects: ");
  Serial.println(wifiReconnectCount);

  Serial.print("HTTP avg time (ms): ");
  if (httpCount > 0)
    Serial.println(httpTotal / httpCount);

  Serial.print("RFID avg time (ms): ");
  if (rfidCount > 0)
    Serial.println(rfidTotal / rfidCount);

  Serial.print("Errors: ");
  Serial.println(errorCount);

  Serial.println("=============================");
}

// -------------------------------------------------

void setup() {

  Serial.begin(115200);

  SPI.begin();
  rfid.PCD_Init();

  startTime = millis();

  Serial.print("Connecting to WiFi");

  WiFi.begin(SSID, PASS);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());

  server.begin();

  cpuBenchmark();
  measureTLSHandshake();

  Serial.println("\n===== TEST CAN BEGIN =====");
}

void loop() {

  if (isIoT) {

    acceptClients();
    handleClients();

  }
  else if (isRFID) {

    static uint8_t lastWiFiStatus = WL_CONNECTED;
    uint8_t currentStatus = WiFi.status();

    if (lastWiFiStatus == WL_CONNECTED && currentStatus != WL_CONNECTED) {
      wifiReconnectCount++;
    }

    lastWiFiStatus = currentStatus;

    updateRamStats();

    handleRFID();

    static unsigned long lastStatusPrint = 0;

    if (millis() - lastStatusPrint > 10000) {
      lastStatusPrint = millis();
      printSystemStats();
    }

    if (bufferIndex >= 9) {
      sendBufferedMeasurements();
    }
  }
}
