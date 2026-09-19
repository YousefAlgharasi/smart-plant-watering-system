import 'package:flutter/material.dart';

import 'screens/scan_screen.dart';

void main() {
  runApp(const PlantWateringApp());
}

class PlantWateringApp extends StatelessWidget {
  const PlantWateringApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Plant Waterer',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorSchemeSeed: const Color(0xFF2E7D32),
        useMaterial3: true,
      ),
      home: const ScanScreen(),
    );
  }
}
