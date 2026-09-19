# Plant Waterer Web Dashboard

A single static HTML file that connects to the ESP32 over the Web Bluetooth
API, shows live sensor data, and stores watering history locally in
IndexedDB. No backend, no build step, no dependencies.

## Running it

Web Bluetooth requires a "secure context" (HTTPS or `localhost`) and a user
gesture to open the pairing prompt — it can't auto-connect on page load.

**Desktop Chrome or Edge** — just open the file directly:

```
app_web/index.html
```

(double-click it, or drag it into the browser window)

**Android Chrome** — `file://` pages are unreliable for Web Bluetooth on
Android, so serve the folder instead. Easiest options:

- Host it on GitHub Pages (or any static host) and open the HTTPS URL on
  your phone.
- Or serve it locally and forward it to your phone over USB:
  ```
  npx serve app_web
  adb reverse tcp:3000 tcp:3000   # match whatever port `serve` prints
  ```
  then open `http://localhost:3000` in Chrome on the phone (USB debugging
  must be enabled).

## How it works

- The page is three tabs once connected: **Dashboard** (live readings, trend
  sparklines, Refresh/Water Now), **History** (last 50 watering events), and
  **Settings** (thresholds, pump mode, schedule).
- The Connect screen calls `navigator.bluetooth.requestDevice`, filtered to
  the `PlantWaterer` service UUID, and connects to its GATT server.
- Once connected, the dashboard subscribes to notifications on SENSOR (live
  readings, pushed by the ESP32 every ~10s) and EVENT (fires once per
  watering). CONFIG is read once on connect and re-read whenever the device
  pushes a change (e.g. after a reset).
- Refresh Now / Water Now / Save / Reset write plain-string or JSON commands
  to the COMMAND/CONFIG characteristics — see the UUID table below.
- Every sensor reading and every watering event is timestamped with
  `Date.now()` (the ESP32 has no real-time clock) and stored in IndexedDB
  (`readings` and `watering_events` object stores), so "Last Watered", the
  trend sparklines, and the history list all persist across page reloads.
- **Clock sync:** the ESP32 has no RTC, so this page pushes the current time
  to the TIME characteristic (as epoch seconds, pre-shifted for your
  timezone) right after connecting and again on every 5-minute auto-refresh.
  The device uses that to run the watering schedule. If the page hasn't
  connected in a while, the schedule runs off whatever time it last heard —
  reconnect periodically to keep it accurate.
- **Pump mode:** "Fixed duration" runs the pump for a flat 3 seconds, same as
  before. "Until soil is wet" pulses the pump and re-checks moisture between
  bursts until it reaches your target — either way the firmware enforces a
  hard ~20s cap regardless of the setting, so a misreading sensor or an empty
  reservoir can't run the pump indefinitely.
- **Schedule:** runs alongside the existing temperature+soil auto-watering
  (either can trigger a watering; the same 10-minute cooldown applies to
  both, so they won't double-water). Default is once a day at 5:00 AM, every
  day — configurable to a different time, an every-N-hours interval, and
  specific days on/off.
- If the BLE connection drops for any reason (`gattserverdisconnected`),
  the UI falls back to the Connect screen automatically.

## UUIDs

Must match `firmware/watering_system/watering_system.ino` exactly. If you
change one side, change the other.

| Characteristic | UUID | Properties | Payload |
|---|---|---|---|
| SENSOR | `...abc0001` | read, notify | `{"temp","hum","soil","pump"}` |
| EVENT | `...abc0002` | notify | `"WATERED"` |
| CONFIG | `...abc0003` | read, write, notify | `{"mode","tempThreshold","soilThreshold","pumpMode","soilWetTarget","schedule":{"enabled","mode","hour","minute","intervalHours","days"}}` |
| COMMAND | `...abc0004` | write | `"WATER_NOW"` \| `"REFRESH"` \| `"RESET_DEFAULTS"` |
| TIME | `...abc0005` | write | epoch seconds as a decimal string, e.g. `"1758271234"` |
