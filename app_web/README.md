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
  (`readings` and `watering_events` object stores), so "Last Watered" and
  the history list persist across page reloads.
- If the BLE connection drops for any reason (`gattserverdisconnected`),
  the UI falls back to the Connect screen automatically.

## UUIDs

Must match `firmware/watering_system/watering_system.ino` exactly. If you
change one side, change the other.

| Characteristic | UUID | Properties | Payload |
|---|---|---|---|
| SENSOR | `...abc0001` | read, notify | `{"temp","hum","soil","pump"}` |
| EVENT | `...abc0002` | notify | `"WATERED"` |
| CONFIG | `...abc0003` | read, write, notify | `{"mode","tempThreshold","soilThreshold"}` |
| COMMAND | `...abc0004` | write | `"WATER_NOW"` \| `"REFRESH"` \| `"RESET_DEFAULTS"` |
