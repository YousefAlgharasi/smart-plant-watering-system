import SQLite from 'react-native-sqlite-storage';

SQLite.enablePromise(true);

let db: SQLite.SQLiteDatabase;

export async function initDB() {
  db = await SQLite.openDatabase({ name: 'plant.db', location: 'default' });

  await db.executeSql(`
    CREATE TABLE IF NOT EXISTS readings (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      temp REAL,
      hum REAL,
      soil INTEGER,
      timestamp TEXT
    );
  `);

  await db.executeSql(`
    CREATE TABLE IF NOT EXISTS watering_events (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      temp REAL,
      hum REAL,
      soil INTEGER,
      timestamp TEXT
    );
  `);
}

export async function insertReading(temp: number, hum: number, soil: number) {
  await db.executeSql(
    'INSERT INTO readings (temp, hum, soil, timestamp) VALUES (?, ?, ?, ?)',
    [temp, hum, soil, new Date().toISOString()]
  );
}

export async function insertWateringEvent(temp: number, hum: number, soil: number) {
  await db.executeSql(
    'INSERT INTO watering_events (temp, hum, soil, timestamp) VALUES (?, ?, ?, ?)',
    [temp, hum, soil, new Date().toISOString()]
  );
}

export async function getLastWateringEvent() {
  const [result] = await db.executeSql(
    'SELECT * FROM watering_events ORDER BY id DESC LIMIT 1'
  );
  return result.rows.length > 0 ? result.rows.item(0) : null;
}

export async function getRecentReadings(limit = 50) {
  const [result] = await db.executeSql(
    'SELECT * FROM readings ORDER BY id DESC LIMIT ?',
    [limit]
  );
  const rows = [];
  for (let i = 0; i < result.rows.length; i++) {
    rows.push(result.rows.item(i));
  }
  return rows;
}

export async function getWateringHistory(limit = 20) {
  const [result] = await db.executeSql(
    'SELECT * FROM watering_events ORDER BY id DESC LIMIT ?',
    [limit]
  );
  const rows = [];
  for (let i = 0; i < result.rows.length; i++) {
    rows.push(result.rows.item(i));
  }
  return rows;
}
