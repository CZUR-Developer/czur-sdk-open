import { reactive, readonly } from 'vue';

import type { ExecutionState } from '../types/demo';
import { sendBoundCommand } from './auth-session';
import { buildVideoWsUrl, isOkResponse } from './protocol';
import { nowTimeLabel, recordAlert, recordRuntimeEvent } from './runtime-records';

export interface DeviceResolution {
  width: number;
  height: number;
  real_width: number;
  real_height: number;
  fps: number;
  pixel_format: string;
  is_default: boolean;
}

interface FrameMeta {
  device_id?: string;
  stream_id?: string;
  frame_seq?: number;
  timestamp_ms?: number;
  width?: number;
  height?: number;
  pixel_format?: string;
}

interface DeviceVideoState {
  selectedDeviceId: string;
  detailState: ExecutionState;
  openState: ExecutionState;
  startState: ExecutionState;
  stopState: ExecutionState;
  videoState: ExecutionState;
  errorMessage: string;
  resolutions: DeviceResolution[];
  selectedResolutionKey: string;
  opened: boolean;
  streamId: string;
  streamSessionToken: string;
  previewUrl: string;
  frameMeta: FrameMeta | null;
  frameCount: number;
  lastFrameAt: string;
}

const state = reactive<DeviceVideoState>({
  selectedDeviceId: '',
  detailState: 'idle',
  openState: 'idle',
  startState: 'idle',
  stopState: 'idle',
  videoState: 'idle',
  errorMessage: '',
  resolutions: [],
  selectedResolutionKey: '',
  opened: false,
  streamId: '',
  streamSessionToken: '',
  previewUrl: '',
  frameMeta: null,
  frameCount: 0,
  lastFrameAt: '',
});

let videoSocket: WebSocket | null = null;

export const deviceVideoState = readonly(state);

export async function selectDevice(deviceId: string): Promise<void> {
  await stopVideo();
  resetPreview();
  state.selectedDeviceId = deviceId;
  state.opened = false;
  state.resolutions = [];
  state.selectedResolutionKey = '';
  if (deviceId) {
    await loadDeviceDetail();
  }
}

export async function loadDeviceDetail(): Promise<void> {
  if (!state.selectedDeviceId) {
    state.detailState = 'blocked';
    return;
  }
  state.detailState = 'running';
  state.errorMessage = '';
  const response = await sendBoundCommand('device.get', {
    params: { device_id: state.selectedDeviceId },
  });
  if (!isOkResponse(response)) {
    state.detailState = 'error';
    state.errorMessage = response.message;
    return;
  }
  state.resolutions = asResolutions(response.data.resolutions);
  const defaultResolution = state.resolutions.find((resolution) => resolution.is_default) ?? state.resolutions[0];
  state.selectedResolutionKey = defaultResolution ? resolutionKey(defaultResolution) : '';
  state.detailState = 'success';
  recordRuntimeEvent({
    title: 'device.detail_loaded',
    detail: `device.get returned ${state.resolutions.length} preview resolution(s).`,
    tone: state.resolutions.length > 0 ? 'success' : 'warning',
  });
}

export async function openSelectedDevice(): Promise<void> {
  const resolution = selectedResolution();
  if (!state.selectedDeviceId || !resolution) {
    state.openState = 'blocked';
    return;
  }
  state.openState = 'running';
  state.errorMessage = '';
  const response = await sendBoundCommand('device.open', {
    params: buildVideoParams(resolution),
  });
  if (!isOkResponse(response)) {
    state.openState = 'error';
    state.errorMessage = response.message;
    return;
  }
  state.opened = Boolean(response.data.opened);
  state.openState = state.opened ? 'success' : 'error';
  recordRuntimeEvent({
    title: 'device.opened',
    detail: `device.open selected ${resolution.width}x${resolution.height}@${resolution.fps}fps.`,
    tone: state.opened ? 'success' : 'warning',
  });
}

export async function startVideo(): Promise<void> {
  const resolution = selectedResolution();
  if (!state.selectedDeviceId || !state.opened || !resolution) {
    state.startState = 'blocked';
    return;
  }
  closeVideoSocket();
  resetPreview();
  state.startState = 'running';
  state.videoState = 'running';
  state.errorMessage = '';
  const response = await sendBoundCommand('video.start', {
    params: buildVideoParams(resolution),
  });
  if (!isOkResponse(response)) {
    state.startState = 'error';
    state.videoState = 'error';
    state.errorMessage = response.message;
    return;
  }
  state.streamId = asString(response.data.stream_id);
  state.streamSessionToken = asString(response.data.session_token);
  state.startState = 'success';
  connectVideoSocket();
}

export async function stopVideo(): Promise<void> {
  closeVideoSocket();
  if (!state.selectedDeviceId || !state.streamId) {
    resetPreview();
    state.stopState = 'idle';
    return;
  }
  state.stopState = 'running';
  const response = await sendBoundCommand('video.stop', {
    params: { device_id: state.selectedDeviceId },
  });
  if (!isOkResponse(response)) {
    state.stopState = 'error';
    state.errorMessage = response.message;
    return;
  }
  resetPreview();
  state.stopState = 'success';
  state.videoState = 'idle';
  recordRuntimeEvent({
    title: 'video.stopped',
    detail: `video.stop closed ${asString(response.data.stream_id) || state.streamId}.`,
    tone: 'info',
  });
}

export function setSelectedResolution(key: string): void {
  state.selectedResolutionKey = key;
  state.opened = false;
  resetPreview();
}

export function resetDeviceVideo(): void {
  closeVideoSocket();
  state.selectedDeviceId = '';
  state.detailState = 'idle';
  state.openState = 'idle';
  state.startState = 'idle';
  state.stopState = 'idle';
  state.videoState = 'idle';
  state.errorMessage = '';
  state.resolutions = [];
  state.selectedResolutionKey = '';
  state.opened = false;
  resetPreview();
}

export function resolutionKey(resolution: DeviceResolution): string {
  return `${resolution.width}x${resolution.height}@${resolution.fps}`;
}

function buildVideoParams(resolution: DeviceResolution): Record<string, unknown> {
  return {
    device_id: state.selectedDeviceId,
    width: resolution.width,
    height: resolution.height,
    fps: resolution.fps,
    pixel_format: resolution.pixel_format || 'jpeg',
  };
}

function connectVideoSocket(): void {
  if (!state.streamSessionToken || !state.streamId) {
    state.videoState = 'error';
    state.errorMessage = 'session_token and stream_id required';
    return;
  }
  const socket = new WebSocket(buildVideoWsUrl(state.streamSessionToken, state.streamId));
  socket.binaryType = 'blob';
  videoSocket = socket;
  socket.addEventListener('open', () => {
    state.videoState = 'success';
    recordRuntimeEvent({
      title: 'video.connected',
      detail: `video WS connected for ${state.streamId}.`,
      tone: 'success',
    });
  });
  socket.addEventListener('message', (event) => {
    if (typeof event.data === 'string') {
      handleVideoText(event.data);
      return;
    }
    handleVideoBinary(event.data);
  });
  socket.addEventListener('close', () => {
    if (videoSocket === socket) {
      videoSocket = null;
    }
  });
  socket.addEventListener('error', () => {
    state.videoState = 'error';
    recordAlert({
      name: 'VIDEO_WS_FAILED',
      method: 'video.ws',
      code: 'NETWORK',
      message: 'video websocket failed',
    });
  });
}

function handleVideoText(payload: string): void {
  try {
    const event = JSON.parse(payload) as { event?: string; payload?: FrameMeta };
    if (event.event === 'stream.frame_meta' && event.payload) {
      state.frameMeta = event.payload;
      state.lastFrameAt = nowTimeLabel();
    }
  } catch {
    // Ignore non-JSON video control text.
  }
}

function handleVideoBinary(data: Blob | ArrayBuffer): void {
  const blob = data instanceof Blob ? data : new Blob([data], { type: 'image/jpeg' });
  const nextUrl = URL.createObjectURL(blob);
  revokePreviewUrl();
  state.previewUrl = nextUrl;
  state.frameCount += 1;
  state.lastFrameAt = nowTimeLabel();
}

function closeVideoSocket(): void {
  if (videoSocket && videoSocket.readyState < WebSocket.CLOSING) {
    videoSocket.close(1000, 'manual close');
  }
  videoSocket = null;
}

function resetPreview(): void {
  revokePreviewUrl();
  state.streamId = '';
  state.streamSessionToken = '';
  state.frameMeta = null;
  state.frameCount = 0;
  state.lastFrameAt = '';
}

function revokePreviewUrl(): void {
  if (state.previewUrl) {
    URL.revokeObjectURL(state.previewUrl);
    state.previewUrl = '';
  }
}

function selectedResolution(): DeviceResolution | null {
  return state.resolutions.find((resolution) => resolutionKey(resolution) === state.selectedResolutionKey) ?? null;
}

function asResolutions(value: unknown): DeviceResolution[] {
  if (!Array.isArray(value)) {
    return [];
  }
  return value
    .filter((item): item is Record<string, unknown> => Boolean(item) && typeof item === 'object')
    .map((item) => ({
      width: asNumber(item.width),
      height: asNumber(item.height),
      real_width: asNumber(item.real_width),
      real_height: asNumber(item.real_height),
      fps: asNumber(item.fps) || 15,
      pixel_format: asString(item.pixel_format) || 'jpeg',
      is_default: Boolean(item.is_default),
    }))
    .filter((resolution) => resolution.width > 0 && resolution.height > 0);
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function asNumber(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}
