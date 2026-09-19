import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:plant_watering_flutter/main.dart';

void main() {
  testWidgets('shows the scanning screen on launch', (WidgetTester tester) async {
    await tester.pumpWidget(const PlantWateringApp());

    expect(find.text('Scanning for PlantWaterer...'), findsNothing);
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    expect(find.text('Retry Scan'), findsOneWidget);
  });
}
