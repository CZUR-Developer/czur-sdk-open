import { reactive, readonly } from 'vue';

import type { ExecutionState, TableRow } from '../types/demo';
import { authSessionState, sendBoundCommand } from './auth-session';
import { isOkResponse } from './protocol';
import { nowTimeLabel, recordRuntimeEvent } from './runtime-records';

export interface DeviceDescriptorPayload {
  device_id?: string;
  model?: string;
  display_name?: string;
  vid?: number;
  pid?: number;
  status?: string;
  authorized?: boolean;
  supports_video?: boolean;
  features?: {
    image_transfer_protocol?: boolean;
  };
}

interface DeviceInventoryState {
  rows: TableRow[];
  devices: DeviceDescriptorPayload[];
  count: number;
  listState: ExecutionState;
  lastLoadedAt: string;
  errorMessage: string;
}

const state = reactive<DeviceInventoryState>({
  rows: [],
  devices: [],
  count: 0,
  listState: 'idle',
  lastLoadedAt: '',
  errorMessage: '',
});

let activeLoadToken = 0;

export const deviceInventoryState = readonly(state);

export async function loadDeviceInventory(): Promise<void> {
  activeLoadToken += 1;
  const loadToken = activeLoadToken;

  if (!authSessionState.token) {
    resetDeviceInventory();
    state.listState = 'blocked';
    state.errorMessage = 'token required';
    return;
  }

  state.listState = 'running';
  state.errorMessage = '';

  try {
    const response = await sendBoundCommand('device.list');
    if (loadToken !== activeLoadToken) {
      return;
    }
    if (!isOkResponse(response)) {
      state.listState = 'error';
      state.errorMessage = response.message;
      return;
    }

    const devices = asDeviceDescriptors(response.data.devices);
    state.devices = devices;
    state.rows = devices.map((device) => ({
      id: device.device_id || `${device.vid || 0}:${device.pid || 0}`,
      cells: {
        deviceId: device.device_id || '-',
        model: device.model || '-',
        displayName: device.display_name || '-',
        vid: formatUsbHex(device.vid),
        pid: formatUsbHex(device.pid),
        status: device.status || '-',
        authorized: String(Boolean(device.authorized)),
        supportsVideo: String(Boolean(device.supports_video)),
      },
    }));
    state.count = typeof response.data.count === 'number' ? response.data.count : devices.length;
    state.lastLoadedAt = nowTimeLabel();
    state.listState = 'success';
    recordRuntimeEvent({
      title: 'device.inventory_loaded',
      detail: `device.list resolved ${state.count} device(s) at ${state.lastLoadedAt}.`,
      tone: state.count > 0 ? 'success' : 'info',
    });
  } catch (error) {
    if (loadToken !== activeLoadToken) {
      return;
    }
    state.listState = 'error';
    state.errorMessage = error instanceof Error ? error.message : 'unknown error';
  }
}

export function resetDeviceInventory(): void {
  activeLoadToken += 1;
  state.rows = [];
  state.devices = [];
  state.count = 0;
  state.listState = 'idle';
  state.lastLoadedAt = '';
  state.errorMessage = '';
}

export function removeInventoryDevice(deviceId: string): boolean {
  if (!deviceId) {
    return false;
  }
  const nextDevices = state.devices.filter((device) => device.device_id !== deviceId);
  if (nextDevices.length === state.devices.length) {
    return false;
  }
  state.devices = nextDevices;
  state.rows = state.rows.filter((row) => row.id !== deviceId && row.cells.deviceId !== deviceId);
  state.count = nextDevices.length;
  state.lastLoadedAt = nowTimeLabel();
  return true;
}

function asDeviceDescriptors(value: unknown): DeviceDescriptorPayload[] {
  if (!Array.isArray(value)) {
    return [];
  }
  return value.filter((item) => item && typeof item === 'object') as DeviceDescriptorPayload[];
}

function formatUsbHex(value: unknown): string {
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) {
    return '-';
  }
  return `0x${value.toString(16).toUpperCase().padStart(4, '0')}`;
}
