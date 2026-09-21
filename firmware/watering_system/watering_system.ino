#include <DHT.h>
// NimBLE instead of the stock BLEDevice.h (Bluedroid): Bluedroid's GATT server
// is known to fail the connect handshake from Windows Web Bluetooth with a
// generic "Connection failed for unknown reason" error. NimBLE fixes this.
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

// ---------- Pins ----------
#define DHTPIN 16
#define DHTTYPE DHT22
#define SOIL_PIN 34
#define RELAY_PIN 27

// PLACEHOLDERS — must be recalibrated per physical sensor. Enable DEBUG_SOIL_RAW
// below, open Serial Monitor, and note the raw analogRead() value in dry air and
// again with the probe in wet soil, then set these two to what you measured.
// Also note: if your sensor's WET reading is LOWER than its DRY reading (typical
// for resistive/capacitive probes wired the usual way), use the first map() call
// in readMoisturePercent(); if it's inverted on your sensor (wet > dry), use the
// second one instead — see that function below.
#define SOIL_DRY 3000
#define SOIL_WET 1200

// Set to 1 to continuously print the raw ADC value from SOIL_PIN over Serial so
// you can capture real dry-air/wet-soil readings for calibration above. Leave at
// 0 for normal operation.
#define DEBUG_SOIL_RAW 0

// ---------- WiFi (STA optional, AP always on except during first-time
// provisioning — all of this runs alongside Bluetooth, which keeps working
// unchanged either way) ----------
// STA: fill in your home network's credentials before uploading, or leave
// WIFI_SSID blank and use the SmartGrow-Setup provisioning flow below
// instead. Once connected, the dashboard also reaches the device at
// http://plantwaterer.local/. Never commit your real credentials — keep
// this file local to your device.
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""
#define WIFI_HOSTNAME "plantwaterer"
// AP: the ESP32 always broadcasts one of these two networks — only one AP
// SSID can be active on the radio at a time, so the control AP and the
// provisioning AP below are mutually exclusive, never simultaneous.
#define AP_SSID     "SmartGrow"
#define AP_PASSWORD "12345678"
#define AP_IP_ADDR  192, 168, 4, 1
// Provisioning AP: shown instead of AP_SSID only while no STA network has
// been saved yet (fresh device, or after a WIFI_RESET). Serves a page at
// http://192.168.4.1/ to enter a home network's SSID/password.
#define PROV_AP_SSID     "SmartGrow-Setup"
#define PROV_AP_PASSWORD "12345678"

// ---------- BLE UUIDs (must match the app's src/constants/ble.ts) ----------
#define SERVICE_UUID      "12345678-1234-5678-1234-56789abc0000"
#define SENSOR_CHAR_UUID  "12345678-1234-5678-1234-56789abc0001"
#define EVENT_CHAR_UUID   "12345678-1234-5678-1234-56789abc0002"
#define CONFIG_CHAR_UUID  "12345678-1234-5678-1234-56789abc0003"
#define COMMAND_CHAR_UUID "12345678-1234-5678-1234-56789abc0004"
#define TIME_CHAR_UUID    "12345678-1234-5678-1234-56789abc0005"
#define HISTORY_CHAR_UUID "12345678-1234-5678-1234-56789abc0006"
#define LOG_CHAR_UUID     "12345678-1234-5678-1234-56789abc0007"
#define DEVICE_NAME       "PlantWaterer"

// On-device watering log so the app can catch up on events that happened
// while it wasn't connected (auto-watering runs with no phone/browser
// attached at all). Ring buffer, persisted to flash after every watering.
#define HISTORY_CAPACITY 10

// ---------- Timing ----------
const unsigned long PUMP_PULSE_MS         = 1500;    // burst length between checks, both pump modes
const unsigned long PUMP_SETTLE_MS        = 800;     // pause after a burst so the reading reflects reality

// ---------- Defaults ----------
// Auto-watering fires only when ALL THREE are true: temp in [MIN,MAX], humidity
// in [MIN,MAX], and soil % below the threshold (it's dry).
const float DEFAULT_TEMP_MIN        = 18.0;
const float DEFAULT_TEMP_MAX        = 30.0;
const float DEFAULT_HUM_MIN         = 30.0;
const float DEFAULT_HUM_MAX         = 70.0;
const int   DEFAULT_SOIL_THRESHOLD  = 20;   // water only if moisture % is below this (it's dry)
const int   DEFAULT_SOIL_WET_TARGET = 65;   // moisture-mode pump target: stop once soil reaches this %
const unsigned long DEFAULT_PUMP_DURATION_MS = 20000; // fixed-duration pump mode run length
const int   DEFAULT_SOIL_MAX_PERCENT = 80;  // universal safety cap: stop ANY watering immediately at this %
// 0 by default (no cooldown) so auto-watering is instantly visible for demos;
// set higher in Settings to space out real-world AUTO waterings (manual
// "Water Now" always bypasses this regardless of its value).
const unsigned long DEFAULT_MIN_WATER_INTERVAL_MS = 0;
// How often sensors are read/published — fast (1s) by default so changes are
// immediately visible for demos/testing; raise it in Settings for real-world
// use to cut down on DHT reads and BLE notify traffic.
const unsigned long DEFAULT_SENSOR_INTERVAL_MS = 1000;

DHT dht(DHTPIN, DHTTYPE);
Preferences prefs;

NimBLEServer *pServer;
NimBLECharacteristic *sensorChar;
NimBLECharacteristic *eventChar;
NimBLECharacteristic *configChar;
NimBLECharacteristic *commandChar;
NimBLECharacteristic *timeChar;
NimBLECharacteristic *historyChar;
NimBLECharacteristic *logChar;

bool deviceConnected = false;

// ---------- WiFi / local HTTP API ----------
// Runs alongside Bluetooth (not instead of it) so the dashboard works from
// anywhere on the same WiFi network, not just within BLE range. Every route
// mirrors a BLE characteristic 1:1 and reuses the exact same apply/build
// functions, so the two transports can never drift out of sync.
WebServer webServer(80);
bool wifiReady = false;
IPAddress apIP(AP_IP_ADDR); // shared by both AP identities — same fixed address either way

// ---------- WiFi provisioning (SmartGrow-Setup) ----------
// Runs only until a STA network has been saved; the control AP/HTTP API/BLE
// above are untouched either way. DNS + the HTTP catch-all below implement a
// minimal captive portal so most phones offer the setup page on their own.
DNSServer dnsServer;
const unsigned long STA_CONNECT_TIMEOUT_MS = 15000;
enum ProvisioningState { PROV_IDLE, PROV_CONNECTING, PROV_FAILED };
bool provisioningActive = false;
ProvisioningState provState = PROV_IDLE;
String staSsidSaved;
String staPassSaved;
String pendingStaSsid;
String pendingStaPass;
String provisioningError;
unsigned long staConnectStartMillis = 0;

#define LOG_BUFFER_CAPACITY 50
String logBuffer[LOG_BUFFER_CAPACITY];
uint32_t logBufferSeq[LOG_BUFFER_CAPACITY];
uint8_t logBufferHead = 0;
uint8_t logBufferCount = 0;
uint32_t logSeqCounter = 0;

// ---------- Watering history (ring buffer, persisted) ----------
struct WateringLogEntry {
  uint32_t epoch; // 0 means "time unknown" (device hadn't been time-synced yet)
  float temp;
  float hum;
  int16_t soil;
};
WateringLogEntry history[HISTORY_CAPACITY];
uint8_t historyCount = 0;
uint8_t historyHead = 0; // index the NEXT entry will be written to

float tempMin = DEFAULT_TEMP_MIN;
float tempMax = DEFAULT_TEMP_MAX;
float humMin = DEFAULT_HUM_MIN;
float humMax = DEFAULT_HUM_MAX;
int soilThreshold = DEFAULT_SOIL_THRESHOLD;

String pumpMode = "duration"; // "duration" (fixed burst) or "moisture" (until soil is wet)
int soilWetTarget = DEFAULT_SOIL_WET_TARGET;
unsigned long pumpDurationMs = DEFAULT_PUMP_DURATION_MS;
int soilMaxPercent = DEFAULT_SOIL_MAX_PERCENT; // stop any watering immediately once soil hits this %
unsigned long minWaterIntervalMs = DEFAULT_MIN_WATER_INTERVAL_MS; // cooldown between AUTO waterings (manual bypasses it)
unsigned long sensorIntervalMs = DEFAULT_SENSOR_INTERVAL_MS; // how often sensors are read/published
bool systemEnabled = true; // master switch: disables auto-watering (conditions + schedule) and force-stops the pump

// ---------- Schedule ----------
bool schedEnabled = true;
String schedMode = "daily";   // "daily" or "interval"
int schedHour = 5;
int schedMinute = 0;
int schedIntervalHours = 1;
uint8_t schedDaysMask = 0x7F; // bit 0 = Sunday ... bit 6 = Saturday; all on by default

unsigned long lastSchedDayFired = 0xFFFFFFFF;  // "days since epoch" of the last scheduled run
unsigned long lastSchedHourFired = 0xFFFFFFFF; // "hours since epoch" of the last scheduled run

// ---------- Phone-synced clock ----------
// The ESP32 has no RTC. The web/app pushes the current local time (epoch
// seconds, pre-shifted for the browser's timezone) on connect and on every
// auto-refresh; we track wall time against millis() in between syncs.
unsigned long timeSyncEpoch = 0;
unsigned long timeSyncMillis = 0;
bool timeSynced = false;

unsigned long lastCheck = 0;
// "Never watered yet" sentinel — set properly in setup() once minWaterIntervalMs
// is loaded (see the comment there). Left as 0 here since the real value
// depends on a config value we haven't read yet at global-init time.
unsigned long lastWaterMillis = 0;

float lastTemp = NAN;
float lastHum = NAN;
bool pumpOn = false;

// ---------- Pump state machine (non-blocking) ----------
// Watering used to be one long blocking call. That meant nothing else — not
// sensor publishes, not an emergency stop command — could happen while the
// pump ran for up to ~20s. This drives the same on/off pulsing pattern from
// loop() one step at a time instead, so the rest of the device stays live.
bool pumpActive = false;
bool pumpRelayOn = false;
bool pumpRequestStop = false;
unsigned long pumpStartMillis = 0;
unsigned long pumpPhaseStartMillis = 0;
unsigned long pumpMaxRunMs = 0;

// BLE write callbacks run on NimBLE's own task; keep them fast and defer real
// work to loop().
volatile bool pendingWaterNow = false;
volatile bool pendingStopNow = false;
volatile bool pendingReset = false;
volatile bool pendingRefresh = false;
volatile bool pendingWifiReset = false;

void publishSensorData();
void publishPumpStatus();
void startWatering(bool manual);

// ---------- Debug log (mirrors Serial to a BLE characteristic for the web
// app's live "Console" tab — testing/debugging only, no functional effect) ----------
void logLine(const String &msg) {
  Serial.println(msg);

  logSeqCounter++;
  logBuffer[logBufferHead] = msg;
  logBufferSeq[logBufferHead] = logSeqCounter;
  logBufferHead = (logBufferHead + 1) % LOG_BUFFER_CAPACITY;
  if (logBufferCount < LOG_BUFFER_CAPACITY) logBufferCount++;

  if (deviceConnected) {
    // BLE notifications are capped by the negotiated ATT MTU (default as low
    // as 20-ish bytes, often 200+ in practice); a message longer than that
    // just gets truncated on the wire for this live view. Serial above still
    // gets the full text either way.
    logChar->setValue(msg.c_str());
    logChar->notify();
  }
}

// ---------- Config persistence ----------
void loadConfig() {
  prefs.begin("watering", true);
  tempMin = prefs.getFloat("tempMin", DEFAULT_TEMP_MIN);
  tempMax = prefs.getFloat("tempMax", DEFAULT_TEMP_MAX);
  humMin = prefs.getFloat("humMin", DEFAULT_HUM_MIN);
  humMax = prefs.getFloat("humMax", DEFAULT_HUM_MAX);
  soilThreshold = prefs.getInt("soilTh", DEFAULT_SOIL_THRESHOLD);
  pumpMode = prefs.getString("pumpMode", "duration");
  soilWetTarget = prefs.getInt("wetTarget", DEFAULT_SOIL_WET_TARGET);
  pumpDurationMs = prefs.getULong("pumpDurMs", DEFAULT_PUMP_DURATION_MS);
  soilMaxPercent = prefs.getInt("soilMax", DEFAULT_SOIL_MAX_PERCENT);
  minWaterIntervalMs = prefs.getULong("cooldownMs", DEFAULT_MIN_WATER_INTERVAL_MS);
  sensorIntervalMs = prefs.getULong("sensorIntMs", DEFAULT_SENSOR_INTERVAL_MS);
  systemEnabled = prefs.getBool("sysEnabled", true);
  schedEnabled = prefs.getBool("schedOn", true);
  schedMode = prefs.getString("schedMode", "daily");
  schedHour = prefs.getInt("schedHour", 5);
  schedMinute = prefs.getInt("schedMin", 0);
  schedIntervalHours = prefs.getInt("schedIntH", 1);
  schedDaysMask = prefs.getUChar("schedDays", 0x7F);
  prefs.end();
}

void saveConfig() {
  prefs.begin("watering", false);
  prefs.putFloat("tempMin", tempMin);
  prefs.putFloat("tempMax", tempMax);
  prefs.putFloat("humMin", humMin);
  prefs.putFloat("humMax", humMax);
  prefs.putInt("soilTh", soilThreshold);
  prefs.putString("pumpMode", pumpMode);
  prefs.putInt("wetTarget", soilWetTarget);
  prefs.putULong("pumpDurMs", pumpDurationMs);
  prefs.putInt("soilMax", soilMaxPercent);
  prefs.putULong("cooldownMs", minWaterIntervalMs);
  prefs.putULong("sensorIntMs", sensorIntervalMs);
  prefs.putBool("sysEnabled", systemEnabled);
  prefs.putBool("schedOn", schedEnabled);
  prefs.putString("schedMode", schedMode);
  prefs.putInt("schedHour", schedHour);
  prefs.putInt("schedMin", schedMinute);
  prefs.putInt("schedIntH", schedIntervalHours);
  prefs.putUChar("schedDays", schedDaysMask);
  prefs.end();
}

// ---------- STA Wi-Fi credential persistence (separate keys from the rest
// of the config above, so a Wi-Fi reset never touches irrigation settings) ----------
void loadWifiCreds() {
  prefs.begin("watering", true);
  staSsidSaved = prefs.getString("staSsid", WIFI_SSID);
  staPassSaved = prefs.getString("staPass", WIFI_PASSWORD);
  prefs.end();
}

void saveWifiCreds(const String &ssid, const String &pass) {
  prefs.begin("watering", false);
  prefs.putString("staSsid", ssid);
  prefs.putString("staPass", pass);
  prefs.end();
}

void clearWifiCreds() {
  prefs.begin("watering", false);
  prefs.remove("staSsid");
  prefs.remove("staPass");
  prefs.end();
}

// Shared by the BLE COMMAND "WIFI_RESET" and HTTP POST /wifi/reset — the only
// place that drops a saved STA network. Restarting is simpler and safer than
// tearing down an active STA/mDNS/AP identity in place, and setup() already
// knows to fall back into provisioning once staSsidSaved comes back empty.
void resetWifiCredentials() {
  clearWifiCreds();
  logLine("[wifi] credentials cleared, restarting into provisioning mode");
  ESP.restart();
}

// ---------- History persistence ----------
void loadHistory() {
  prefs.begin("watering", true);
  size_t len = prefs.getBytesLength("histBuf");
  if (len == sizeof(history)) {
    prefs.getBytes("histBuf", history, sizeof(history));
    historyHead = prefs.getUChar("histHead", 0);
    historyCount = prefs.getUChar("histCount", 0);
  }
  prefs.end();
}

void saveHistory() {
  prefs.begin("watering", false);
  prefs.putBytes("histBuf", history, sizeof(history));
  prefs.putUChar("histHead", historyHead);
  prefs.putUChar("histCount", historyCount);
  prefs.end();
}

String buildHistoryJson() {
  // Oldest-first. When the buffer is full, historyHead already points at the
  // oldest surviving entry (the next slot to be overwritten); otherwise the
  // oldest entry is always at index 0.
  int oldestIndex = (historyCount < HISTORY_CAPACITY) ? 0 : historyHead;

  StaticJsonDocument<2048> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < historyCount; i++) {
    int idx = (oldestIndex + i) % HISTORY_CAPACITY;
    JsonObject o = arr.createNestedObject();
    o["t"] = history[idx].epoch;
    o["temp"] = history[idx].temp;
    o["hum"] = history[idx].hum;
    o["soil"] = history[idx].soil;
  }

  String out;
  serializeJson(doc, out);
  logLine("[history] entries=" + String(historyCount) + " overflowed=" + String(doc.overflowed()) + " json=" + out);
  return out;
}

void updateHistoryChar() {
  historyChar->setValue(buildHistoryJson().c_str());
}

void recordWateringEvent(int soilAfter) {
  logLine("[history] recording watering event, soilAfter=" + String(soilAfter));
  WateringLogEntry &entry = history[historyHead];
  entry.epoch = timeSynced ? (uint32_t)currentLocalEpoch() : 0;
  entry.temp = lastTemp;
  entry.hum = lastHum;
  entry.soil = soilAfter;

  historyHead = (historyHead + 1) % HISTORY_CAPACITY;
  if (historyCount < HISTORY_CAPACITY) historyCount++;

  saveHistory();
  updateHistoryChar();
}

String buildConfigJson() {
  StaticJsonDocument<768> doc;
  doc["tempMin"] = tempMin;
  doc["tempMax"] = tempMax;
  doc["humMin"] = humMin;
  doc["humMax"] = humMax;
  doc["soilThreshold"] = soilThreshold;
  doc["pumpMode"] = pumpMode;
  doc["soilWetTarget"] = soilWetTarget;
  doc["pumpDurationMs"] = pumpDurationMs;
  doc["soilMaxPercent"] = soilMaxPercent;
  doc["cooldownMs"] = minWaterIntervalMs;
  doc["sensorIntervalMs"] = sensorIntervalMs;
  doc["systemEnabled"] = systemEnabled;

  JsonObject sched = doc.createNestedObject("schedule");
  sched["enabled"] = schedEnabled;
  sched["mode"] = schedMode;
  sched["hour"] = schedHour;
  sched["minute"] = schedMinute;
  sched["intervalHours"] = schedIntervalHours;
  JsonArray days = sched.createNestedArray("days");
  for (int i = 0; i < 7; i++) days.add((bool)((schedDaysMask >> i) & 1));

  String out;
  serializeJson(doc, out);
  return out;
}

void pushConfig() {
  String out = buildConfigJson();
  configChar->setValue(out.c_str());
  if (deviceConnected) configChar->notify();
}

void resetToDefaults() {
  tempMin = DEFAULT_TEMP_MIN;
  tempMax = DEFAULT_TEMP_MAX;
  humMin = DEFAULT_HUM_MIN;
  humMax = DEFAULT_HUM_MAX;
  soilThreshold = DEFAULT_SOIL_THRESHOLD;
  pumpMode = "duration";
  soilWetTarget = DEFAULT_SOIL_WET_TARGET;
  pumpDurationMs = DEFAULT_PUMP_DURATION_MS;
  soilMaxPercent = DEFAULT_SOIL_MAX_PERCENT;
  minWaterIntervalMs = DEFAULT_MIN_WATER_INTERVAL_MS;
  sensorIntervalMs = DEFAULT_SENSOR_INTERVAL_MS;
  systemEnabled = true;
  schedEnabled = true;
  schedMode = "daily";
  schedHour = 5;
  schedMinute = 0;
  schedIntervalHours = 1;
  schedDaysMask = 0x7F;
  saveConfig();
  pushConfig();
}

// ---------- BLE callbacks ----------
class ServerCallbacks : public NimBLEServerCallbacks {
  // NimBLE 2.x passes connection info as a second argument on every callback.
  void onConnect(NimBLEServer *s, NimBLEConnInfo &connInfo) override {
    deviceConnected = true;
  }
  void onDisconnect(NimBLEServer *s, NimBLEConnInfo &connInfo, int reason) override {
    deviceConnected = false;
    NimBLEDevice::startAdvertising(); // stay discoverable after a disconnect
  }
};

// Shared by both the BLE CONFIG characteristic and the HTTP POST /api/config
// route, so the two transports can never apply config differently.
bool applyConfigJson(const String &value) {
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, value) != DeserializationError::Ok) return false;

  if (doc.containsKey("tempMin")) tempMin = doc["tempMin"];
  if (doc.containsKey("tempMax")) tempMax = doc["tempMax"];
  if (doc.containsKey("humMin")) humMin = doc["humMin"];
  if (doc.containsKey("humMax")) humMax = doc["humMax"];
  if (doc.containsKey("soilThreshold")) soilThreshold = doc["soilThreshold"];
  if (doc.containsKey("pumpMode")) pumpMode = doc["pumpMode"].as<String>();
  if (doc.containsKey("soilWetTarget")) soilWetTarget = doc["soilWetTarget"];
  if (doc.containsKey("pumpDurationMs")) pumpDurationMs = doc["pumpDurationMs"];
  if (doc.containsKey("soilMaxPercent")) soilMaxPercent = doc["soilMaxPercent"];
  if (doc.containsKey("cooldownMs")) minWaterIntervalMs = doc["cooldownMs"];
  if (doc.containsKey("sensorIntervalMs")) sensorIntervalMs = doc["sensorIntervalMs"];
  if (doc.containsKey("systemEnabled")) systemEnabled = doc["systemEnabled"];

  if (doc.containsKey("schedule")) {
    JsonObject sched = doc["schedule"];
    if (sched.containsKey("enabled")) schedEnabled = sched["enabled"];
    if (sched.containsKey("mode")) schedMode = sched["mode"].as<String>();
    if (sched.containsKey("hour")) schedHour = sched["hour"];
    if (sched.containsKey("minute")) schedMinute = sched["minute"];
    if (sched.containsKey("intervalHours")) schedIntervalHours = sched["intervalHours"];
    if (sched.containsKey("days")) {
      JsonArray days = sched["days"];
      uint8_t newMask = 0;
      for (int i = 0; i < 7 && i < (int)days.size(); i++) {
        if (days[i].as<bool>()) newMask |= (1 << i);
      }
      schedDaysMask = newMask;
    }
  }

  saveConfig();
  pushConfig();
  if (!systemEnabled) pendingStopNow = true; // harmless no-op if the pump isn't running
  return true;
}

// Single place that changes systemEnabled, so every control path (BLE
// command, HTTP /system/on|off, a future app) enforces the same rule:
// disabling it force-stops the pump via the exact same async path STOP_NOW
// already uses, instead of a second, separate stop mechanism.
void setSystemEnabled(bool enabled) {
  systemEnabled = enabled;
  saveConfig();
  pushConfig();
  if (!systemEnabled) pendingStopNow = true;
  logLine(String("[system] systemEnabled=") + String(systemEnabled));
}

// Shared by both the BLE COMMAND characteristic and HTTP POST /api/command.
void applyCommand(const String &cmd) {
  if (cmd == "WATER_NOW") {
    pendingWaterNow = true;
  } else if (cmd == "STOP_NOW") {
    pendingStopNow = true;
  } else if (cmd == "RESET_DEFAULTS") {
    pendingReset = true;
  } else if (cmd == "REFRESH") {
    pendingRefresh = true;
  } else if (cmd == "SYSTEM_ON") {
    setSystemEnabled(true);
  } else if (cmd == "SYSTEM_OFF") {
    setSystemEnabled(false);
  } else if (cmd == "WIFI_RESET") {
    pendingWifiReset = true;
  }
}

// Shared by both the BLE TIME characteristic and HTTP POST /api/time.
void applyTimeSync(const String &value) {
  unsigned long epoch = strtoul(value.c_str(), nullptr, 10);
  if (epoch > 0) {
    timeSyncEpoch = epoch;
    timeSyncMillis = millis();
    timeSynced = true;
  }
}

class ConfigCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    applyConfigJson(c->getValue().c_str());
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    applyCommand(c->getValue().c_str());
  }
};

class TimeCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    applyTimeSync(c->getValue().c_str());
  }
};

// ---------- Sensors ----------
int readMoisturePercent() {
  int raw = analogRead(SOIL_PIN);

  // Pick whichever of these matches your sensor's calibration direction (see the
  // SOIL_DRY/SOIL_WET comment above). Only one should be uncommented at a time.
  int percent = map(raw, SOIL_DRY, SOIL_WET, 0, 100);   // wet reading LOWER than dry
  // int percent = map(raw, SOIL_WET, SOIL_DRY, 100, 0); // wet reading HIGHER than dry (inverted)

  return constrain(percent, 0, 100);
}

bool readDHTWithRetry(float &temp, float &hum, int attempts = 3) {
  for (int i = 0; i < attempts; i++) {
    temp = dht.readTemperature();
    hum = dht.readHumidity();
    if (!isnan(temp) && !isnan(hum)) return true;
    delay(250);
  }
  return false;
}

// ---------- Clock (phone-synced, no RTC on board) ----------
unsigned long currentLocalEpoch() {
  return timeSyncEpoch + (millis() - timeSyncMillis) / 1000UL;
}

int dayOfWeekFor(unsigned long epoch) {
  // Jan 1 1970 was a Thursday; Sunday = 0 ... Saturday = 6.
  return (int)(((epoch / 86400UL) + 4) % 7);
}

// ---------- Pump (non-blocking state machine, driven by updatePump() in loop()) ----------
void startWatering(bool manual) {
  if (pumpActive) return; // already running — a manual tap or an auto/schedule
                          // trigger during an existing run is just a no-op

  unsigned long now = millis();
  if (!manual && (now - lastWaterMillis < minWaterIntervalMs)) {
    logLine("[auto-water] blocked by cooldown, ms remaining=" + String(minWaterIntervalMs - (now - lastWaterMillis)));
    return; // auto watering (condition or schedule) is on cooldown, ignore for now
  }

  pumpActive = true;
  pumpRequestStop = false;
  pumpStartMillis = now;
  pumpPhaseStartMillis = now;
  // Same user-configured run length caps both modes now — "moisture" mode
  // just adds an earlier stop (soilWetTarget, checked in updatePump()) on
  // top of it, instead of ignoring pumpDurationMs and always running to a
  // fixed internal cap regardless of what's configured.
  pumpMaxRunMs = pumpDurationMs;
  pumpRelayOn = true;
  pumpOn = true;
  digitalWrite(RELAY_PIN, HIGH); // adjust to LOW if your relay is active-low

  publishPumpStatus(); // instant UI feedback instead of waiting for the next periodic publish
}

void stopWatering() {
  digitalWrite(RELAY_PIN, LOW);
  pumpRelayOn = false;
  pumpActive = false;
  pumpOn = false;
  lastWaterMillis = millis();

  int soilAfter = readMoisturePercent();
  recordWateringEvent(soilAfter);

  if (deviceConnected) {
    eventChar->setValue("WATERED");
    eventChar->notify();
  }
  publishPumpStatus(); // instant UI feedback instead of waiting for the next periodic publish
}

// Advances the pump's on/off pulsing by one step. Pulses (rather than one
// continuous run) so soil readings between bursts reflect water that's
// actually reached the sensor. Whatever the mode, soilMaxPercent is a
// universal cutoff: watering always stops once soil is at/above it, even if
// there's time (or moisture-target headroom) left. Called every loop()
// iteration so an emergency stop or a sensor publish can happen mid-watering
// instead of waiting for one blocking call to finish.
void updatePump() {
  if (!pumpActive) return;

  if (pumpRequestStop) {
    stopWatering();
    return;
  }

  unsigned long now = millis();
  unsigned long totalElapsed = now - pumpStartMillis;
  if (totalElapsed >= pumpMaxRunMs) {
    stopWatering();
    return;
  }

  unsigned long phaseElapsed = now - pumpPhaseStartMillis;
  unsigned long remaining = pumpMaxRunMs - totalElapsed;

  if (pumpRelayOn) {
    unsigned long pulse = (PUMP_PULSE_MS < remaining) ? PUMP_PULSE_MS : remaining;
    if (phaseElapsed < pulse) return;

    digitalWrite(RELAY_PIN, LOW);
    pumpRelayOn = false;
    pumpPhaseStartMillis = now;
    // Soil is checked at the end of the settle phase below, not here — right
    // at the instant the relay switches off is exactly when its coil/motor
    // can put a noise spike on a soil sensor sharing the same supply, which
    // was previously read immediately and could misread as "soil is wet"
    // and stop the pump after a single ~1.5s pulse.
  } else {
    unsigned long settle = (PUMP_SETTLE_MS < remaining) ? PUMP_SETTLE_MS : remaining;
    if (phaseElapsed < settle) return;

    int soil = readMoisturePercent();
    if (soil >= soilMaxPercent || (pumpMode == "moisture" && soil >= soilWetTarget)) {
      stopWatering();
      return;
    }

    digitalWrite(RELAY_PIN, HIGH);
    pumpRelayOn = true;
    pumpPhaseStartMillis = now;
  }
}

// ---------- Main sensor + decision cycle ----------
String buildSensorJson(int moisture) {
  StaticJsonDocument<128> doc;
  doc["temp"] = lastTemp;
  doc["hum"] = lastHum;
  doc["soil"] = moisture;
  doc["pump"] = pumpOn;
  String out;
  serializeJson(doc, out);
  return out;
}

void notifySensor(int moisture) {
  String out = buildSensorJson(moisture);

  if (deviceConnected) {
    sensorChar->setValue(out.c_str());
    sensorChar->notify();
  }
  logLine(out);
}

// Cheap pump-state-only push (no DHT re-read) for instant UI feedback right
// when watering starts/stops, without waiting for the next periodic publish.
void publishPumpStatus() {
  notifySensor(readMoisturePercent());
}

void checkAutoWater(int moisture) {
  bool tempOk = !isnan(lastTemp) && lastTemp >= tempMin && lastTemp <= tempMax;
  bool humOk = !isnan(lastHum) && lastHum >= humMin && lastHum <= humMax;
  bool soilDry = moisture < soilThreshold;

  String line = "[auto-water] temp=" + String(lastTemp)
    + " (need " + String(tempMin) + "-" + String(tempMax) + ", ok=" + String(tempOk)
    + ") hum=" + String(lastHum)
    + " (need " + String(humMin) + "-" + String(humMax) + ", ok=" + String(humOk)
    + ") soil=" + String(moisture)
    + " (need <" + String(soilThreshold) + ", dry=" + String(soilDry)
    + ") pumpActive=" + String(pumpActive);
  logLine(line);

  if (!systemEnabled || pumpActive) return;
  if (tempOk && humOk && soilDry) startWatering(false);
}

void publishSensorData() {
  bool ok = readDHTWithRetry(lastTemp, lastHum);
  if (!ok) {
    logLine("DHT read failed after retries, using last known values.");
  }
  int moisture = readMoisturePercent();
  notifySensor(moisture);
  checkAutoWater(moisture);
}

// ---------- Scheduled watering ----------
void checkSchedule() {
  if (!systemEnabled || !timeSynced || !schedEnabled || pumpActive) return;

  unsigned long epoch = currentLocalEpoch();
  int dow = dayOfWeekFor(epoch);
  if (!((schedDaysMask >> dow) & 1)) return; // today is turned off

  unsigned long secOfDay = epoch % 86400UL;
  int hour = secOfDay / 3600;
  int minute = (secOfDay % 3600) / 60;

  if (schedMode == "interval") {
    unsigned long epochHour = epoch / 3600UL;
    int intervalHours = schedIntervalHours > 0 ? schedIntervalHours : 1;
    if (minute == 0 && (epochHour % intervalHours) == 0 && epochHour != lastSchedHourFired) {
      lastSchedHourFired = epochHour;
      startWatering(false);
    }
  } else { // "daily"
    unsigned long daysSinceEpoch = epoch / 86400UL;
    if (hour == schedHour && minute == schedMinute && daysSinceEpoch != lastSchedDayFired) {
      lastSchedDayFired = daysSinceEpoch;
      startWatering(false);
    }
  }
}

// ---------- WiFi HTTP API (mirrors the BLE service for same-network access) ----------
void sendCorsHeaders() {
  // Lets the dashboard (opened as a local file, origin "null") fetch() this
  // device across origins — there's no user data at stake here beyond plant
  // watering settings, so a wildcard is fine for a single-user home device.
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleCorsPreflight() {
  sendCorsHeaders();
  webServer.send(204);
}

void handleApiSensorGet() {
  sendCorsHeaders();
  webServer.send(200, "application/json", buildSensorJson(readMoisturePercent()));
}

void handleApiConfigGet() {
  sendCorsHeaders();
  webServer.send(200, "application/json", buildConfigJson());
}

void handleApiConfigPost() {
  sendCorsHeaders();
  bool ok = applyConfigJson(webServer.arg("plain"));
  webServer.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void handleApiCommandPost() {
  sendCorsHeaders();
  String body = webServer.arg("plain");
  body.trim();
  // Accepts either a bare command string body or {"cmd":"WATER_NOW"} JSON.
  if (body.length() > 0 && body[0] == '{') {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok && doc.containsKey("cmd")) {
      applyCommand(doc["cmd"].as<String>());
    }
  } else {
    applyCommand(body);
  }
  webServer.send(200, "application/json", "{\"ok\":true}");
}

void handleApiTimePost() {
  sendCorsHeaders();
  applyTimeSync(webServer.arg("plain"));
  webServer.send(200, "application/json", "{\"ok\":true}");
}

void handleApiHistoryGet() {
  sendCorsHeaders();
  webServer.send(200, "application/json", buildHistoryJson());
}

void handleApiLogGet() {
  sendCorsHeaders();
  uint32_t since = 0;
  if (webServer.hasArg("since")) since = strtoul(webServer.arg("since").c_str(), nullptr, 10);

  StaticJsonDocument<3072> doc;
  JsonArray arr = doc.to<JsonArray>();
  int oldestIndex = (logBufferCount < LOG_BUFFER_CAPACITY) ? 0 : logBufferHead;
  for (int i = 0; i < logBufferCount; i++) {
    int idx = (oldestIndex + i) % LOG_BUFFER_CAPACITY;
    if (logBufferSeq[idx] > since) {
      JsonObject o = arr.createNestedObject();
      o["seq"] = logBufferSeq[idx];
      o["text"] = logBuffer[idx];
    }
  }
  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// Minimal HTML config form served only while provisioningActive — normal
// operation (control AP and/or STA connected) never reaches this.
String buildProvisioningPage() {
  String body = "<!DOCTYPE html><html><head><meta name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"><title>SmartGrow Setup</title></head><body>";

  if (provState == PROV_CONNECTING) {
    body += "<meta http-equiv=\"refresh\" content=\"2;url=/\">";
    body += "<h1>Connecting...</h1><p>Attempting to join \"" + pendingStaSsid + "\".</p>";
  } else if (provState == PROV_FAILED) {
    body += "<h1>Connection Failed</h1><p>" + provisioningError + "</p>";
    body += "<form method=\"POST\" action=\"/connect\">"
      "SSID:<br><input name=\"ssid\" value=\"" + pendingStaSsid + "\"><br>"
      "Password:<br><input name=\"password\" type=\"password\"><br><br>"
      "<input type=\"submit\" value=\"Try Again\"></form>";
  } else {
    body += "<h1>SmartGrow Wi-Fi Setup</h1>"
      "<form method=\"POST\" action=\"/connect\">"
      "SSID:<br><input name=\"ssid\"><br>"
      "Password:<br><input name=\"password\" type=\"password\"><br><br>"
      "<input type=\"submit\" value=\"Connect\"></form>";
  }

  body += "</body></html>";
  return body;
}

void handleRoot() {
  sendCorsHeaders();
  if (provisioningActive) {
    webServer.send(200, "text/html", buildProvisioningPage());
    return;
  }
  webServer.send(200, "text/plain",
    "PlantWaterer is running. Open the dashboard HTML file and use \"Connect via WiFi\" "
    "with host " WIFI_HOSTNAME ".local (or this device's IP address).");
}

// STA connect itself is async (WiFi.begin() returns immediately); the actual
// result is picked up non-blockingly by updateProvisioning() in loop(), so
// this handler never stalls the web server.
void handleProvisionConnectPost() {
  sendCorsHeaders();
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("password");
  ssid.trim();

  if (ssid.length() == 0) {
    provState = PROV_FAILED;
    provisioningError = "Please enter a network name.";
  } else {
    pendingStaSsid = ssid;
    pendingStaPass = pass;
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(ssid.c_str(), pass.c_str());
    provState = PROV_CONNECTING;
    staConnectStartMillis = millis();
  }

  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleWifiResetPost() {
  sendCorsHeaders();
  applyCommand("WIFI_RESET");
  webServer.send(200, "application/json", "{\"ok\":true}");
}

// Captive-portal catch-all: any unknown path/host hit while provisioning is
// active gets bounced to the setup page instead of a bare 404, so most OSes
// offer to open it on their own without depending on that behavior.
void handleNotFound() {
  if (provisioningActive) {
    webServer.sendHeader("Location", "http://192.168.4.1/");
    webServer.send(302, "text/plain", "");
    return;
  }
  sendCorsHeaders();
  webServer.send(404, "application/json", "{\"error\":\"not found\"}");
}

// Minimal status/control surface for simple external clients (e.g. a plain
// script or a future app) that don't need the full /api/* schema above.
// Backed by the exact same state and functions as everything else — no
// second pump/system implementation.
String buildStatusJson() {
  StaticJsonDocument<192> doc;
  doc["systemEnabled"] = systemEnabled;
  doc["pump"] = pumpOn;
  doc["temp"] = lastTemp;
  doc["hum"] = lastHum;
  doc["soil"] = readMoisturePercent();
  String out;
  serializeJson(doc, out);
  return out;
}

void handleStatusGet() {
  sendCorsHeaders();
  webServer.send(200, "application/json", buildStatusJson());
}

void handleSystemOnPost() {
  sendCorsHeaders();
  setSystemEnabled(true);
  webServer.send(200, "application/json", buildStatusJson());
}

void handleSystemOffPost() {
  sendCorsHeaders();
  setSystemEnabled(false);
  webServer.send(200, "application/json", buildStatusJson());
}

void handlePumpOnPost() {
  sendCorsHeaders();
  if (!systemEnabled) {
    webServer.send(403, "application/json", "{\"error\":\"system disabled\"}");
    return;
  }
  applyCommand("WATER_NOW");
  webServer.send(200, "application/json", buildStatusJson());
}

void handlePumpOffPost() {
  sendCorsHeaders();
  applyCommand("STOP_NOW");
  webServer.send(200, "application/json", buildStatusJson());
}

void setupWebServer() {
  webServer.on("/", HTTP_GET, handleRoot);

  webServer.on("/status", HTTP_GET, handleStatusGet);
  webServer.on("/status", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/system/on", HTTP_POST, handleSystemOnPost);
  webServer.on("/system/on", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/system/off", HTTP_POST, handleSystemOffPost);
  webServer.on("/system/off", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/pump/on", HTTP_POST, handlePumpOnPost);
  webServer.on("/pump/on", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/pump/off", HTTP_POST, handlePumpOffPost);
  webServer.on("/pump/off", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/api/sensor", HTTP_GET, handleApiSensorGet);
  webServer.on("/api/sensor", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/api/config", HTTP_GET, handleApiConfigGet);
  webServer.on("/api/config", HTTP_POST, handleApiConfigPost);
  webServer.on("/api/config", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/api/command", HTTP_POST, handleApiCommandPost);
  webServer.on("/api/command", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/api/time", HTTP_POST, handleApiTimePost);
  webServer.on("/api/time", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/api/history", HTTP_GET, handleApiHistoryGet);
  webServer.on("/api/history", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/api/log", HTTP_GET, handleApiLogGet);
  webServer.on("/api/log", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/connect", HTTP_POST, handleProvisionConnectPost);
  webServer.on("/connect", HTTP_OPTIONS, handleCorsPreflight);

  webServer.on("/wifi/reset", HTTP_POST, handleWifiResetPost);
  webServer.on("/wifi/reset", HTTP_OPTIONS, handleCorsPreflight);

  webServer.onNotFound(handleNotFound);
}

void startControlAP() {
  // No router/internet needed, matches the pre-set 192.168.4.1 phones
  // connect to directly.
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("WiFi AP: SSID='" AP_SSID "' IP=");
  Serial.println(WiFi.softAPIP());
}

void startProvisioningAP() {
  // Same fixed address as the control AP — only one is ever broadcasting.
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(PROV_AP_SSID, PROV_AP_PASSWORD);
  dnsServer.start(53, "*", apIP); // answers every DNS query with our IP (captive portal)
  provisioningActive = true;
  provState = PROV_IDLE;
  Serial.print("WiFi provisioning AP: SSID='" PROV_AP_SSID "' IP=");
  Serial.println(WiFi.softAPIP());
}

void startMdns() {
  if (MDNS.begin(WIFI_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS ready: connect the dashboard to http://" WIFI_HOSTNAME ".local/");
  } else {
    Serial.println("mDNS failed to start — connect using the STA IP address above instead.");
  }
}

// Boot-time-only connect (unchanged 15s blocking retry from PR #9) — used
// when a STA network is already saved, so there's nothing to provision.
void connectStaBlocking(const String &ssid, const String &pass) {
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("WiFi STA: connecting to '" + ssid + "'");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_CONNECT_TIMEOUT_MS) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi STA connected. IP address: ");
    Serial.println(WiFi.localIP());
    startMdns();
  } else {
    Serial.println("WiFi STA: connection failed after 15s — AP mode is still available.");
  }
}

void setupWifi() {
  WiFi.mode(WIFI_AP_STA);
  loadWifiCreds();

  if (staSsidSaved.length() == 0) {
    startProvisioningAP();
  } else {
    startControlAP();
    connectStaBlocking(staSsidSaved, staPassSaved);
  }

  setupWebServer();
  webServer.begin();
  wifiReady = true;
}

// Non-blocking counterpart to connectStaBlocking(): picks up the result of
// the WiFi.begin() kicked off by handleProvisionConnectPost() a bit at a
// time from loop(), instead of stalling the web server for up to 15s.
void updateProvisioning() {
  if (!provisioningActive) return;
  dnsServer.processNextRequest();

  if (provState != PROV_CONNECTING) return;

  if (WiFi.status() == WL_CONNECTED) {
    saveWifiCreds(pendingStaSsid, pendingStaPass);
    staSsidSaved = pendingStaSsid;
    staPassSaved = pendingStaPass;
    dnsServer.stop();
    provisioningActive = false;
    provState = PROV_IDLE;
    startControlAP();
    startMdns();
    logLine("[wifi] provisioning succeeded, joined '" + staSsidSaved + "'");
  } else if (millis() - staConnectStartMillis > STA_CONNECT_TIMEOUT_MS) {
    WiFi.disconnect();
    provState = PROV_FAILED;
    provisioningError = "Unable to connect to the configured Wi-Fi network. Please check the SSID and password.";
    logLine("[wifi] provisioning attempt failed for '" + pendingStaSsid + "'");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  dht.begin();

  loadConfig();
  loadHistory();

  // "Never watered yet" sentinel: deliberately wraps below 0 so that on
  // boot, millis() - lastWaterMillis is already >= minWaterIntervalMs. A
  // plain 0 here would make a fresh boot look like "watered at time zero",
  // silently blocking auto-watering until the cooldown elapses even when
  // every condition is already met. Computed here (not as a global
  // initializer) since it depends on the cooldown loaded from config.
  lastWaterMillis = 0 - minWaterIntervalMs;

  // Version marker + current config dump — if you don't see this exact line
  // (or the values look wrong) after uploading, the ESP32 is still running
  // an older sketch and needs a fresh upload.
  Serial.println("=== PlantWaterer firmware: range-based auto-water + emergency stop + history sync ===");
  Serial.println("tempMin=" + String(tempMin) + " tempMax=" + String(tempMax)
    + " humMin=" + String(humMin) + " humMax=" + String(humMax)
    + " soilThreshold=" + String(soilThreshold) + " pumpMode=" + pumpMode
    + " soilMaxPercent=" + String(soilMaxPercent)
    + " minWaterIntervalMs=" + String(minWaterIntervalMs)
    + " sensorIntervalMs=" + String(sensorIntervalMs)
    + " systemEnabled=" + String(systemEnabled)
    + " historyCount=" + String(historyCount));

  NimBLEDevice::init(DEVICE_NAME);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService *service = pServer->createService(SERVICE_UUID);

  // NimBLE auto-creates the CCCD (0x2902) descriptor for any characteristic
  // with the NOTIFY property, so no manual addDescriptor() call is needed.
  sensorChar = service->createCharacteristic(
    SENSOR_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  eventChar = service->createCharacteristic(
    EVENT_CHAR_UUID,
    NIMBLE_PROPERTY::NOTIFY
  );

  configChar = service->createCharacteristic(
    CONFIG_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  configChar->setCallbacks(new ConfigCallbacks());

  commandChar = service->createCharacteristic(
    COMMAND_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  commandChar->setCallbacks(new CommandCallbacks());

  timeChar = service->createCharacteristic(
    TIME_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  timeChar->setCallbacks(new TimeCallbacks());

  historyChar = service->createCharacteristic(
    HISTORY_CHAR_UUID,
    NIMBLE_PROPERTY::READ
  );

  logChar = service->createCharacteristic(
    LOG_CHAR_UUID,
    NIMBLE_PROPERTY::NOTIFY
  );

  service->start();
  pushConfig();
  updateHistoryChar(); // so a fresh connection can read persisted history right away

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  // NimBLE 2.x dropped the setScanResponse(bool) toggle and manages scan
  // response data automatically once a service UUID is added.
  NimBLEDevice::startAdvertising();

  Serial.println("BLE advertising started as '" DEVICE_NAME "'");

  setupWifi(); // starts the AP unconditionally; STA only if WIFI_SSID is filled in above
}

void loop() {
  if (wifiReady) webServer.handleClient();
  updateProvisioning();

#if DEBUG_SOIL_RAW
  Serial.println(analogRead(SOIL_PIN));
  delay(200);
#endif

  if (pendingWaterNow) {
    pendingWaterNow = false;
    startWatering(true);
  }
  if (pendingStopNow) {
    pendingStopNow = false;
    if (pumpActive) pumpRequestStop = true;
  }
  if (pendingReset) {
    pendingReset = false;
    resetToDefaults();
  }
  if (pendingRefresh) {
    pendingRefresh = false;
    publishSensorData();
  }
  if (pendingWifiReset) {
    pendingWifiReset = false;
    resetWifiCredentials(); // restarts the ESP32 — nothing after this line runs
  }

  updatePump(); // non-blocking pump pulsing/cutoff check, every tick

  unsigned long now = millis();
  if (now - lastCheck >= sensorIntervalMs) {
    lastCheck = now;
    publishSensorData();
    checkSchedule();
  }
}
