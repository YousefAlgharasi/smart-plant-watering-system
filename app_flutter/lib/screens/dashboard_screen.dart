import 'dart:async';

import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

import '../models/sensor_reading.dart';
import '../services/ble_service.dart';
import '../services/database_service.dart';
import 'scan_screen.dart';
import 'settings_screen.dart';

const _refreshInterval = Duration(minutes: 5);

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  SensorReading? _reading;
  DateTime? _lastWatered;
  bool _refreshing = false;

  StreamSubscription<SensorReading>? _sensorSub;
  StreamSubscription<void>? _wateredSub;
  StreamSubscription<bool>? _connectionSub;
  Timer? _autoRefreshTimer;

  @override
  void initState() {
    super.initState();

    _sensorSub = BleService.instance.sensorUpdates.listen((data) {
      setState(() => _reading = data);
      DatabaseService.instance.insertReading(data);
    });

    _wateredSub = BleService.instance.wateredEvents.listen((_) async {
      await DatabaseService.instance.insertWateringEvent(BleService.instance.lastReading);
      await _loadLastWatered();
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Plant watered — the pump just ran.')),
        );
      }
    });

    _connectionSub = BleService.instance.connectionStatus.listen((connected) {
      if (!connected && mounted) {
        Navigator.of(context).pushReplacement(
          MaterialPageRoute(builder: (_) => const ScanScreen()),
        );
      }
    });

    _loadLastWatered();

    _autoRefreshTimer = Timer.periodic(_refreshInterval, (_) {
      BleService.instance.requestRefresh();
    });
  }

  Future<void> _loadLastWatered() async {
    final row = await DatabaseService.instance.getLastWateringEvent();
    if (row != null && mounted) {
      setState(() => _lastWatered = DateTime.parse(row['timestamp'] as String));
    }
  }

  Future<void> _handleRefresh() async {
    setState(() => _refreshing = true);
    await BleService.instance.requestRefresh();
    if (mounted) setState(() => _refreshing = false);
  }

  Future<void> _handleWaterNow() async {
    await BleService.instance.waterNow();
  }

  @override
  void dispose() {
    _sensorSub?.cancel();
    _wateredSub?.cancel();
    _connectionSub?.cancel();
    _autoRefreshTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final reading = _reading;
    return Scaffold(
      appBar: AppBar(title: const Text('Plant Dashboard')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _InfoCard(
            label: 'Temperature',
            value: reading != null ? '${reading.temp.toStringAsFixed(1)} °C' : '--',
          ),
          _InfoCard(
            label: 'Humidity',
            value: reading != null ? '${reading.hum.toStringAsFixed(1)} %' : '--',
          ),
          _InfoCard(
            label: 'Soil Moisture',
            value: reading != null ? '${reading.soil} %' : '--',
          ),
          _InfoCard(
            label: 'Pump Status',
            value: reading?.pump == true ? 'Running' : 'Idle',
          ),
          _InfoCard(
            label: 'Last Watered',
            value: _lastWatered != null
                ? DateFormat.yMMMd().add_jm().format(_lastWatered!)
                : 'No history yet',
          ),
          const SizedBox(height: 8),
          ElevatedButton(
            onPressed: _refreshing ? null : _handleRefresh,
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF2E7D32),
              padding: const EdgeInsets.symmetric(vertical: 16),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            ),
            child: Text(
              _refreshing ? 'Refreshing...' : 'Refresh Now',
              style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 16),
            ),
          ),
          const SizedBox(height: 8),
          ElevatedButton(
            onPressed: _handleWaterNow,
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF1565C0),
              padding: const EdgeInsets.symmetric(vertical: 16),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            ),
            child: const Text(
              'Water Now',
              style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 16),
            ),
          ),
          const SizedBox(height: 20),
          Center(
            child: TextButton(
              onPressed: () => Navigator.of(context).push(
                MaterialPageRoute(builder: (_) => const SettingsScreen()),
              ),
              child: const Text('Settings', style: TextStyle(color: Color(0xFF1565C0), fontSize: 16)),
            ),
          ),
        ],
      ),
    );
  }
}

class _InfoCard extends StatelessWidget {
  final String label;
  final String value;

  const _InfoCard({required this.label, required this.value});

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: const Color(0xFFF2F2F2),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: const TextStyle(fontSize: 14, color: Colors.black54)),
          const SizedBox(height: 4),
          Text(value, style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w600)),
        ],
      ),
    );
  }
}
