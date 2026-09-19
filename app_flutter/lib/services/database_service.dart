import 'package:path/path.dart';
import 'package:sqflite/sqflite.dart';

import '../models/sensor_reading.dart';

/// Local history: every sensor reading and every watering event, each
/// stamped with the phone's own clock (the ESP32 has no RTC).
class DatabaseService {
  DatabaseService._internal();
  static final DatabaseService instance = DatabaseService._internal();

  Database? _db;

  Future<Database> get _database async {
    final existing = _db;
    if (existing != null) return existing;
    final db = await _open();
    _db = db;
    return db;
  }

  Future<Database> _open() async {
    final path = join(await getDatabasesPath(), 'plant.db');
    return openDatabase(
      path,
      version: 1,
      onCreate: (db, version) async {
        await db.execute('''
          CREATE TABLE readings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            temp REAL,
            hum REAL,
            soil INTEGER,
            timestamp TEXT
          );
        ''');
        await db.execute('''
          CREATE TABLE watering_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            temp REAL,
            hum REAL,
            soil INTEGER,
            timestamp TEXT
          );
        ''');
      },
    );
  }

  Future<void> insertReading(SensorReading reading) async {
    final db = await _database;
    await db.insert('readings', {
      'temp': reading.temp,
      'hum': reading.hum,
      'soil': reading.soil,
      'timestamp': DateTime.now().toIso8601String(),
    });
  }

  Future<void> insertWateringEvent(SensorReading? reading) async {
    final db = await _database;
    await db.insert('watering_events', {
      'temp': reading?.temp,
      'hum': reading?.hum,
      'soil': reading?.soil,
      'timestamp': DateTime.now().toIso8601String(),
    });
  }

  Future<Map<String, Object?>?> getLastWateringEvent() async {
    final db = await _database;
    final rows = await db.query(
      'watering_events',
      orderBy: 'id DESC',
      limit: 1,
    );
    return rows.isNotEmpty ? rows.first : null;
  }

  Future<List<Map<String, Object?>>> getRecentReadings({int limit = 50}) async {
    final db = await _database;
    return db.query('readings', orderBy: 'id DESC', limit: limit);
  }

  Future<List<Map<String, Object?>>> getWateringHistory({int limit = 20}) async {
    final db = await _database;
    return db.query('watering_events', orderBy: 'id DESC', limit: limit);
  }
}
