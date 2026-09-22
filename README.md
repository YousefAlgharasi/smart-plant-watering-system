# Smart Plant Watering System (SmartGrow / PlantWaterer)

An ESP32-based automatic plant watering system. The ESP32 runs all the
watering logic itself — sensors, pump control, scheduling, safety cutoffs —
so it keeps working even with no phone, browser, or network connection at
all. A web dashboard gives you live readings, manual control, history, and
full configuration over either Bluetooth or Wi-Fi.

## Structure

- **`firmware/watering_system/`** — the ESP32 Arduino sketch. This is the
  only thing that actually runs the system; everything else is a client.
  See **[firmware/watering_system/README.md](firmware/watering_system/README.md)**
  for the full technical reference (BLE characteristics, HTTP API, Wi-Fi
  provisioning, pump behavior, config schema, calibration, wiring/power
  notes).
- **`app_web/`** — the maintained dashboard: a static, no-build HTML page
  (Web Bluetooth + a WiFi/HTTP mode). See
  **[app_web/README.md](app_web/README.md)**.
- **`app/`**, **`app_flutter/`** — earlier React Native / Flutter dashboard
  prototypes. They predate most of the current firmware protocol (single
  temperature threshold, no Wi-Fi, no scheduling, no history sync) and are
  not maintained alongside it — use `app_web/` unless you're specifically
  reviving one of these.

## Hardware

- ESP32 DevKit
- DHT22 temperature/humidity sensor
- Soil moisture sensor (resistive/capacitive probe + driver board)
- 1-channel 5V relay module
- Mini submersible water pump

### ⚠️ Power wiring

Never power the ESP32 from USB (laptop) **and** an external power supply
at the same time. Most ESP32 dev boards have no safe circuitry to combine
two power sources — when the pump's relay switches on and pulls a current
spike, the two rails can momentarily fight or drift apart just enough to
glitch/reset the chip. Pick one supply at a time; if you need Serial output
while the pump is powered externally, use a data-only USB connection (or
just watch the dashboard instead of Serial).

If you still see resets/crashes right when the pump switches on with only
one supply connected, add a bulk capacitor (1000µF+) across the pump's
power leads, close to the pump, to absorb its inrush current.

## Getting started

1. **Flash the firmware** — open
   `firmware/watering_system/watering_system.ino` in Arduino IDE. Install
   the **ESP32 board package** (Espressif), and the **DHT sensor library**,
   **Adafruit Unified Sensor**, **ArduinoJson**, and **NimBLE-Arduino**
   libraries via Library Manager. Select **ESP32 Dev Module** as the board,
   then upload. See the firmware README for calibration and Wi-Fi setup.
2. **Connect** — open `app_web/index.html` in Chrome or Edge and either:
   - **Bluetooth:** hit Connect and pick "PlantWaterer" — works out of the
     box, no setup.
   - **Wi-Fi:** connect your phone/laptop to the `SmartGrow-Setup` network
     the ESP32 broadcasts on first boot, enter your home Wi-Fi credentials
     at `http://192.168.4.1`, then connect the dashboard to
     `plantwaterer.local` (or the printed IP) once it joins your network.

## How it works

The ESP32 owns all the watering logic; every control surface (Bluetooth,
the local HTTP API, Wi-Fi provisioning) is just a different door into the
same underlying state — nothing is ever duplicated across transports.

- **Auto-watering** fires when temperature, humidity, and soil moisture are
  all within/below their configured ranges, subject to a configurable
  cooldown between automatic waterings (manual "Water Now" always bypasses
  it).
- **Scheduling** runs independently and can also trigger a watering (daily
  time-of-day, or every N hours), sharing the same cooldown so the two
  mechanisms won't double-water.
- **Pump behavior** is configurable: stop after a fixed run time, or stop
  once soil reaches a target moisture — either way, run time is a hard cap
  you control, and a universal moisture safety cutoff always applies
  regardless of mode.
- **System on/off**: a master switch disables all automatic watering and
  immediately stops the pump if it's running; manual control still works
  through it depending on the switch's state (see the firmware README).
- **Connectivity:** Bluetooth is always on. Wi-Fi runs alongside it — the
  ESP32 can join your home network (STA) while also broadcasting its own
  always-on control network (`SmartGrow`) for router-free access, and
  falls into a one-time setup network (`SmartGrow-Setup`) if no home
  network has been configured yet.

Full protocol details (BLE UUIDs, HTTP endpoints, JSON schemas) live in the
firmware README, not duplicated here.

## License

Personal project — add a license here if you plan to open it up.
