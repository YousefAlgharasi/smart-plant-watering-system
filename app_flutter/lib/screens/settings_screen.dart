import 'dart:async';

import 'package:flutter/material.dart';

import '../models/watering_config.dart';
import '../services/ble_service.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  String _mode = 'default';
  final _tempController = TextEditingController(text: '28');
  final _soilController = TextEditingController(text: '40');

  StreamSubscription<WateringConfig>? _configSub;

  @override
  void initState() {
    super.initState();
    _configSub = BleService.instance.configUpdates.listen((config) {
      setState(() {
        _mode = config.mode;
        _tempController.text = config.tempThreshold.toString();
        _soilController.text = config.soilThreshold.toString();
      });
    });
  }

  @override
  void dispose() {
    _configSub?.cancel();
    _tempController.dispose();
    _soilController.dispose();
    super.dispose();
  }

  Future<void> _handleSave() async {
    await BleService.instance.updateConfig(WateringConfig(
      mode: _mode,
      tempThreshold: double.tryParse(_tempController.text) ?? 0,
      soilThreshold: int.tryParse(_soilController.text) ?? 0,
    ));
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Settings sent to the device.')),
      );
    }
  }

  Future<void> _handleReset() async {
    await BleService.instance.resetToDefaults();
    setState(() {
      _mode = 'default';
      _tempController.text = '28';
      _soilController.text = '40';
    });
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Device reverted to default watering settings.')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final isCustom = _mode == 'custom';
    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              const Text('Custom mode', style: TextStyle(fontSize: 14, color: Colors.black87)),
              Switch(
                value: isCustom,
                onChanged: (v) => setState(() => _mode = v ? 'custom' : 'default'),
              ),
            ],
          ),
          const SizedBox(height: 12),
          const Text('Water when temperature is at least (°C)',
              style: TextStyle(fontSize: 14, color: Colors.black54)),
          const SizedBox(height: 6),
          TextField(
            controller: _tempController,
            keyboardType: const TextInputType.numberWithOptions(decimal: true),
            enabled: isCustom,
            decoration: const InputDecoration(border: OutlineInputBorder()),
          ),
          const SizedBox(height: 12),
          const Text('Water when soil moisture is below (%)',
              style: TextStyle(fontSize: 14, color: Colors.black54)),
          const SizedBox(height: 6),
          TextField(
            controller: _soilController,
            keyboardType: TextInputType.number,
            enabled: isCustom,
            decoration: const InputDecoration(border: OutlineInputBorder()),
          ),
          const SizedBox(height: 20),
          ElevatedButton(
            onPressed: _handleSave,
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF2E7D32),
              padding: const EdgeInsets.symmetric(vertical: 16),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            ),
            child: const Text('Save', style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 16)),
          ),
          const SizedBox(height: 12),
          ElevatedButton(
            onPressed: _handleReset,
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFFC62828),
              padding: const EdgeInsets.symmetric(vertical: 16),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            ),
            child: const Text('Reset to Default', style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 16)),
          ),
        ],
      ),
    );
  }
}
