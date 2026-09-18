import { BleManager, Device } from 'react-native-ble-plx';
import { Buffer } from 'buffer';
import {
  SERVICE_UUID,
  SENSOR_CHAR_UUID,
  EVENT_CHAR_UUID,
  CONFIG_CHAR_UUID,
  COMMAND_CHAR_UUID,
  DEVICE_NAME,
} from '../constants/ble';

export type SensorReading = {
  temp: number;
  hum: number;
  soil: number;
  pump: boolean;
};

export type Config = {
  mode: 'default' | 'custom';
  tempThreshold: number;
  soilThreshold: number;
};

class PlantBLEManager {
  private manager = new BleManager();
  private device: Device | null = null;

  onSensorUpdate: ((data: SensorReading) => void) | null = null;
  onWateredEvent: (() => void) | null = null;
  onConfigUpdate: ((config: Config) => void) | null = null;
  onConnected: (() => void) | null = null;
  onDisconnected: (() => void) | null = null;

  scanAndConnect() {
    this.manager.startDeviceScan(null, null, (error, scannedDevice) => {
      if (error) {
        console.warn('Scan error', error);
        return;
      }
      if (scannedDevice?.name === DEVICE_NAME) {
        this.manager.stopDeviceScan();
        this.connectToDevice(scannedDevice);
      }
    });
  }

  private async connectToDevice(device: Device) {
    try {
      const connected = await device.connect();
      this.device = await connected.discoverAllServicesAndCharacteristics();
      this.subscribeToNotifications();
      this.onConnected?.();

      this.device.onDisconnected(() => {
        this.device = null;
        this.onDisconnected?.();
      });
    } catch (e) {
      console.warn('Connection failed', e);
    }
  }

  private subscribeToNotifications() {
    if (!this.device) return;

    this.device.monitorCharacteristicForService(
      SERVICE_UUID,
      SENSOR_CHAR_UUID,
      (error, characteristic) => {
        if (error || !characteristic?.value) return;
        const json = Buffer.from(characteristic.value, 'base64').toString('utf-8');
        try {
          const data: SensorReading = JSON.parse(json);
          this.onSensorUpdate?.(data);
        } catch {
          // ignore malformed packet
        }
      }
    );

    this.device.monitorCharacteristicForService(
      SERVICE_UUID,
      EVENT_CHAR_UUID,
      (error, characteristic) => {
        if (error || !characteristic?.value) return;
        const text = Buffer.from(characteristic.value, 'base64').toString('utf-8');
        if (text === 'WATERED') {
          this.onWateredEvent?.();
        }
      }
    );

    this.device.monitorCharacteristicForService(
      SERVICE_UUID,
      CONFIG_CHAR_UUID,
      (error, characteristic) => {
        if (error || !characteristic?.value) return;
        const json = Buffer.from(characteristic.value, 'base64').toString('utf-8');
        try {
          const config: Config = JSON.parse(json);
          this.onConfigUpdate?.(config);
        } catch {
          // ignore malformed packet
        }
      }
    );
  }

  async requestRefresh() {
    await this.writeCommand('REFRESH');
  }

  async waterNow() {
    await this.writeCommand('WATER_NOW');
  }

  async resetToDefaults() {
    await this.writeCommand('RESET_DEFAULTS');
  }

  async updateConfig(config: Config) {
    if (!this.device) return;
    const payload = Buffer.from(JSON.stringify(config), 'utf-8').toString('base64');
    await this.device.writeCharacteristicWithResponseForService(
      SERVICE_UUID,
      CONFIG_CHAR_UUID,
      payload
    );
  }

  private async writeCommand(cmd: string) {
    if (!this.device) return;
    const payload = Buffer.from(cmd, 'utf-8').toString('base64');
    await this.device.writeCharacteristicWithResponseForService(
      SERVICE_UUID,
      COMMAND_CHAR_UUID,
      payload
    );
  }

  disconnect() {
    this.device?.cancelConnection();
  }
}

export const bleManager = new PlantBLEManager();
