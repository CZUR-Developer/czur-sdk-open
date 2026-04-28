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
  closeState: ExecutionState;
  startState: ExecutionState;
  stopState: ExecutionState;
  videoState: ExecutionState;
  errorMessage: string;
  resolutions: DeviceResolution[];
  selectedResolutionKey: string;
  opened: boolean;
  streamId: string;
  streamSessionToken: string;
  frameMeta: FrameMeta | null;
  receivedFrameCount: number;
  renderedFrameCount: number;
  droppedFrameCount: number;
  decodeFailedCount: number;
  lastFrameBytes: number;
  lastRenderedSeq: number;
  lastFrameAt: string;
}

interface PendingVideoFrame {
  streamId: string;
  frameSeq: number;
  meta: FrameMeta | null;
  buffer: ArrayBuffer;
}

const state = reactive<DeviceVideoState>({
  selectedDeviceId: '',
  detailState: 'idle',
  openState: 'idle',
  closeState: 'idle',
  startState: 'idle',
  stopState: 'idle',
  videoState: 'idle',
  errorMessage: '',
  resolutions: [],
  selectedResolutionKey: '',
  opened: false,
  streamId: '',
  streamSessionToken: '',
  frameMeta: null,
  receivedFrameCount: 0,
  renderedFrameCount: 0,
  droppedFrameCount: 0,
  decodeFailedCount: 0,
  lastFrameBytes: 0,
  lastRenderedSeq: 0,
  lastFrameAt: '',
});

let videoSocket: WebSocket | null = null;
let videoCanvas: HTMLCanvasElement | null = null;
let videoContext: CanvasRenderingContext2D | null = null;
let pendingFrame: PendingVideoFrame | null = null;
let decodingFrame = false;
let streamGeneration = 0;
let nextSyntheticFrameSeq = 1;

export const deviceVideoState = readonly(state);

export async function selectDevice(deviceId: string): Promise<void> {
  await closeSelectedDevice();
  state.selectedDeviceId = deviceId;
  state.opened = false;
  state.closeState = 'idle';
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
  state.closeState = 'idle';
  recordRuntimeEvent({
    title: 'device.opened',
    detail: `device.open selected ${resolution.width}x${resolution.height}@${resolution.fps}fps.`,
    tone: state.opened ? 'success' : 'warning',
  });
}

export async function closeSelectedDevice(): Promise<void> {
  closeVideoSocket();
  if (!state.selectedDeviceId || (!state.opened && !state.streamId)) {
    resetPreview();
    state.opened = false;
    state.closeState = 'idle';
    return;
  }

  const deviceId = state.selectedDeviceId;
  const streamId = state.streamId;
  state.closeState = 'running';
  state.errorMessage = '';
  const response = await sendBoundCommand('device.close', {
    params: { device_id: deviceId },
  });
  if (!isOkResponse(response)) {
    state.closeState = 'error';
    state.errorMessage = response.message;
    return;
  }

  resetPreview();
  state.opened = false;
  state.openState = 'idle';
  state.startState = 'idle';
  state.stopState = 'idle';
  state.videoState = 'idle';
  state.closeState = 'success';
  recordRuntimeEvent({
    title: 'device.closed',
    detail: `device.close released ${deviceId}${streamId ? ` and stopped ${streamId}` : ''}.`,
    tone: 'info',
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

export async function setSelectedResolution(key: string): Promise<void> {
  if (state.selectedResolutionKey !== key) {
    await closeSelectedDevice();
  }
  state.selectedResolutionKey = key;
}

export function resetDeviceVideo(): void {
  closeVideoSocket();
  state.selectedDeviceId = '';
  state.detailState = 'idle';
  state.openState = 'idle';
  state.closeState = 'idle';
  state.startState = 'idle';
  state.stopState = 'idle';
  state.videoState = 'idle';
  state.errorMessage = '';
  state.resolutions = [];
  state.selectedResolutionKey = '';
  state.opened = false;
  resetPreview();
}

export function attachVideoCanvas(canvas: HTMLCanvasElement | null): void {
  videoCanvas = canvas;
  videoContext = canvas ? canvas.getContext('2d') : null;
  if (canvas) {
    clearCanvas();
  }
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
  socket.binaryType = 'arraybuffer';
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
  if (data instanceof Blob) {
    void data.arrayBuffer().then((buffer) => enqueueVideoFrame(buffer));
    return;
  }
  enqueueVideoFrame(data);
}

function enqueueVideoFrame(buffer: ArrayBuffer): void {
  state.receivedFrameCount += 1;
  state.lastFrameBytes = buffer.byteLength;

  if (!state.streamId || buffer.byteLength === 0 || !isCompleteJpeg(buffer)) {
    state.droppedFrameCount += 1;
    return;
  }

  const meta = state.frameMeta;
  const frameStreamId = meta?.stream_id || state.streamId;
  if (frameStreamId !== state.streamId) {
    state.droppedFrameCount += 1;
    return;
  }

  const frameSeq = typeof meta?.frame_seq === 'number' && Number.isFinite(meta.frame_seq)
    ? meta.frame_seq
    : nextSyntheticFrameSeq++;
  if (frameSeq <= state.lastRenderedSeq) {
    state.droppedFrameCount += 1;
    return;
  }

  pendingFrame = {
    streamId: frameStreamId,
    frameSeq,
    meta,
    buffer: buffer.slice(0),
  };
  processPendingFrame();
}

function processPendingFrame(): void {
  if (decodingFrame || !pendingFrame) {
    return;
  }

  const frame = pendingFrame;
  pendingFrame = null;
  decodingFrame = true;
  const generation = streamGeneration;

  void decodeFrame(frame.buffer)
    .then(async (image) => {
      if (generation !== streamGeneration || frame.streamId !== state.streamId || frame.frameSeq <= state.lastRenderedSeq) {
        closeDecodedImage(image);
        state.droppedFrameCount += 1;
        return;
      }
      if (pendingFrame && pendingFrame.frameSeq > frame.frameSeq) {
        closeDecodedImage(image);
        state.droppedFrameCount += 1;
        return;
      }
      await new Promise<void>((resolve) => {
        requestAnimationFrame(() => {
          if (generation !== streamGeneration || frame.streamId !== state.streamId || frame.frameSeq <= state.lastRenderedSeq) {
            closeDecodedImage(image);
            state.droppedFrameCount += 1;
            resolve();
            return;
          }
          if (pendingFrame && pendingFrame.frameSeq > frame.frameSeq) {
            closeDecodedImage(image);
            state.droppedFrameCount += 1;
            resolve();
            return;
          }
          drawDecodedFrame(image);
          closeDecodedImage(image);
          state.frameMeta = frame.meta;
          state.renderedFrameCount += 1;
          state.lastRenderedSeq = frame.frameSeq;
          state.lastFrameAt = nowTimeLabel();
          resolve();
        });
      });
    })
    .catch(() => {
      state.decodeFailedCount += 1;
      state.droppedFrameCount += 1;
    })
    .finally(() => {
      decodingFrame = false;
      processPendingFrame();
    });
}

function closeVideoSocket(): void {
  if (videoSocket && videoSocket.readyState < WebSocket.CLOSING) {
    videoSocket.close(1000, 'manual close');
  }
  videoSocket = null;
}

function resetPreview(): void {
  clearCanvas();
  streamGeneration += 1;
  pendingFrame = null;
  nextSyntheticFrameSeq = 1;
  state.streamId = '';
  state.streamSessionToken = '';
  state.frameMeta = null;
  state.receivedFrameCount = 0;
  state.renderedFrameCount = 0;
  state.droppedFrameCount = 0;
  state.decodeFailedCount = 0;
  state.lastFrameBytes = 0;
  state.lastRenderedSeq = 0;
  state.lastFrameAt = '';
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

function isCompleteJpeg(buffer: ArrayBuffer): boolean {
  const bytes = new Uint8Array(buffer);
  const length = bytes.length;
  if (length < 4 || bytes[0] !== 0xff || bytes[1] !== 0xd8) {
    return false;
  }
  for (let index = length - 2; index >= 2; index -= 1) {
    if (bytes[index] === 0xff && bytes[index + 1] === 0xd9) {
      return true;
    }
  }
  return false;
}

async function decodeFrame(buffer: ArrayBuffer): Promise<ImageBitmap | HTMLImageElement> {
  const blob = new Blob([buffer], { type: 'image/jpeg' });
  if ('createImageBitmap' in window) {
    return createImageBitmap(blob);
  }
  return decodeWithImage(blob);
}

async function decodeWithImage(blob: Blob): Promise<HTMLImageElement> {
  const url = URL.createObjectURL(blob);
  const image = new Image();
  image.decoding = 'async';
  image.src = url;
  try {
    if ('decode' in image) {
      await image.decode();
    } else {
      await new Promise<void>((resolve, reject) => {
        image.onload = () => resolve();
        image.onerror = () => reject(new Error('image decode failed'));
      });
    }
    return image;
  } finally {
    URL.revokeObjectURL(url);
  }
}

function drawDecodedFrame(image: ImageBitmap | HTMLImageElement): void {
  if (!videoCanvas || !videoContext) {
    return;
  }
  const width = image.width;
  const height = image.height;
  if (width <= 0 || height <= 0) {
    return;
  }
  if (videoCanvas.width !== width || videoCanvas.height !== height) {
    videoCanvas.width = width;
    videoCanvas.height = height;
  }
  videoContext.clearRect(0, 0, width, height);
  videoContext.drawImage(image, 0, 0, width, height);
}

function closeDecodedImage(image: ImageBitmap | HTMLImageElement): void {
  if ('close' in image && typeof image.close === 'function') {
    image.close();
  }
}

function clearCanvas(): void {
  if (!videoCanvas || !videoContext) {
    return;
  }
  videoContext.clearRect(0, 0, videoCanvas.width, videoCanvas.height);
}
