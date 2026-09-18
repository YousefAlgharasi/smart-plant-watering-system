import React, { useEffect, useState, useCallback, useRef } from 'react';
import { View, Text, StyleSheet, TouchableOpacity, ScrollView, Alert } from 'react-native';
import { bleManager, SensorReading } from '../services/BLEManager';
import { insertReading, insertWateringEvent, getLastWateringEvent } from '../services/database';

const REFRESH_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes

export default function DashboardScreen({ navigation }: any) {
  const [reading, setReading] = useState<SensorReading | null>(null);
  const [lastWatered, setLastWatered] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const readingRef = useRef<SensorReading | null>(null);

  const loadLastWatered = useCallback(async () => {
    const row = await getLastWateringEvent();
    if (row) setLastWatered(row.timestamp);
  }, []);

  useEffect(() => {
    bleManager.onSensorUpdate = async (data) => {
      readingRef.current = data;
      setReading(data);
      await insertReading(data.temp, data.hum, data.soil);
    };

    bleManager.onWateredEvent = async () => {
      const r = readingRef.current;
      if (r) {
        await insertWateringEvent(r.temp, r.hum, r.soil);
      }
      await loadLastWatered();
      Alert.alert('Plant watered', 'The pump just ran (automatically or manually).');
    };

    bleManager.onDisconnected = () => {
      navigation.replace('Scan');
    };

    loadLastWatered();

    const interval = setInterval(() => {
      bleManager.requestRefresh();
    }, REFRESH_INTERVAL_MS);

    return () => clearInterval(interval);
  }, [loadLastWatered, navigation]);

  const handleRefresh = async () => {
    setLoading(true);
    await bleManager.requestRefresh();
    setLoading(false);
  };

  const handleWaterNow = async () => {
    await bleManager.waterNow();
  };

  return (
    <ScrollView contentContainerStyle={styles.container}>
      <Text style={styles.title}>Plant Dashboard</Text>

      <View style={styles.card}>
        <Text style={styles.label}>Temperature</Text>
        <Text style={styles.value}>{reading ? `${reading.temp.toFixed(1)} °C` : '--'}</Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.label}>Humidity</Text>
        <Text style={styles.value}>{reading ? `${reading.hum.toFixed(1)} %` : '--'}</Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.label}>Soil Moisture</Text>
        <Text style={styles.value}>{reading ? `${reading.soil} %` : '--'}</Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.label}>Pump Status</Text>
        <Text style={styles.value}>{reading?.pump ? 'Running' : 'Idle'}</Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.label}>Last Watered</Text>
        <Text style={styles.value}>
          {lastWatered ? new Date(lastWatered).toLocaleString() : 'No history yet'}
        </Text>
      </View>

      <TouchableOpacity style={styles.button} onPress={handleRefresh} disabled={loading}>
        <Text style={styles.buttonText}>{loading ? 'Refreshing...' : 'Refresh Now'}</Text>
      </TouchableOpacity>

      <TouchableOpacity style={[styles.button, styles.waterButton]} onPress={handleWaterNow}>
        <Text style={styles.buttonText}>Water Now</Text>
      </TouchableOpacity>

      <TouchableOpacity style={styles.settingsLink} onPress={() => navigation.navigate('Settings')}>
        <Text style={styles.settingsLinkText}>Settings</Text>
      </TouchableOpacity>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { padding: 20, alignItems: 'stretch' },
  title: { fontSize: 24, fontWeight: '700', marginBottom: 20, textAlign: 'center' },
  card: { backgroundColor: '#f2f2f2', borderRadius: 12, padding: 16, marginBottom: 12 },
  label: { fontSize: 14, color: '#666' },
  value: { fontSize: 22, fontWeight: '600', marginTop: 4 },
  button: { backgroundColor: '#2e7d32', padding: 16, borderRadius: 12, marginTop: 8, alignItems: 'center' },
  waterButton: { backgroundColor: '#1565c0' },
  buttonText: { color: '#fff', fontWeight: '700', fontSize: 16 },
  settingsLink: { marginTop: 20, alignItems: 'center' },
  settingsLinkText: { color: '#1565c0', fontSize: 16 },
});
