/// A single snapshot pushed by the SENSOR characteristic.
class SensorReading {
  final double temp;
  final double hum;
  final int soil;
  final bool pump;

  const SensorReading({
    required this.temp,
    required this.hum,
    required this.soil,
    required this.pump,
  });

  factory SensorReading.fromJson(Map<String, dynamic> json) {
    return SensorReading(
      temp: (json['temp'] as num).toDouble(),
      hum: (json['hum'] as num).toDouble(),
      soil: (json['soil'] as num).toInt(),
      pump: json['pump'] as bool,
    );
  }
}
