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

// Calibrate after testing: dip sensor in dry soil and wet soil, read raw values via Serial
#define SOIL_DRY 3000
#define SOIL_WET 1200

// ---------- BLE UUIDs (must match the app's src/constants/ble.ts) ----------
#define SERVICE_UUID      "12345678-1234-5678-1234-56789abc0000"
#define SENSOR_CHAR_UUID  "12345678-1234-5678-1234-56789abc0001"
#define EVENT_CHAR_UUID   "12345678-1234-5678-1234-56789abc0002"
#define CONFIG_CHAR_UUID  "12345678-1234-5678-1234-56789abc0003"
#define COMMAND_CHAR_UUID "12345678-1234-5678-1234-56789abc0004"
#define DEVICE_NAME       "PlantWaterer"

// ---------- Timing ----------
const unsigned long CHECK_INTERVAL_MS   = 10000;   // how often to read sensors
const unsigned long PUMP_RUN_MS         = 3000;    // how long the pump runs
const unsigned long MIN_WATER_INTERVAL_MS = 600000; // 10 min cooldown between AUTO waterings

// ---------- Defaults ----------
const float DEFAULT_TEMP_THRESHOLD = 28.0; // water only if temp >= this (it's warm enough)
const int   DEFAULT_SOIL_THRESHOLD = 40;   // water only if moisture % is below this (it's dry)

DHT dht(DHTPIN, DHTTYPE);
Preferences prefs;

NimBLEServer *pServer;
NimBLECharacteristic *sensorChar;
NimBLECharacteristic *eventChar;
NimBLECharacteristic *configChar;
NimBLECharacteristic *commandChar;

bool deviceConnected = false;

String mode = "default";     // "default" or "custom"
float tempThreshold = DEFAULT_TEMP_THRESHOLD;
int soilThreshold = DEFAULT_SOIL_THRESHOLD;

unsigned long lastCheck = 0;
unsigned long lastWaterMillis = 0;

float lastTemp = NAN;
float lastHum = NAN;
bool pumpOn = false;

void publishSensorData();
void waterPlant(bool manual);

// ---------- Config persistence ----------
void loadConfig() {
  prefs.begin("watering", true);
  mode = prefs.getString("mode", "default");
  tempThreshold = prefs.getFloat("tempTh", DEFAULT_TEMP_THRESHOLD);
  soilThreshold = prefs.getInt("soilTh", DEFAULT_SOIL_THRESHOLD);
  prefs.end();
}

void saveConfig() {
  prefs.begin("watering", false);
  prefs.putString("mode", mode);
  prefs.putFloat("tempTh", tempThreshold);
  prefs.putInt("soilTh", soilThreshold);
  prefs.end();
}

void pushConfig() {
  StaticJsonDocument<128> doc;
  doc["mode"] = mode;
  doc["tempThreshold"] = tempThreshold;
  doc["soilThreshold"] = soilThreshold;
  String out;
  serializeJson(doc, out);
  configChar->setValue(out.c_str());
  if (deviceConnected) configChar->notify();
}

void resetToDefaults() {
  mode = "default";
  tempThreshold = DEFAULT_TEMP_THRESHOLD;
  soilThreshold = DEFAULT_SOIL_THRESHOLD;
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
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, value) == DeserializationError::Ok) {
      if (doc.containsKey("mode")) mode = doc["mode"].as<String>();
      if (doc.containsKey("tempThreshold")) tempThreshold = doc["tempThreshold"];
      if (doc.containsKey("soilThreshold")) soilThreshold = doc["soilThreshold"];
      saveConfig();
      pushConfig();
    }
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    String cmd = c->getValue().c_str();
    if (cmd == "WATER_NOW") {
      waterPlant(true);
    } else if (cmd == "RESET_DEFAULTS") {
      resetToDefaults();
    } else if (cmd == "REFRESH") {
      publishSensorData();
    }
  }
};

// ---------- Sensors ----------
int readMoisturePercent() {
  int raw = analogRead(SOIL_PIN);
  int percent = map(raw, SOIL_DRY, SOIL_WET, 0, 100);
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

// ---------- Pump ----------
void waterPlant(bool manual) {
  unsigned long now = millis();
  if (!manual && (now - lastWaterMillis < MIN_WATER_INTERVAL_MS)) {
    return; // auto watering is on cooldown, ignore for now
  }

  pumpOn = true;
  digitalWrite(RELAY_PIN, HIGH); // adjust to LOW if your relay is active-low
  delay(PUMP_RUN_MS);
  digitalWrite(RELAY_PIN, LOW);
  pumpOn = false;
  lastWaterMillis = now;

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

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  dht.begin();

  loadConfig();

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

  service->start();
  pushConfig();

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  // NimBLE 2.x dropped the setScanResponse(bool) toggle and manages scan
  // response data automatically once a service UUID is added.
  NimBLEDevice::startAdvertising();

  Serial.println("BLE advertising started as '" DEVICE_NAME "'");
}

void loop() {
  unsigned long now = millis();
  if (now - lastCheck >= CHECK_INTERVAL_MS) {
    lastCheck = now;
    publishSensorData();
  }
}
