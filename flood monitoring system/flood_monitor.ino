/*
  Flood Sensing Node - ESP32
  ---------------------------------------------------------------
  Sensor : analog water level sensor  -> GPIO34 (ADC1, input only)
  RGB LED: common cathode             -> R=GPIO25, G=GPIO26, B=GPIO27 (220 ohm each)
  Buzzer : 5V active buzzer via NPN   -> GPIO23 (through 1k to transistor base)

  The local alarm (LED + buzzer) works even if Wi-Fi or the server is down.
  Board package: "esp32 by Espressif Systems" (2.x or 3.x). No extra libraries needed.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ---------- USER SETTINGS ----------
const char* WIFI_SSID  = "YOUR_WIFI_NAME";
const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
const char* SERVER_URL = "https://YOUR-APP-NAME.onrender.com/api/reading";  // your Render backend
const char* API_KEY    = "change-me";                              // must match backend
const char* DEVICE_ID  = "flood-node-1";

// Calibration: open Serial Monitor (115200) and note the raw value
// with the sensor dry and with it submerged to the maximum mark.
int RAW_DRY = 0;
int RAW_WET = 2000;

// Alert thresholds in percent of sensor length
const int WARN_PCT  = 35;   // yellow + slow beeps
const int FLOOD_PCT = 65;   // red + fast beeps
const int HYST_PCT  = 5;    // prevents flicker around a threshold

const bool COMMON_ANODE = false;  // set true if your RGB LED is common anode
// -----------------------------------

const int PIN_SENSOR = 34;
const int PIN_R = 25, PIN_G = 26, PIN_B = 27;
const int PIN_BUZZER = 23;

enum Status { SAFE, WARNING, FLOOD };
Status status = SAFE;

const int SAMPLES = 10;
int samples[SAMPLES];
int sampleIdx = 0;
bool bufferFilled = false;

unsigned long lastSample = 0, lastWifiTry = 0;
int levelPct = 0, rawAvg = 0;

const char* statusName(Status s) {
  switch (s) {
    case WARNING: return "WARNING";
    case FLOOD:   return "FLOOD";
    default:      return "SAFE";
  }
}

void setLed(bool r, bool g, bool b) {
  if (COMMON_ANODE) { r = !r; g = !g; b = !b; }
  digitalWrite(PIN_R, r);
  digitalWrite(PIN_G, g);
  digitalWrite(PIN_B, b);
}

void updateStatus(int pct) {
  switch (status) {
    case SAFE:
      if (pct >= FLOOD_PCT) status = FLOOD;
      else if (pct >= WARN_PCT) status = WARNING;
      break;
    case WARNING:
      if (pct >= FLOOD_PCT) status = FLOOD;
      else if (pct < WARN_PCT - HYST_PCT) status = SAFE;
      break;
    case FLOOD:
      if (pct < FLOOD_PCT - HYST_PCT) status = (pct >= WARN_PCT) ? WARNING : SAFE;
      break;
  }
}

// Non-blocking LED + buzzer pattern
void driveOutputs() {
  unsigned long now = millis();
  bool buzz = false;

  switch (status) {
    case SAFE:
      setLed(false, true, false);                       // green
      break;
    case WARNING:
      setLed(true, true, false);                        // yellow
      buzz = (now % 2000) < 150;                        // short beep every 2 s
      break;
    case FLOOD:
      setLed((now % 500) < 250, false, false);          // blinking red
      buzz = (now % 400) < 200;                         // fast beeping
      break;
  }
  digitalWrite(PIN_BUZZER, buzz);
}

void sampleSensor() {
  samples[sampleIdx] = analogRead(PIN_SENSOR);
  sampleIdx = (sampleIdx + 1) % SAMPLES;
  if (sampleIdx == 0) bufferFilled = true;

  int count = bufferFilled ? SAMPLES : sampleIdx;
  if (count == 0) return;
  long sum = 0;
  for (int i = 0; i < count; i++) sum += samples[i];
  rawAvg = sum / count;

  levelPct = constrain(map(rawAvg, RAW_DRY, RAW_WET, 0, 100), 0, 100);
  updateStatus(levelPct);
}

void sendReading() {
  if (WiFi.status() != WL_CONNECTED) return;

  char body[256];
  snprintf(body, sizeof(body),
           "{\"device\":\"%s\",\"level\":%d,\"raw\":%d,\"status\":\"%s\","
           "\"warn\":%d,\"flood\":%d,\"rssi\":%d}",
           DEVICE_ID, levelPct, rawAvg, statusName(status),
           WARN_PCT, FLOOD_PCT, WiFi.RSSI());

  WiFiClientSecure client;
  client.setInsecure();          // skips certificate check; fine for a hobby project
  HTTPClient http;
  http.setTimeout(8000);         // Render free tier can take a while to wake up
  http.begin(client, SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", API_KEY);
  int code = http.POST(body);
  Serial.printf("POST %s -> %d\n", body, code);
  http.end();
}

// Runs on its own core so slow HTTPS requests never delay the LED/buzzer
void sendTask(void*) {
  for (;;) {
    sendReading();
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SENSOR, ADC_11db);

  // Quick self-test: R, G, B, short beep
  setLed(true, false, false);  delay(300);
  setLed(false, true, false);  delay(300);
  setLed(false, false, true);  delay(300);
  digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting to Wi-Fi...");

  xTaskCreatePinnedToCore(sendTask, "send", 12288, NULL, 1, NULL, 0);
}

void loop() {
  unsigned long now = millis();

  if (now - lastSample >= 200) {
    lastSample = now;
    sampleSensor();
    Serial.printf("raw=%d level=%d%% status=%s\n", rawAvg, levelPct, statusName(status));
  }

  if (WiFi.status() != WL_CONNECTED && now - lastWifiTry >= 10000) {
    lastWifiTry = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  driveOutputs();
}
