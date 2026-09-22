# Plant Waterer Web Dashboard

A single static HTML file that connects to the ESP32 over **either** the
Web Bluetooth API or its local Wi-Fi HTTP API, shows live sensor data, and
stores watering history locally in IndexedDB. No backend, no build step,
no dependencies.

## Running it

Web Bluetooth requires a "secure context" (HTTPS or `localhost`) and a user
gesture to open the pairing prompt — it can't auto-connect on page load.
The Wi-Fi mode has no such restriction (it's plain `fetch()`), so it works
fine opened as a local `file://` page.

**Desktop Chrome or Edge** — just open the file directly:

```
app_web/index.html
```

(double-click it, or drag it into the browser window)

**Android Chrome, Bluetooth mode** — `file://` pages are unreliable for Web
Bluetooth on Android, so serve the folder instead. Easiest options:

- Host it on GitHub Pages (or any static host) and open the HTTPS URL on
  your phone.
- Or serve it locally and forward it to your phone over USB:
  ```
  npx serve app_web
  adb reverse tcp:3000 tcp:3000   # match whatever port `serve` prints
  ```
  then open `http://localhost:3000` in Chrome on the phone (USB debugging
  must be enabled).

**Wi-Fi mode, any device** — just open `index.html` directly (`file://` is
fine) and use "Connect via WiFi"; no hosting needed.

## How it works

- Four tabs once connected: **Dashboard** (live readings, trend
  sparklines, Refresh/Water Now), **History** (last 50 watering events),
  **Settings** (thresholds, pump behavior, schedule), and **Console** (a
  live mirror of the ESP32's Serial output, for debugging).
- **Bluetooth:** the Connect screen calls `navigator.bluetooth.requestDevice`,
  filtered to the `PlantWaterer` service UUID, and connects to its GATT
  server. Live updates arrive as GATT notifications (SENSOR, EVENT, LOG).
- **Wi-Fi:** "Connect via WiFi" takes a hostname/IP (defaults to
  `plantwaterer.local`) and talks to the device's local HTTP API instead —
  see the firmware README's HTTP API section for every route. Since plain
  HTTP has no push channel, live sensor updates and the Console log are
  polled instead of using notifications, and a "just finished watering"
  history entry is inferred from the pump status flipping true→false
  between polls.
- Either way, the same command/config/history/console logic is shared
  behind a single `usingWifi` flag rather than duplicated per transport.
- Refresh Now / Water Now / Save / Reset send the same commands either way
  — see `firmware/watering_system/README.md` for the exact BLE
  characteristics / HTTP routes and the full config JSON schema (this file
  doesn't duplicate that reference).
- Every sensor reading and every watering event is timestamped with
  `Date.now()` (the ESP32 has no real-time clock) and stored in IndexedDB
  (`readings` and `watering_events` object stores), so "Last Watered", the
  trend sparklines, and the history list all persist across page reloads.
  Settings are also cached in `localStorage` so the page shows your last
  known config immediately on load, before any connection.
- **Clock sync:** the ESP32 has no RTC, so this page pushes the current
  time (epoch seconds, pre-shifted for your timezone) right after
  connecting and periodically after. The device uses that to run the
  watering schedule — if the page hasn't connected in a while, the
  schedule runs off whatever time it last heard.
- **Pump behavior:** "A fixed time has passed" runs the pump for your
  configured Run Time; "Soil reaches a target moisture" also stops early
  if the target is hit, but Run Time still applies as the cap either way.
  A separate Emergency Cutoff percentage always stops the pump immediately
  regardless of mode.
- **Schedule:** runs alongside the temperature/humidity/soil auto-watering
  (either can trigger a watering; they share the same cooldown, so they
  won't double-water). Default is once a day at 5:00 AM, every day —
  configurable to a different time, an every-N-hours interval, and
  specific days on/off.
- Before Save, you get a confirmation prompt; a native "are you sure"
  dialog, not a custom modal.
- If the Bluetooth connection drops for any reason
  (`gattserverdisconnected`), the UI falls back to the Connect screen
  automatically. Wi-Fi mode just stops getting fresh polls until the
  device is reachable again.

## Protocol reference

See **[`firmware/watering_system/README.md`](../firmware/watering_system/README.md)**
for the full BLE UUID table, HTTP API, config JSON schema, and history
format — kept in one place so it can't drift out of sync with this file.
