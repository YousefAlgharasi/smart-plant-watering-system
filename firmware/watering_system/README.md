# Firmware — `watering_system.ino`

Single Arduino sketch for the ESP32. It owns all the watering logic and
exposes it over three control surfaces — Bluetooth LE, a local HTTP API,
and Wi-Fi provisioning — all backed by the same shared functions, so no
control path can ever apply settings or commands differently than another.

## Pins

| Pin | Purpose |
|---|---|
| GPIO 16 | DHT22 data |
| GPIO 34 | Soil moisture sensor (analog) |
| GPIO 27 | Relay control (drives the pump) |

Adjust `DHTPIN` / `SOIL_PIN` / `RELAY_PIN` at the top of the file if your
wiring differs. If your relay is active-low, flip the `HIGH`/`LOW` in
`startWatering()` / `stopWatering()` / `setup()`.

## Soil sensor calibration

`SOIL_DRY` / `SOIL_WET` (raw ADC values) must be recalibrated per physical
sensor — they're placeholders. Set `DEBUG_SOIL_RAW` to `1`, open Serial
Monitor, and note the raw `analogRead()` value in dry air and with the
probe in wet soil. Then:

- If your sensor's **wet** reading is **lower** than its **dry** reading
  (typical), use the first `map()` call in `readMoisturePercent()`.
- If it's **inverted** (wet > dry), use the second one instead — only one
  should be uncommented at a time.

Set `DEBUG_SOIL_RAW` back to `0` for normal operation (it blocks `loop()`
with a `delay(200)` while enabled).

## Power / wiring

Never power the ESP32 from USB and an external supply simultaneously —
see the root README's "Power wiring" section. When the pump/relay switches
on, current-hungry loads can sag a shared/marginal supply enough to trip
the ESP32's brownout detector, which shows up as the chip resetting
(garbled/looping Serial output) right when the pump turns on. A bulk
capacitor (1000µF+) across the pump's power leads helps absorb the inrush
spike if a single, adequately-rated supply still isn't enough.

## Connectivity

All three run concurrently; enabling/using one never disables another.

### Bluetooth (NimBLE)

Always on, unconditionally, from boot. Service and characteristic UUIDs
(must match `app_web/index.html`'s `BLE_*` constants):

| Characteristic | UUID suffix | Properties | Payload |
|---|---|---|---|
| SENSOR | `...abc0001` | read, notify | `{"temp","hum","soil","pump"}` |
| EVENT | `...abc0002` | notify | `"WATERED"` (fires once per completed watering) |
| CONFIG | `...abc0003` | read, write, notify | full config JSON — see below |
| COMMAND | `...abc0004` | write | one of the command strings below |
| TIME | `...abc0005` | write | epoch seconds as a decimal string, e.g. `"1758271234"` |
| HISTORY | `...abc0006` | read | JSON array of the last watering events (see below) |
| LOG | `...abc0007` | notify | live mirror of Serial output (debugging only) |

Service UUID: `12345678-1234-5678-1234-56789abc0000`.

NimBLE (not the stock `BLEDevice.h`/Bluedroid) is used specifically because
Bluedroid's GATT server fails the connect handshake from Windows Web
Bluetooth with a generic "Connection failed for unknown reason" error.

### Wi-Fi STA (join your home network)

Optional. Fill in `WIFI_SSID` / `WIFI_PASSWORD` before uploading, **or**
leave them blank and use the provisioning flow below instead — either way
ends up saved to the same place (see Provisioning). Once connected:

- Reachable at `http://plantwaterer.local/` (via mDNS) or by the IP Serial
  prints on boot.
- A 15s connection attempt happens once at boot if credentials are already
  saved; if it fails, the device continues running fine on Bluetooth + the
  control AP (below) — STA is never required.

### Control AP (`SmartGrow`)

Always broadcasting **except** while provisioning is in progress (only one
AP identity can be active on the radio at a time). SSID `SmartGrow`,
password `12345678`, fixed at `192.168.4.1` — connect a phone directly with
no router or internet at all.

### Wi-Fi provisioning (`SmartGrow-Setup`)

On boot, if no STA network has been saved yet (fresh device, or after a
`WIFI_RESET`), the device broadcasts `SmartGrow-Setup` (password
`12345678`) instead of the control AP, and serves a plain SSID/password
form at `http://192.168.4.1/` — no network scanning. A DNS server plus an
HTTP catch-all bounce any request to that page, so most phones offer it as
a captive portal automatically.

Flow:
1. Connect to `SmartGrow-Setup`, open `http://192.168.4.1` (or let the OS
   open it for you).
2. Enter your home network's SSID/password, submit.
3. The device attempts a non-blocking connection (`updateProvisioning()` in
   `loop()`, ~15s timeout) — the setup page shows "Connecting..." and
   auto-refreshes.
4. **Success:** credentials are saved, the radio switches back to the
   normal `SmartGrow` control AP, and mDNS starts.
5. **Failure:** nothing is saved, `SmartGrow-Setup` keeps running (no
   timeout on the AP itself), and the page re-shows the form with an error
   so you can retry.

**Wi-Fi reset:** clears only the saved STA SSID/password (irrigation
config, schedule, and history are untouched) and restarts the device,
which drops it back into provisioning. Trigger it with:
- BLE COMMAND: `"WIFI_RESET"`
- HTTP: `POST /wifi/reset`

## HTTP API

Served on port 80 whenever Wi-Fi is up (control AP, provisioning AP, or
STA — whichever is active). All routes send permissive CORS headers so a
`file://`-opened dashboard can `fetch()` them, and support `OPTIONS`
preflight.

### Full API (mirrors every BLE characteristic 1:1)

| Route | Method | Body / Query | Response |
|---|---|---|---|
| `/api/sensor` | GET | — | `{"temp","hum","soil","pump"}` |
| `/api/config` | GET | — | full config JSON (below) |
| `/api/config` | POST | config JSON (any subset of fields) | `{"ok":true\|false}` |
| `/api/command` | POST | a bare command string, or `{"cmd":"..."}` | `{"ok":true}` |
| `/api/time` | POST | epoch seconds as plain text | `{"ok":true}` |
| `/api/history` | GET | — | JSON array of watering events (below) |
| `/api/log` | GET | `?since=<seq>` (optional) | `[{"seq","text"}, ...]` |

### Minimal status/control surface

For simple external clients (a script, a future app) that don't need the
full schema above — backed by the exact same functions, no separate logic:

| Route | Method | Notes |
|---|---|---|
| `/status` | GET | `{"systemEnabled","pump","temp","hum","soil"}` |
| `/system/on` | POST | re-enables auto-watering |
| `/system/off` | POST | disables auto-watering and immediately stops the pump |
| `/pump/on` | POST | manual start; **403** `{"error":"system disabled"}` if the system is off |
| `/pump/off` | POST | manual stop — always works, regardless of system state |

### Commands

Valid strings for `COMMAND` (BLE) / `/api/command` (HTTP):

| Command | Effect |
|---|---|
| `WATER_NOW` | manual watering (ignores the auto-watering cooldown) |
| `STOP_NOW` | emergency stop — ends an in-progress watering immediately |
| `RESET_DEFAULTS` | resets all config (thresholds, pump behavior, schedule) to defaults |
| `REFRESH` | re-reads sensors and re-publishes immediately |
| `SYSTEM_ON` / `SYSTEM_OFF` | same as `/system/on` / `/system/off` |
| `WIFI_RESET` | clears saved STA credentials and restarts into provisioning |

## Config JSON schema

Read/write via BLE `CONFIG` or HTTP `/api/config` (`applyConfigJson()` /
`buildConfigJson()` — any subset of top-level fields may be sent; unknown
fields are ignored):

```json
{
  "tempMin": 18.0, "tempMax": 30.0,
  "humMin": 30.0, "humMax": 70.0,
  "soilThreshold": 20,
  "pumpMode": "duration",
  "soilWetTarget": 65,
  "pumpDurationMs": 20000,
  "soilMaxPercent": 80,
  "cooldownMs": 0,
  "sensorIntervalMs": 1000,
  "systemEnabled": true,
  "schedule": {
    "enabled": true,
    "mode": "daily",
    "hour": 5,
    "minute": 0,
    "intervalHours": 1,
    "days": [true, true, true, true, true, true, true]
  }
}
```

| Field | Meaning |
|---|---|
| `tempMin`/`tempMax`, `humMin`/`humMax` | auto-watering fires only when readings are inside both ranges |
| `soilThreshold` | auto-watering fires only when soil % is below this ("it's dry") |
| `pumpMode` | `"duration"` (run for `pumpDurationMs`, always) or `"moisture"` (also stop early once `soilWetTarget` is reached) |
| `soilWetTarget` | early-stop target %, only checked in `"moisture"` mode |
| `pumpDurationMs` | run-time cap — applies in **both** pump modes |
| `soilMaxPercent` | universal safety cutoff — pump always stops immediately at/above this %, regardless of mode |
| `cooldownMs` | minimum time between **automatic** waterings (condition- or schedule-triggered); manual `WATER_NOW` always bypasses it |
| `sensorIntervalMs` | how often sensors are read/published and auto-water/schedule are checked |
| `systemEnabled` | master switch — `false` disables all automatic watering and force-stops the pump; see `/pump/on`'s 403 behavior |
| `schedule.mode` | `"daily"` (fires once at `hour:minute`) or `"interval"` (fires every `intervalHours`, on the hour) |
| `schedule.days` | 7 booleans, Sunday first — which days the schedule is active |

`schedule` and auto-watering conditions are independent triggers that both
call the same `startWatering(false)` and share the same `cooldownMs`, so
they can't double-water.

## Pump behavior

Non-blocking state machine (`updatePump()`, driven from `loop()`) so a
sensor publish or emergency stop can happen mid-watering instead of
waiting for one long blocking call:

- The pump pulses rather than running continuously: `PUMP_PULSE_MS` (1.5s)
  on, `PUMP_SETTLE_MS` (0.8s) off, repeating.
- At the **end of each settle phase** (not the instant the relay switches
  off — that's deliberately avoided, see below), soil moisture is checked:
  the pump stops for good if it's at/above `soilMaxPercent` (both modes),
  or at/above `soilWetTarget` (`"moisture"` mode only).
- Either way, the pump never runs longer than `pumpDurationMs` in total.

The soil check happens after the settle delay rather than immediately at
relay-off specifically because relay/motor switching noise on a shared
power rail can otherwise glitch that one ADC read into a falsely-high
value, misreading as "soil is already wet" and stopping the pump after a
single pulse (~1.5s) — a real bug that existed here for a while. If you
ever see the pump run for one short pulse and then stop for good, check
`soilMaxPercent` / `soilWetTarget` in your config first (a threshold set
too close to your actual soil reading will do exactly that legitimately),
then suspect power/wiring noise if those look reasonable.

## Persistence

`Preferences` (NVS), single `"watering"` namespace. Two independent groups
of keys so a Wi-Fi reset never touches irrigation config, and vice versa:

- **Config** (`loadConfig()`/`saveConfig()`): `tempMin`, `tempMax`,
  `humMin`, `humMax`, `soilTh`, `pumpMode`, `wetTarget`, `pumpDurMs`,
  `soilMax`, `cooldownMs`, `sensorIntMs`, `sysEnabled`, `schedOn`,
  `schedMode`, `schedHour`, `schedMin`, `schedIntH`, `schedDays`.
- **History** (`loadHistory()`/`saveHistory()`): `histBuf` (raw ring buffer
  bytes), `histHead`, `histCount`.
- **Wi-Fi STA credentials** (`loadWifiCreds()`/`saveWifiCreds()`/
  `clearWifiCreds()`): `staSsid`, `staPass`.

## Watering history

Ring buffer of the last `HISTORY_CAPACITY` (10) watering events, persisted
to flash after every watering so the app can catch up on events that
happened with nothing connected. Read via BLE `HISTORY` or HTTP
`/api/history`:

```json
[{"t": 1758271234, "temp": 24.5, "hum": 55.0, "soil": 68}, ...]
```

`t` is epoch seconds, `0` if the device hadn't been time-synced yet when
that watering happened. Oldest first.

## Clock

The ESP32 has no RTC. `TIME` (BLE) / `/api/time` (HTTP) push the current
epoch seconds (pre-shifted for the client's timezone) once on connect and
periodically after; the device tracks wall time against `millis()` in
between syncs (`currentLocalEpoch()`). The schedule feature needs this —
without a sync, `checkSchedule()` simply never fires.

## Debug console

`LOG` (BLE, notify) mirrors every `Serial.println()`-equivalent line
(`logLine()`) live, truncated to whatever ATT MTU is negotiated. `/api/log`
(HTTP, polled with `?since=<seq>`) exposes the same 50-entry ring buffer
for the Wi-Fi dashboard's Console tab. Debugging/testing only — no
functional effect either way.
