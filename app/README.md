# Plant Watering App

React Native app that connects to the ESP32 over Bluetooth LE, shows live
sensor data, and stores watering history locally in SQLite.

## Setup

1. Create a bare React Native project (not Expo managed — BLE needs native code):
   ```
   npx react-native init PlantWateringApp --version 0.74.0
   ```
2. Copy `App.tsx`, `src/`, and merge `package.json` dependencies into the new project.
3. Install dependencies:
   ```
   npm install
   cd ios && pod install && cd ..   # iOS only
   ```
4. **Android permissions** — add to `android/app/src/main/AndroidManifest.xml`:
   ```xml
   <uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
   <uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
   <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
   ```
5. **iOS permissions** — add to `ios/PlantWateringApp/Info.plist`:
   ```xml
   <key>NSBluetoothAlwaysUsageDescription</key>
   <string>This app connects to your plant watering device over Bluetooth.</string>
   ```
6. Run:
   ```
   npm run android
   # or
   npm run ios
   ```

## How it works

- On launch, the app scans for a BLE device named `PlantWaterer` and connects
  automatically. Once connected, it jumps straight to the Dashboard.
- The Dashboard shows live temp/humidity/soil readings (pushed by the ESP32
  every ~10s), auto-refreshes every 5 minutes, and has a manual Refresh and
  Water Now button.
- Every reading and every watering event is saved to a local SQLite database
  (`plant.db`), so "Last Watered" persists across app restarts.
- Settings lets you switch between Default and Custom watering thresholds
  (temperature + soil moisture), and a Reset button reverts the ESP32 to
  its built-in defaults.

## UUIDs

Must match `src/constants/ble.ts` exactly against the firmware's `#define`s.
If you change one side, change the other.
