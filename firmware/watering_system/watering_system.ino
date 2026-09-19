#include <DHT.h>
// NimBLE instead of the stock BLEDevice.h (Bluedroid): Bluedroid's GATT server
// is known to fail the connect handshake from Windows Web Bluetooth with a
// generic "Connection failed for unknown reason" error. NimBLE fixes this.
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <ArduinoJson.h>

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

// ---------- BLE UUIDs (must match the app's src/constants/ble.ts) ----------
#define SERVICE_UUID      "12345678-1234-5678-1234-56789abc0000"
#define SENSOR_CHAR_UUID  "12345678-1234-5678-1234-56789abc0001"
#define EVENT_CHAR_UUID   "12345678-1234-5678-1234-56789abc0002"
#define CONFIG_CHAR_UUID  "12345678-1234-5678-1234-56789abc0003"
#define COMMAND_CHAR_UUID "12345678-1234-5678-1234-56789abc0004"
#define TIME_CHAR_UUID    "12345678-1234-5678-1234-56789abc0005"
#define HISTORY_CHAR_UUID "12345678-1234-5678-1234-56789abc0006"
#define DEVICE_NAME       "PlantWaterer"

// On-device watering log so the app can catch up on events that happened
// while it wasn't connected (auto-watering runs with no phone/browser
// attached at all). Ring buffer, persisted to flash after every watering.
#define HISTORY_CAPACITY 10

// ---------- Timing ----------
const unsigned long CHECK_INTERVAL_MS     = 10000;   // how often to read sensors
const unsigned long PUMP_SAFETY_MAX_MS    = 20000;   // hard cap for moisture-feedback mode, no matter what
const unsigned long PUMP_PULSE_MS         = 1500;    // moisture mode: burst length between checks
const unsigned long PUMP_SETTLE_MS        = 800;     // moisture mode: pause after a burst so the reading reflects reality
const unsigned long MIN_WATER_INTERVAL_MS = 600000;  // 10 min cooldown between AUTO waterings (schedule counts as auto)

// ---------- Defaults ----------
const float DEFAULT_TEMP_THRESHOLD  = 28.0; // water only if temp >= this (it's warm enough)
const int   DEFAULT_SOIL_THRESHOLD  = 40;   // water only if moisture % is below this (it's dry)
const int   DEFAULT_SOIL_WET_TARGET = 65;   // moisture-mode pump target: stop once soil reaches this %
const unsigned long DEFAULT_PUMP_DURATION_MS = 20000; // fixed-duration pump mode run length
const int   DEFAULT_SOIL_MAX_PERCENT = 80;  // universal safety cap: stop ANY watering immediately at this %

DHT dht(DHTPIN, DHTTYPE);
Preferences prefs;

NimBLEServer *pServer;
NimBLECharacteristic *sensorChar;
NimBLECharacteristic *eventChar;
NimBLECharacteristic *configChar;
NimBLECharacteristic *commandChar;
NimBLECharacteristic *timeChar;
NimBLECharacteristic *historyChar;

bool deviceConnected = false;

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

String mode = "default";     // "default" or "custom" (temp/soil thresholds)
float tempThreshold = DEFAULT_TEMP_THRESHOLD;
int soilThreshold = DEFAULT_SOIL_THRESHOLD;

String pumpMode = "duration"; // "duration" (fixed burst) or "moisture" (until soil is wet)
int soilWetTarget = DEFAULT_SOIL_WET_TARGET;
unsigned long pumpDurationMs = DEFAULT_PUMP_DURATION_MS;
int soilMaxPercent = DEFAULT_SOIL_MAX_PERCENT; // stop any watering immediately once soil hits this %

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
unsigned long lastWaterMillis = 0;

float lastTemp = NAN;
float lastHum = NAN;
bool pumpOn = false;

// BLE write callbacks run on NimBLE's own task; keep them fast and defer real
// work (especially watering, which can now block for several seconds) to loop().
volatile bool pendingWaterNow = false;
volatile bool pendingReset = false;
volatile bool pendingRefresh = false;

void publishSensorData();
void waterPlant(bool manual);

// ---------- Config persistence ----------
void loadConfig() {
  prefs.begin("watering", true);
  mode = prefs.getString("mode", "default");
  tempThreshold = prefs.getFloat("tempTh", DEFAULT_TEMP_THRESHOLD);
  soilThreshold = prefs.getInt("soilTh", DEFAULT_SOIL_THRESHOLD);
  pumpMode = prefs.getString("pumpMode", "duration");
  soilWetTarget = prefs.getInt("wetTarget", DEFAULT_SOIL_WET_TARGET);
  pumpDurationMs = prefs.getULong("pumpDurMs", DEFAULT_PUMP_DURATION_MS);
  soilMaxPercent = prefs.getInt("soilMax", DEFAULT_SOIL_MAX_PERCENT);
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
  prefs.putString("mode", mode);
  prefs.putFloat("tempTh", tempThreshold);
  prefs.putInt("soilTh", soilThreshold);
  prefs.putString("pumpMode", pumpMode);
  prefs.putInt("wetTarget", soilWetTarget);
  prefs.putULong("pumpDurMs", pumpDurationMs);
  prefs.putInt("soilMax", soilMaxPercent);
  prefs.putBool("schedOn", schedEnabled);
  prefs.putString("schedMode", schedMode);
  prefs.putInt("schedHour", schedHour);
  prefs.putInt("schedMin", schedMinute);
  prefs.putInt("schedIntH", schedIntervalHours);
  prefs.putUChar("schedDays", schedDaysMask);
  prefs.end();
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

void updateHistoryChar() {
  // Oldest-first. When the buffer is full, historyHead already points at the
  // oldest surviving entry (the next slot to be overwritten); otherwise the
  // oldest entry is always at index 0.
  int oldestIndex = (historyCount < HISTORY_CAPACITY) ? 0 : historyHead;

  StaticJsonDocument<1024> doc;
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
  historyChar->setValue(out.c_str());
}

void recordWateringEvent(int soilAfter) {
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

void pushConfig() {
  StaticJsonDocument<768> doc;
  doc["mode"] = mode;
  doc["tempThreshold"] = tempThreshold;
  doc["soilThreshold"] = soilThreshold;
  doc["pumpMode"] = pumpMode;
  doc["soilWetTarget"] = soilWetTarget;
  doc["pumpDurationMs"] = pumpDurationMs;
  doc["soilMaxPercent"] = soilMaxPercent;

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
  configChar->setValue(out.c_str());
  if (deviceConnected) configChar->notify();
}

void resetToDefaults() {
  mode = "default";
  tempThreshold = DEFAULT_TEMP_THRESHOLD;
  soilThreshold = DEFAULT_SOIL_THRESHOLD;
  pumpMode = "duration";
  soilWetTarget = DEFAULT_SOIL_WET_TARGET;
  pumpDurationMs = DEFAULT_PUMP_DURATION_MS;
  soilMaxPercent = DEFAULT_SOIL_MAX_PERCENT;
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

class ConfigCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    String value = c->getValue().c_str();
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, value) == DeserializationError::Ok) {
      if (doc.containsKey("mode")) mode = doc["mode"].as<String>();
      if (doc.containsKey("tempThreshold")) tempThreshold = doc["tempThreshold"];
      if (doc.containsKey("soilThreshold")) soilThreshold = doc["soilThreshold"];
      if (doc.containsKey("pumpMode")) pumpMode = doc["pumpMode"].as<String>();
      if (doc.containsKey("soilWetTarget")) soilWetTarget = doc["soilWetTarget"];
      if (doc.containsKey("pumpDurationMs")) pumpDurationMs = doc["pumpDurationMs"];
      if (doc.containsKey("soilMaxPercent")) soilMaxPercent = doc["soilMaxPercent"];

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
    }
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    String cmd = c->getValue().c_str();
    if (cmd == "WATER_NOW") {
      pendingWaterNow = true;
    } else if (cmd == "RESET_DEFAULTS") {
      pendingReset = true;
    } else if (cmd == "REFRESH") {
      pendingRefresh = true;
    }
  }
};

class TimeCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    String value = c->getValue().c_str();
    unsigned long epoch = strtoul(value.c_str(), nullptr, 10);
    if (epoch > 0) {
      timeSyncEpoch = epoch;
      timeSyncMillis = millis();
      timeSynced = true;
    }
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

// ---------- Pump ----------
void waterPlant(bool manual) {
  unsigned long now = millis();
  if (!manual && (now - lastWaterMillis < MIN_WATER_INTERVAL_MS)) {
    return; // auto watering (condition or schedule) is on cooldown, ignore for now
  }

  pumpOn = true;
  unsigned long startMillis = millis();
  // "duration" mode runs up to the configured length; "moisture" mode runs up
  // to the hard safety cap instead, since its whole point is to keep going
  // until the target is reached (or the cap saves it from a bad sensor/empty
  // reservoir).
  unsigned long maxRunMs = (pumpMode == "moisture") ? PUMP_SAFETY_MAX_MS : pumpDurationMs;
  int soilAfter = -1;

  // Pulse the pump and re-check moisture between bursts (rather than one
  // continuous run) so the reading reflects water that's actually reached
  // the sensor. Whatever the mode, soilMaxPercent is a universal cutoff:
  // watering always stops immediately once soil is at/above it, even if
  // there's time (or moisture-target headroom) left.
  while (true) {
    unsigned long elapsed = millis() - startMillis;
    if (elapsed >= maxRunMs) break;

    unsigned long remaining = maxRunMs - elapsed;
    unsigned long pulse = (PUMP_PULSE_MS < remaining) ? PUMP_PULSE_MS : remaining;

    digitalWrite(RELAY_PIN, HIGH); // adjust to LOW if your relay is active-low
    delay(pulse);
    digitalWrite(RELAY_PIN, LOW);

    soilAfter = readMoisturePercent();
    if (soilAfter >= soilMaxPercent) break;
    if (pumpMode == "moisture" && soilAfter >= soilWetTarget) break;

    elapsed = millis() - startMillis;
    if (elapsed >= maxRunMs) break;
    remaining = maxRunMs - elapsed;
    unsigned long settle = (PUMP_SETTLE_MS < remaining) ? PUMP_SETTLE_MS : remaining;
    delay(settle);
  }

  pumpOn = false;
  lastWaterMillis = now;

  if (soilAfter < 0) soilAfter = readMoisturePercent();
  recordWateringEvent(soilAfter);

  if (deviceConnected) {
    eventChar->setValue("WATERED");
    eventChar->notify();
  }
}

// ---------- Main sensor + decision cycle ----------
void publishSensorData() {
  bool ok = readDHTWithRetry(lastTemp, lastHum);
  if (!ok) {
    Serial.println("DHT read failed after retries, using last known values.");
  }
  int moisture = readMoisturePercent();

  StaticJsonDocument<128> doc;
  doc["temp"] = lastTemp;
  doc["hum"] = lastHum;
  doc["soil"] = moisture;
  doc["pump"] = pumpOn;
  String out;
  serializeJson(doc, out);

  Serial.println(out);

  if (deviceConnected) {
    sensorChar->setValue(out.c_str());
    sensorChar->notify();
  }

  // Auto-watering: warm enough AND soil is dry
  if (!isnan(lastTemp) && lastTemp >= tempThreshold && moisture < soilThreshold) {
    waterPlant(false);
  }
}

// ---------- Scheduled watering ----------
void checkSchedule() {
  if (!timeSynced || !schedEnabled) return;

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
      waterPlant(false);
    }
  } else { // "daily"
    unsigned long daysSinceEpoch = epoch / 86400UL;
    if (hour == schedHour && minute == schedMinute && daysSinceEpoch != lastSchedDayFired) {
      lastSchedDayFired = daysSinceEpoch;
      waterPlant(false);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  dht.begin();

  loadConfig();
  loadHistory();

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

  service->start();
  pushConfig();
  updateHistoryChar(); // so a fresh connection can read persisted history right away

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  // NimBLE 2.x dropped the setScanResponse(bool) toggle and manages scan
  // response data automatically once a service UUID is added.
  NimBLEDevice::startAdvertising();

  Serial.println("BLE advertising started as '" DEVICE_NAME "'");
}

void loop() {
#if DEBUG_SOIL_RAW
  Serial.println(analogRead(SOIL_PIN));
  delay(200);
#endif

  if (pendingWaterNow) {
    pendingWaterNow = false;
    waterPlant(true);
  }
  if (pendingReset) {
    pendingReset = false;
    resetToDefaults();
  }
  if (pendingRefresh) {
    pendingRefresh = false;
    publishSensorData();
  }

  unsigned long now = millis();
  if (now - lastCheck >= CHECK_INTERVAL_MS) {
    lastCheck = now;
    publishSensorData();
    checkSchedule();
  }
}
