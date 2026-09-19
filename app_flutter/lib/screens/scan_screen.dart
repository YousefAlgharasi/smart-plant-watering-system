import 'dart:async';

import 'package:flutter/material.dart';

import '../services/ble_service.dart';
import 'dashboard_screen.dart';

/// Shown on launch and whenever the connection drops. Scans for
/// "PlantWaterer" and jumps straight to the Dashboard once connected.
class ScanScreen extends StatefulWidget {
  const ScanScreen({super.key});

  @override
  State<ScanScreen> createState() => _ScanScreenState();
}

class _ScanScreenState extends State<ScanScreen> {
  String _status = 'Looking for your plant device...';
  StreamSubscription<bool>? _connectionSub;

  @override
  void initState() {
    super.initState();
    _startScan();
  }

  void _startScan() {
    setState(() => _status = 'Scanning for PlantWaterer...');

    _connectionSub?.cancel();
    _connectionSub = BleService.instance.connectionStatus.listen((connected) {
      if (connected && mounted) {
        Navigator.of(context).pushReplacement(
          MaterialPageRoute(builder: (_) => const DashboardScreen()),
        );
      } else if (!connected && mounted) {
        setState(() => _status = 'Connection failed. Retry?');
      }
    });

    BleService.instance.scanAndConnect();
  }

  @override
  void dispose() {
    _connectionSub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              const CircularProgressIndicator(color: Color(0xFF2E7D32)),
              const SizedBox(height: 20),
              Text(
                _status,
                textAlign: TextAlign.center,
                style: const TextStyle(fontSize: 16, color: Colors.black54),
              ),
              const SizedBox(height: 30),
              ElevatedButton(
                onPressed: _startScan,
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF2E7D32),
                  padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 14),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                ),
                child: const Text(
                  'Retry Scan',
                  style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
