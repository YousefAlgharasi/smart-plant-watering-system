# Smart Plant Watering System

An ESP32-based automatic plant watering system with a React Native
dashboard app connected over Bluetooth LE.

## Structure

- **`firmware/watering_system/`** — Arduino sketch for the ESP32. Reads a
  DHT22 (temp/humidity) and a soil moisture sensor, drives a relay + pump,
  and exposes everything over BLE (live readings, watering events, and
  configurable watering thresholds).
- **`app/`** — React Native app. Scans for the ESP32 over Bluetooth,
  shows a live dashboard, lets you set default/custom watering thresholds,
  and logs every reading + watering event to a local SQLite database.

## Hardware

- ESP32 DevKit
- DHT22 temperature/humidity sensor
- Soil moisture sensor (HW-080 probe + HW-103 driver board)
- 1-channel 5V relay module
- Mini submersible water pump
- Breadboard power supply module (5V)

## Getting started

1. **Flash the firmware** — open `firmware/watering_system/watering_system.ino`
   in Arduino IDE. Install the **ESP32 board package** (Espressif), and the
   **DHT sensor library** + **Adafruit Unified Sensor** + **ArduinoJson**
   libraries via Library Manager. Select **ESP32 Dev Module** as the board,
   then upload.
2. **Run the app** — see `app/README.md` for full setup (bare React Native
   project, BLE + SQLite dependencies, Android/iOS permissions).

## How it works

The ESP32 runs the watering logic itself (so it keeps working even if your
phone isn't connected): it waters automatically when temperature is above
a threshold **and** soil moisture is below a threshold, with a 10-minute
cooldown between automatic waterings. The app is a window into that: live
readings, manual override ("Water Now"), threshold configuration, and a
local history log.

## License

Personal project — add a license here if you plan to open it up.
