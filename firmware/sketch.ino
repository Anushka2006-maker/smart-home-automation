#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DHT.h>

// ---------------- WiFi + Firebase ----------------
const char* WIFI_SSID = "Wokwi-GUEST";   // Wokwi's free simulated network
const char* WIFI_PASS = "";
const char* DB_URL = "https://smart-home-automation-a51e2-default-rtdb.asia-southeast1.firebasedatabase.app";

const unsigned long PUSH_MS = 5000;   // upload sensors + states
const unsigned long POLL_MS = 3000;   // check dashboard commands

// ---------------- Pins ----------------
#define DHT_PIN      4
#define DHT_TYPE     DHT22
#define LDR_PIN      34
#define PIR_PIN      27
#define RELAY_FAN    25
#define RELAY_LIGHT  26
#define RELAY_AUX    33

// Flip these two if a relay behaves inverted
#define RELAY_ON     HIGH
#define RELAY_OFF    LOW

// ---------------- Thresholds ----------------
const float TEMP_ON  = 30.0;
const float TEMP_OFF = 28.0;
const float HUM_ON   = 70.0;
const float HUM_OFF  = 65.0;

const int  LDR_THRESHOLD = 2000;
const bool DARK_WHEN_LOW = true;

const unsigned long LIGHT_HOLD_MS = 10000;
const unsigned long SENSE_MS      = 2000;

// ---------------- State ----------------
DHT dht(DHT_PIN, DHT_TYPE);

struct Appliance {
  const char* name;
  uint8_t pin;
  bool on;
};

Appliance appliances[3] = {
  {"Fan",   RELAY_FAN,   false},
  {"Light", RELAY_LIGHT, false},
  {"Aux",   RELAY_AUX,   false}
};

float temperature = NAN;
float humidity    = NAN;
int   ldrRaw      = 0;
bool  motionNow   = false;
bool  motionSeen  = false;
bool  autoMode    = true;
unsigned long lastMotionMs = 0;
unsigned long lastSenseMs  = 0;
unsigned long lastPushMs   = 0;
unsigned long lastPollMs   = 0;

// ---------------- Log queue (sent to Firebase when WiFi is up) ----------------
struct LogEntry {
  const char* name;
  bool on;
  const char* reason;   // always a string literal
  bool autoAtTime;
};
LogEntry logQueue[10];
int logHead = 0, logCount = 0;

void queueLog(const char* name, bool on, const char* reason) {
  if (logCount >= 10) return;   // drop if queue is full
  int tail = (logHead + logCount) % 10;
  logQueue[tail] = {name, on, reason, autoMode};
  logCount++;
}

// ---------------- Firebase REST helper ----------------
// Returns HTTP status code (negative on failure). Response body goes in *resp if given.
int firebaseRequest(const char* method, const String& path, const String& body, String* resp = nullptr) {
  if (WiFi.status() != WL_CONNECTED) return -1;

  WiFiClientSecure client;
  client.setInsecure();   // fine for a simulation; no certificate pinning
  HTTPClient http;
  String url = String(DB_URL) + path + ".json";
  if (!http.begin(client, url)) return -2;
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");

  int code;
  if (strcmp(method, "GET") == 0) code = http.GET();
  else code = http.sendRequest(method, body);

  if (resp && code > 0) *resp = http.getString();
  http.end();
  return code;
}

// ---------------- Helpers ----------------
bool isDark() {
  return DARK_WHEN_LOW ? (ldrRaw < LDR_THRESHOLD) : (ldrRaw > LDR_THRESHOLD);
}

void setAppliance(int i, bool on, const char* reason) {
  if (appliances[i].on == on) return;
  appliances[i].on = on;
  digitalWrite(appliances[i].pin, on ? RELAY_ON : RELAY_OFF);
  Serial.printf("[%lus] %s -> %s (%s)\n",
                millis() / 1000, appliances[i].name, on ? "ON" : "OFF", reason);
  queueLog(appliances[i].name, on, reason);
}

void printStatus() {
  Serial.printf("T=%.1fC  H=%.1f%%  LDR=%d (%s)  Motion=%s  Mode=%s  |  Fan:%s Light:%s Aux:%s  WiFi:%s\n",
                temperature, humidity, ldrRaw, isDark() ? "dark" : "bright",
                motionNow ? "YES" : "no", autoMode ? "AUTO" : "MANUAL",
                appliances[0].on ? "ON" : "off",
                appliances[1].on ? "ON" : "off",
                appliances[2].on ? "ON" : "off",
                WiFi.status() == WL_CONNECTED ? "ok" : "down");
}

// ---------------- Automation ----------------
void runAutomation() {
  if (!isnan(temperature)) {
    if (temperature > TEMP_ON)       setAppliance(0, true,  "temperature high");
    else if (temperature < TEMP_OFF) setAppliance(0, false, "temperature normal");
  }

  bool recentMotion = motionSeen && (millis() - lastMotionMs < LIGHT_HOLD_MS);
  if (isDark() && recentMotion) setAppliance(1, true,  "dark + motion");
  else if (!recentMotion)       setAppliance(1, false, "no recent motion");
  else                          setAppliance(1, false, "room is bright");

  if (!isnan(humidity)) {
    if (humidity > HUM_ON)       setAppliance(2, true,  "humidity high");
    else if (humidity < HUM_OFF) setAppliance(2, false, "humidity normal");
  }
}

// ---------------- Firebase: upload ----------------
void pushState() {
  if (isnan(temperature) || isnan(humidity)) return;   // wait for first valid reading

  String body = "{\"sensors\":{";
  body += "\"temperature\":" + String(temperature, 1) + ",";
  body += "\"humidity\":"    + String(humidity, 1)    + ",";
  body += "\"ldr\":"         + String(ldrRaw)         + ",";
  body += "\"dark\":"        + String(isDark() ? "true" : "false") + ",";
  body += "\"motion\":"      + String(motionNow ? "true" : "false");
  body += "},\"appliances\":{";
  body += "\"fan\":"   + String(appliances[0].on ? "true" : "false") + ",";
  body += "\"light\":" + String(appliances[1].on ? "true" : "false") + ",";
  body += "\"aux\":"   + String(appliances[2].on ? "true" : "false");
  body += "},\"status\":{";
  body += "\"mode\":\"" + String(autoMode ? "auto" : "manual") + "\",";
  body += "\"uptime\":" + String(millis() / 1000) + ",";
  body += "\"updated\":{\".sv\":\"timestamp\"}";
  body += "}}";

  int code = firebaseRequest("PATCH", "/smarthome", body);
  Serial.printf("Firebase push: %d\n", code);
}

void flushLog() {
  if (logCount == 0) return;
  LogEntry e = logQueue[logHead];
  String body = "{\"appliance\":\"" + String(e.name) + "\",";
  body += "\"state\":\"" + String(e.on ? "ON" : "OFF") + "\",";
  body += "\"reason\":\"" + String(e.reason) + "\",";
  body += "\"mode\":\"" + String(e.autoAtTime ? "AUTO" : "MANUAL") + "\",";
  body += "\"ts\":{\".sv\":\"timestamp\"}}";

  int code = firebaseRequest("POST", "/smarthome/logs", body);
  if (code == 200) {          // only drop from queue once Firebase accepted it
    logHead = (logHead + 1) % 10;
    logCount--;
  }
}

// ---------------- Firebase: dashboard commands ----------------
// /smarthome/control = {"mode":"auto"|"manual","fan":bool,"light":bool,"aux":bool}
void pollControl() {
  String resp;
  int code = firebaseRequest("GET", "/smarthome/control", "", &resp);
  if (code != 200 || resp == "null") return;

  bool manual = resp.indexOf("\"mode\":\"manual\"") >= 0;
  if (manual != !autoMode) {
    autoMode = !manual;
    Serial.println(manual ? ">> MANUAL mode (dashboard)" : ">> AUTO mode (dashboard)");
  }
  if (manual) {
    setAppliance(0, resp.indexOf("\"fan\":true")   >= 0, "dashboard override");
    setAppliance(1, resp.indexOf("\"light\":true") >= 0, "dashboard override");
    setAppliance(2, resp.indexOf("\"aux\":true")   >= 0, "dashboard override");
  }
}

void writeDefaultControl() {
  String body = "{\"mode\":\"auto\",\"fan\":false,\"light\":false,\"aux\":false}";
  int code = firebaseRequest("PUT", "/smarthome/control", body);
  Serial.printf("Firebase default control: %d\n", code);
}

// ---------------- Serial (status only; control comes from dashboard) ----------------
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 's') printStatus();
  }
}

// ---------------- Arduino ----------------
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);   // channel 6 speeds up connection in Wokwi
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " connected" : " FAILED (running offline)");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Smart Home Automation - Phase 2 (Firebase)");

  dht.begin();
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  for (int i = 0; i < 3; i++) {
    pinMode(appliances[i].pin, OUTPUT);
    digitalWrite(appliances[i].pin, RELAY_OFF);
  }

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) writeDefaultControl();
}

void loop() {
  handleSerial();

  motionNow = digitalRead(PIR_PIN) == HIGH;
  if (motionNow) { lastMotionMs = millis(); motionSeen = true; }

  if (millis() - lastSenseMs >= SENSE_MS) {
    lastSenseMs = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) { temperature = t; humidity = h; }
    else Serial.println("DHT read failed");
    ldrRaw = analogRead(LDR_PIN);
    printStatus();
  }

  if (autoMode) runAutomation();

  if (WiFi.status() == WL_CONNECTED) {
    if (millis() - lastPollMs >= POLL_MS) { lastPollMs = millis(); pollControl(); }
    if (millis() - lastPushMs >= PUSH_MS) { lastPushMs = millis(); pushState(); }
    flushLog();
  }

  delay(50);
}
