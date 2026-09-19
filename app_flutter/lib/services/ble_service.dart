import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

import '../constants/ble_constants.dart';
import '../models/sensor_reading.dart';
import '../models/watering_config.dart';

/// Talks to the ESP32 "PlantWaterer" GATT server: scans, connects,
/// subscribes to notifications, and exposes writes as simple methods.
///
/// A singleton so the Scan/Dashboard/Settings screens share one BLE
/// connection instead of each opening their own.
class BleService {
  BleService._internal();
  static final BleService instance = BleService._internal();

  BluetoothDevice? _device;
  BluetoothCharacteristic? _sensorChar;
  BluetoothCharacteristic? _eventChar;
  BluetoothCharacteristic? _configChar;
  BluetoothCharacteristic? _commandChar;

  StreamSubscription<List<ScanResult>>? _scanSub;
  StreamSubscription<BluetoothConnectionState>? _connSub;
  final List<StreamSubscription<List<int>>> _notifySubs = [];

  final _sensorController = StreamController<SensorReading>.broadcast();
  final _wateredController = StreamController<void>.broadcast();
  final _configController = StreamController<WateringConfig>.broadcast();
  final _connectionController = StreamController<bool>.broadcast();

  /// Live sensor readings as they arrive from the SENSOR characteristic.
  Stream<SensorReading> get sensorUpdates => _sensorController.stream;

  /// Fires once every time the ESP32 reports the pump ran.
  Stream<void> get wateredEvents => _wateredController.stream;

  /// Fires when the device pushes its current CONFIG (e.g. after a reset).
  Stream<WateringConfig> get configUpdates => _configController.stream;

  /// true when connected, false on disconnect (initial or dropped).
  Stream<bool> get connectionStatus => _connectionController.stream;

  /// The most recent sensor reading, kept so a WATERED event can be
  /// logged alongside the conditions that triggered it.
  SensorReading? lastReading;

  bool get isConnected => _device != null;

  Future<void> _requestPermissions() async {
    if (Platform.isAndroid) {
      await [
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
        Permission.locationWhenInUse,
      ].request();
    }
  }

  /// Starts scanning for a device named [deviceName] and connects to the
  /// first match. Connection result is reported on [connectionStatus].
  Future<void> scanAndConnect() async {
    await _requestPermissions();
    await stopScan();

    _scanSub = FlutterBluePlus.scanResults.listen((results) async {
      for (final result in results) {
        final advertisedName = result.device.platformName.isNotEmpty
            ? result.device.platformName
            : result.advertisementData.advName;
        if (advertisedName == deviceName) {
          await stopScan();
          await _connectToDevice(result.device);
          break;
        }
      }
    });

    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 30));
  }

  Future<void> stopScan() async {
    await _scanSub?.cancel();
    _scanSub = null;
    if (FlutterBluePlus.isScanningNow) {
      await FlutterBluePlus.stopScan();
    }
  }

  Future<void> _connectToDevice(BluetoothDevice device) async {
    try {
      await device.connect(autoConnect: false);
      final services = await device.discoverServices();
      final service = services.firstWhere((s) => s.uuid == Guid(serviceUuid));

      _sensorChar = service.characteristics
          .firstWhere((c) => c.uuid == Guid(sensorCharUuid));
      _eventChar =
          service.characteristics.firstWhere((c) => c.uuid == Guid(eventCharUuid));
      _configChar = service.characteristics
          .firstWhere((c) => c.uuid == Guid(configCharUuid));
      _commandChar = service.characteristics
          .firstWhere((c) => c.uuid == Guid(commandCharUuid));

      _device = device;
      await _subscribeToNotifications();

      _connSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          _handleDisconnect();
        }
      });

      _connectionController.add(true);
    } catch (e) {
      debugPrint('BLE connect failed: $e');
      _handleDisconnect();
    }
  }

  Future<void> _subscribeToNotifications() async {
    final sensorChar = _sensorChar;
    if (sensorChar != null) {
      await sensorChar.setNotifyValue(true);
      _notifySubs.add(sensorChar.lastValueStream.listen(_handleSensorValue));
    }

    final eventChar = _eventChar;
    if (eventChar != null) {
      await eventChar.setNotifyValue(true);
      _notifySubs.add(eventChar.lastValueStream.listen(_handleEventValue));
    }

    final configChar = _configChar;
    if (configChar != null) {
      await configChar.setNotifyValue(true);
      _notifySubs.add(configChar.lastValueStream.listen(_handleConfigValue));
    }
  }

  void _handleSensorValue(List<int> value) {
    if (value.isEmpty) return;
    try {
      final json = jsonDecode(utf8.decode(value)) as Map<String, dynamic>;
      final reading = SensorReading.fromJson(json);
      lastReading = reading;
      _sensorController.add(reading);
    } catch (_) {
      // ignore malformed packet
    }
  }

  void _handleEventValue(List<int> value) {
    if (value.isEmpty) return;
    if (utf8.decode(value) == eventWatered) {
      _wateredController.add(null);
    }
  }

  void _handleConfigValue(List<int> value) {
    if (value.isEmpty) return;
    try {
      final json = jsonDecode(utf8.decode(value)) as Map<String, dynamic>;
      _configController.add(WateringConfig.fromJson(json));
    } catch (_) {
      // ignore malformed packet
    }
  }

  Future<void> _writeCommand(String cmd) async {
    final char = _commandChar;
    if (char == null) return;
    await char.write(utf8.encode(cmd), withoutResponse: false);
  }

  Future<void> requestRefresh() => _writeCommand(cmdRefresh);

  Future<void> waterNow() => _writeCommand(cmdWaterNow);

  Future<void> resetToDefaults() => _writeCommand(cmdResetDefaults);

  Future<void> updateConfig(WateringConfig config) async {
    final char = _configChar;
    if (char == null) return;
    await char.write(utf8.encode(jsonEncode(config.toJson())),
        withoutResponse: false);
  }

  Future<void> disconnect() async {
    await _device?.disconnect();
  }

  void _handleDisconnect() {
    for (final sub in _notifySubs) {
      sub.cancel();
    }
    _notifySubs.clear();
    _connSub?.cancel();
    _connSub = null;
    _sensorChar = null;
    _eventChar = null;
    _configChar = null;
    _commandChar = null;
    _device = null;
    lastReading = null;
    _connectionController.add(false);
  }
}
