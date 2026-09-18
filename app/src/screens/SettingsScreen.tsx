import React, { useEffect, useState } from 'react';
import { View, Text, StyleSheet, TextInput, TouchableOpacity, Switch, Alert } from 'react-native';
import { bleManager, Config } from '../services/BLEManager';

export default function SettingsScreen({ navigation }: any) {
  const [mode, setMode] = useState<'default' | 'custom'>('default');
  const [tempThreshold, setTempThreshold] = useState('28');
  const [soilThreshold, setSoilThreshold] = useState('40');

  useEffect(() => {
    bleManager.onConfigUpdate = (config: Config) => {
      setMode(config.mode);
      setTempThreshold(String(config.tempThreshold));
      setSoilThreshold(String(config.soilThreshold));
    };
  }, []);

  const handleSave = async () => {
    await bleManager.updateConfig({
      mode,
      tempThreshold: parseFloat(tempThreshold),
      soilThreshold: parseInt(soilThreshold, 10),
    });
    Alert.alert('Saved', 'Settings sent to the device.');
  };

  const handleReset = async () => {
    await bleManager.resetToDefaults();
    setMode('default');
    setTempThreshold('28');
    setSoilThreshold('40');
    Alert.alert('Reset', 'Device reverted to default watering settings.');
  };

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Watering Settings</Text>

      <View style={styles.row}>
        <Text style={styles.label}>Custom mode</Text>
        <Switch value={mode === 'custom'} onValueChange={(v) => setMode(v ? 'custom' : 'default')} />
      </View>

      <Text style={styles.label}>Water when temperature is at least (°C)</Text>
      <TextInput
        style={styles.input}
        keyboardType="numeric"
        value={tempThreshold}
        onChangeText={setTempThreshold}
        editable={mode === 'custom'}
      />

      <Text style={styles.label}>Water when soil moisture is below (%)</Text>
      <TextInput
        style={styles.input}
        keyboardType="numeric"
        value={soilThreshold}
        onChangeText={setSoilThreshold}
        editable={mode === 'custom'}
      />

      <TouchableOpacity style={styles.button} onPress={handleSave}>
        <Text style={styles.buttonText}>Save</Text>
      </TouchableOpacity>

      <TouchableOpacity style={[styles.button, styles.resetButton]} onPress={handleReset}>
        <Text style={styles.buttonText}>Reset to Default</Text>
      </TouchableOpacity>

      <TouchableOpacity style={styles.back} onPress={() => navigation.goBack()}>
        <Text style={styles.backText}>Back to Dashboard</Text>
      </TouchableOpacity>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, padding: 20 },
  title: { fontSize: 22, fontWeight: '700', marginBottom: 20 },
  row: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', marginBottom: 20 },
  label: { fontSize: 14, color: '#444', marginBottom: 6, marginTop: 12 },
  input: { borderWidth: 1, borderColor: '#ccc', borderRadius: 8, padding: 10, fontSize: 16 },
  button: { backgroundColor: '#2e7d32', padding: 16, borderRadius: 12, marginTop: 20, alignItems: 'center' },
  resetButton: { backgroundColor: '#c62828' },
  buttonText: { color: '#fff', fontWeight: '700', fontSize: 16 },
  back: { marginTop: 20, alignItems: 'center' },
  backText: { color: '#1565c0', fontSize: 16 },
});
