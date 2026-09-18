import React, { useEffect, useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ActivityIndicator,
  TouchableOpacity,
  PermissionsAndroid,
  Platform,
} from 'react-native';
import { bleManager } from '../services/BLEManager';

async function requestAndroidPermissions() {
  if (Platform.OS === 'android') {
    await PermissionsAndroid.requestMultiple([
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
      PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
    ]);
  }
}

export default function ScanScreen({ navigation }: any) {
  const [status, setStatus] = useState('Looking for your plant device...');

  const startScan = async () => {
    await requestAndroidPermissions();
    setStatus('Scanning for PlantWaterer...');

    bleManager.onConnected = () => {
      navigation.replace('Dashboard');
    };

    bleManager.scanAndConnect();
  };

  useEffect(() => {
    startScan();
  }, []);

  return (
    <View style={styles.container}>
      <ActivityIndicator size="large" color="#2e7d32" />
      <Text style={styles.status}>{status}</Text>
      <TouchableOpacity style={styles.retryButton} onPress={startScan}>
        <Text style={styles.retryText}>Retry Scan</Text>
      </TouchableOpacity>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, justifyContent: 'center', alignItems: 'center', padding: 20 },
  status: { marginTop: 20, fontSize: 16, textAlign: 'center', color: '#444' },
  retryButton: { marginTop: 30, backgroundColor: '#2e7d32', padding: 14, borderRadius: 10 },
  retryText: { color: '#fff', fontWeight: '700' },
});
