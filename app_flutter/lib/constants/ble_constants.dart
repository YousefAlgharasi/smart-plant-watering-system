/// BLE identifiers for the ESP32 "PlantWaterer" GATT server.
/// Must match firmware/watering_system/watering_system.ino exactly.
library;

const String deviceName = 'PlantWaterer';

const String serviceUuid = '12345678-1234-5678-1234-56789abc0000';

/// Read + notify. JSON: {"temp": float, "hum": float, "soil": int, "pump": bool}
const String sensorCharUuid = '12345678-1234-5678-1234-56789abc0001';

/// Notify only. Plain string "WATERED", sent once per pump run.
const String eventCharUuid = '12345678-1234-5678-1234-56789abc0002';

/// Read + write + notify. JSON: {"mode": "default"|"custom", "tempThreshold": float, "soilThreshold": int}
const String configCharUuid = '12345678-1234-5678-1234-56789abc0003';

/// Write only. Plain string commands: WATER_NOW, REFRESH, RESET_DEFAULTS.
const String commandCharUuid = '12345678-1234-5678-1234-56789abc0004';

const String cmdWaterNow = 'WATER_NOW';
const String cmdRefresh = 'REFRESH';
const String cmdResetDefaults = 'RESET_DEFAULTS';
const String eventWatered = 'WATERED';
