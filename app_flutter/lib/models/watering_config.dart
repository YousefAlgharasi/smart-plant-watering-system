/// Watering thresholds, mirrored from/to the CONFIG characteristic.
class WateringConfig {
  final String mode; // 'default' | 'custom'
  final double tempThreshold;
  final int soilThreshold;

  const WateringConfig({
    required this.mode,
    required this.tempThreshold,
    required this.soilThreshold,
  });

  factory WateringConfig.fromJson(Map<String, dynamic> json) {
    return WateringConfig(
      mode: json['mode'] as String,
      tempThreshold: (json['tempThreshold'] as num).toDouble(),
      soilThreshold: (json['soilThreshold'] as num).toInt(),
    );
  }

  Map<String, dynamic> toJson() => {
        'mode': mode,
        'tempThreshold': tempThreshold,
        'soilThreshold': soilThreshold,
      };
}
