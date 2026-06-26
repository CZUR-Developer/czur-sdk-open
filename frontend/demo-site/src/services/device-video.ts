import { reactive, readonly } from 'vue';

import type { ExecutionState } from '../types/demo';
import { sendBoundCommand } from './auth-session';
import { buildVideoWsUrl, isOkResponse } from './protocol';
import { nowTimeLabel, recordAlert, recordInternalRuntimeEvent, recordRuntimeEvent } from './runtime-records';

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
  detected_rects?: DetectedRect[];
  detected_rects_source?: {
    width?: number;
    height?: number;
  };
}

interface DetectedRect {
  left_top?: [number, number];
  right_top?: [number, number];
  right_down?: [number, number];
  left_down?: [number, number];
}

interface DeviceFeatures {
  image_transfer_protocol: boolean;
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
  features: DeviceFeatures;
  resolutions: DeviceResolution[];
  selectedResolutionKey: string;
  opened: boolean;
  streamId: string;
  streamSessionToken: string;
  frameMeta: FrameMeta | null;
  showDetectedRects: boolean;
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
  width: number;
  height: number;
  pixelFormat: string;
  buffer: ArrayBuffer;
}

interface RenderedVideoFrame {
  width: number;
  height: number;
  pixelFormat: string;
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
  features: { image_transfer_protocol: false },
  resolutions: [],
  selectedResolutionKey: '',
  opened: false,
  streamId: '',
  streamSessionToken: '',
  frameMeta: null,
  showDetectedRects: false,
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
let lastRenderedFrame: RenderedVideoFrame | null = null;

export const deviceVideoState = readonly(state);

export async function selectDevice(deviceId: string): Promise<void> {
  await closeSelectedDevice();
  state.selectedDeviceId = deviceId;
  state.opened = false;
  state.closeState = 'idle';
  state.features = { image_transfer_protocol: false };
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
  state.features = asFeatures(response.data.features);
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
  if (!state.selectedDeviceId) {
    resetPreview();
    state.opened = false;
    state.closeState = 'idle';
    return;
  }

  const deviceId = state.selectedDeviceId;
  const streamId = state.streamId;
  state.closeState = 'running';
  state.errorMessage = '';

  let response;
  try {
    response = await sendBoundCommand('device.close', {
      params: { device_id: deviceId },
    });
  } catch (error) {
    state.closeState = 'error';
    state.errorMessage = error instanceof Error ? error.message : 'device.close failed';
    return;
  }

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

export async function startVideo(profile?: unknown): Promise<void> {
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
  const params = buildVideoParams(resolution);
  if (profile && typeof profile === 'object') {
    params.profile = profile;
  }
  state.showDetectedRects = shouldShowDetectedRects(profile);
  const response = await sendBoundCommand('video.start', { params });
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

export async function setVideoProfile(profile: unknown): Promise<void> {
  if (!state.selectedDeviceId || !state.streamId) {
    return;
  }
  const response = await sendBoundCommand('video.set_profile', {
    params: {
      device_id: state.selectedDeviceId,
      profile,
    },
  });
  if (!isOkResponse(response)) {
    state.errorMessage = response.message;
    recordAlert({
      name: 'VIDEO_SET_PROFILE_FAILED',
      method: 'video.set_profile',
      code: response.code,
      message: response.message,
    });
    return;
  }
  state.showDetectedRects = shouldShowDetectedRects(profile);
  if (!state.showDetectedRects) {
    clearDetectedRectsPreview();
  }
  recordRuntimeEvent({
    title: 'video.profile_updated',
    detail: `video.set_profile applied to ${state.selectedDeviceId}.`,
    tone: 'info',
  });
}

export async function stopVideo(): Promise<void> {
  closeVideoSocket();
  if (!state.selectedDeviceId || !state.streamId) {
    resetPreview();
    state.stopState = 'idle';
    return;
  }
  state.stopState = 'running';
  let response;
  try {
    response = await sendBoundCommand('video.stop', {
      params: { device_id: state.selectedDeviceId },
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : 'video.stop failed';
    state.stopState = 'error';
    state.errorMessage = message;
    recordRuntimeEvent({
      title: 'video.stop_failed',
      detail: message,
      tone: 'danger',
    });
    return;
  }
  if (!isOkResponse(response)) {
    state.stopState = 'error';
    state.errorMessage = response.message;
    recordRuntimeEvent({
      title: 'video.stop_failed',
      detail: response.message || `code=${response.code}`,
      tone: 'danger',
    });
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
  state.features = { image_transfer_protocol: false };
  state.selectedResolutionKey = '';
  state.opened = false;
  resetPreview();
}

export function handleDeviceRemoved(deviceId: string, reason = 'hotplug_removed'): boolean {
  const selectedDeviceId = state.selectedDeviceId;
  const matched = Boolean(deviceId && selectedDeviceId === deviceId);
  recordInternalRuntimeEvent({
    title: 'device.removed.local',
    detail: matched
      ? `cleared local preview state (${reason}).`
      : `ignored removed device ${deviceId || '-'}; current=${selectedDeviceId || '-'}.`,
    meta: deviceId || selectedDeviceId || '-',
    tone: matched ? 'warning' : 'info',
  });
  if (!matched) {
    return false;
  }
  closeVideoSocket();
  resetPreview();
  state.selectedDeviceId = '';
  state.detailState = 'idle';
  state.opened = false;
  state.resolutions = [];
  state.features = { image_transfer_protocol: false };
  state.selectedResolutionKey = '';
  state.openState = 'idle';
  state.startState = 'idle';
  state.stopState = 'idle';
  state.videoState = 'idle';
  state.closeState = 'success';
  state.errorMessage = `device removed (${reason})`;
  return true;
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

export async function applyCaptureAcquisitionResolution(): Promise<void> {
  if (!state.features.image_transfer_protocol) {
    return;
  }
  const captureResolution = ensureCaptureResolution();
  if (state.selectedResolutionKey !== resolutionKey(captureResolution)) {
    await setSelectedResolution(resolutionKey(captureResolution));
  }
}

function buildVideoParams(resolution: DeviceResolution): Record<string, unknown> {
  return {
    device_id: state.selectedDeviceId,
    width: resolution.width,
    height: resolution.height,
    fps: resolution.fps,
    pixel_format: 'mjpeg',
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

  if (!state.streamId || buffer.byteLength === 0) {
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

  const pixelFormat = normalizeVideoPixelFormat(meta?.pixel_format);
  const width = typeof meta?.width === 'number' && Number.isFinite(meta.width) ? meta.width : 0;
  const height = typeof meta?.height === 'number' && Number.isFinite(meta.height) ? meta.height : 0;
  const expectedBytes = width > 0 && height > 0 ? width * height * 3 : 0;
  if (pixelFormat === 'bgr24' && (expectedBytes <= 0 || buffer.byteLength < expectedBytes)) {
    state.decodeFailedCount += 1;
    state.droppedFrameCount += 1;
    return;
  }
  if (pixelFormat !== 'bgr24' && !isJpegVideoPixelFormat(pixelFormat)) {
    state.decodeFailedCount += 1;
    state.droppedFrameCount += 1;
    return;
  }

  pendingFrame = {
    streamId: frameStreamId,
    frameSeq,
    meta,
    width,
    height,
    pixelFormat,
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

  requestAnimationFrame(() => {
    if (generation !== streamGeneration || frame.streamId !== state.streamId || frame.frameSeq <= state.lastRenderedSeq) {
      state.droppedFrameCount += 1;
      decodingFrame = false;
      processPendingFrame();
      return;
    }
    if (pendingFrame && pendingFrame.frameSeq > frame.frameSeq) {
      state.droppedFrameCount += 1;
      decodingFrame = false;
      processPendingFrame();
      return;
    }
    void drawVideoFrame(frame, frame.meta)
      .then((rendered) => {
        if (!rendered) {
          state.decodeFailedCount += 1;
          state.droppedFrameCount += 1;
          return;
        }
        if (generation !== streamGeneration || frame.streamId !== state.streamId || frame.frameSeq <= state.lastRenderedSeq) {
          state.droppedFrameCount += 1;
          return;
        }
        lastRenderedFrame = rendered;
        state.frameMeta = frame.meta;
        state.renderedFrameCount += 1;
        state.lastRenderedSeq = frame.frameSeq;
        state.lastFrameAt = nowTimeLabel();
      })
      .catch(() => {
        state.decodeFailedCount += 1;
        state.droppedFrameCount += 1;
      })
      .finally(() => {
        decodingFrame = false;
        processPendingFrame();
      });
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
  state.showDetectedRects = false;
  lastRenderedFrame = null;
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

function ensureCaptureResolution(): DeviceResolution {
  const existing = state.resolutions.find((resolution) => resolution.width === 1536 && resolution.height === 1152);
  if (existing) {
    return existing;
  }
  const fallback: DeviceResolution = {
    width: 1536,
    height: 1152,
    real_width: 1536,
    real_height: 1152,
    fps: 15,
    pixel_format: 'mjpeg',
    is_default: false,
  };
  state.resolutions = [fallback, ...state.resolutions];
  return fallback;
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
      pixel_format: normalizeVideoPixelFormat(asString(item.pixel_format)),
      is_default: Boolean(item.is_default),
    }))
    .filter((resolution) => resolution.width > 0 && resolution.height > 0);
}

function asFeatures(value: unknown): DeviceFeatures {
  if (!value || typeof value !== 'object') {
    return { image_transfer_protocol: false };
  }
  const features = value as Record<string, unknown>;
  return {
    image_transfer_protocol: Boolean(features.image_transfer_protocol),
  };
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function asNumber(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function normalizeVideoPixelFormat(value: unknown): string {
  if (value === 'bgr24') {
    return 'bgr24';
  }
  if (value === 'jpeg' || value === 'mjpeg') {
    return value;
  }
  return 'mjpeg';
}

function isJpegVideoPixelFormat(pixelFormat: string): boolean {
  return pixelFormat === 'mjpeg' || pixelFormat === 'jpeg';
}

async function drawVideoFrame(frame: PendingVideoFrame | RenderedVideoFrame, meta: FrameMeta | null): Promise<RenderedVideoFrame | null> {
  if (frame.pixelFormat === 'bgr24') {
    if (!drawBgrFrame(frame.buffer, frame.width, frame.height, meta)) {
      return null;
    }
    return {
      width: frame.width,
      height: frame.height,
      pixelFormat: frame.pixelFormat,
      buffer: frame.buffer.slice(0),
    };
  }
  return drawJpegFrame(frame.buffer, meta);
}

function drawBgrFrame(buffer: ArrayBuffer, width: number, height: number, meta: FrameMeta | null): boolean {
  if (!videoCanvas || !videoContext) {
    return false;
  }
  const expectedBytes = width * height * 3;
  if (width <= 0 || height <= 0 || buffer.byteLength < expectedBytes) {
    return false;
  }

  if (videoCanvas.width !== width || videoCanvas.height !== height) {
    videoCanvas.width = width;
    videoCanvas.height = height;
  }

  const bgr = new Uint8Array(buffer, 0, expectedBytes);
  const imageData = videoContext.createImageData(width, height);
  const rgba = imageData.data;
  for (let source = 0, target = 0; source < expectedBytes; source += 3, target += 4) {
    rgba[target] = bgr[source + 2];
    rgba[target + 1] = bgr[source + 1];
    rgba[target + 2] = bgr[source];
    rgba[target + 3] = 255;
  }
  videoContext.putImageData(imageData, 0, 0);
  drawDetectedRects(meta, width, height);
  return true;
}

async function drawJpegFrame(buffer: ArrayBuffer, meta: FrameMeta | null): Promise<RenderedVideoFrame | null> {
  if (!videoCanvas || !videoContext || buffer.byteLength === 0) {
    return null;
  }

  const blob = new Blob([buffer], { type: 'image/jpeg' });
  if ('createImageBitmap' in window) {
    const bitmap = await createImageBitmap(blob);
    const drawn = drawDecodedImage(bitmap, bitmap.width, bitmap.height, meta);
    bitmap.close();
    if (!drawn) {
      return null;
    }
    return {
      width: drawn.width,
      height: drawn.height,
      pixelFormat: 'mjpeg',
      buffer: buffer.slice(0),
    };
  }

  const image = await loadJpegImage(blob);
  const drawn = drawDecodedImage(image, image.naturalWidth, image.naturalHeight, meta);
  if (!drawn) {
    return null;
  }
  return {
    width: drawn.width,
    height: drawn.height,
    pixelFormat: 'mjpeg',
    buffer: buffer.slice(0),
  };
}

function drawDecodedImage(
  image: CanvasImageSource,
  naturalWidth: number,
  naturalHeight: number,
  meta: FrameMeta | null,
): { width: number; height: number } | null {
  if (!videoCanvas || !videoContext || naturalWidth <= 0 || naturalHeight <= 0) {
    return null;
  }
  const width = typeof meta?.width === 'number' && meta.width > 0 ? meta.width : naturalWidth;
  const height = typeof meta?.height === 'number' && meta.height > 0 ? meta.height : naturalHeight;
  if (videoCanvas.width !== width || videoCanvas.height !== height) {
    videoCanvas.width = width;
    videoCanvas.height = height;
  }
  videoContext.drawImage(image, 0, 0, width, height);
  drawDetectedRects(meta, width, height);
  return { width, height };
}

function loadJpegImage(blob: Blob): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const image = new Image();
    const url = URL.createObjectURL(blob);
    image.onload = () => {
      URL.revokeObjectURL(url);
      resolve(image);
    };
    image.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error('jpeg decode failed'));
    };
    image.src = url;
  });
}

function drawDetectedRects(meta: FrameMeta | null, width: number, height: number): void {
  if (!state.showDetectedRects || !videoContext || !meta?.detected_rects?.length) {
    return;
  }
  const sourceWidth = typeof meta.detected_rects_source?.width === 'number' && meta.detected_rects_source.width > 0
    ? meta.detected_rects_source.width
    : width;
  const sourceHeight = typeof meta.detected_rects_source?.height === 'number' && meta.detected_rects_source.height > 0
    ? meta.detected_rects_source.height
    : height;
  const scaleX = width / sourceWidth;
  const scaleY = height / sourceHeight;

  videoContext.save();
  videoContext.lineWidth = Math.max(3, Math.round(Math.min(width, height) / 240));
  videoContext.strokeStyle = '#22d3ee';
  videoContext.shadowColor = 'rgba(8, 47, 73, 0.65)';
  videoContext.shadowBlur = 6;
  for (const rect of meta.detected_rects) {
    if (!isDetectedRect(rect)) {
      continue;
    }
    videoContext.beginPath();
    videoContext.moveTo(rect.left_top[0] * scaleX, rect.left_top[1] * scaleY);
    videoContext.lineTo(rect.right_top[0] * scaleX, rect.right_top[1] * scaleY);
    videoContext.lineTo(rect.right_down[0] * scaleX, rect.right_down[1] * scaleY);
    videoContext.lineTo(rect.left_down[0] * scaleX, rect.left_down[1] * scaleY);
    videoContext.closePath();
    videoContext.stroke();
  }
  videoContext.restore();
}

function clearDetectedRectsPreview(): void {
  if (state.frameMeta) {
    state.frameMeta = {
      ...state.frameMeta,
      detected_rects: [],
      detected_rects_source: undefined,
    };
  }
  if (pendingFrame?.meta) {
    pendingFrame.meta = {
      ...pendingFrame.meta,
      detected_rects: [],
      detected_rects_source: undefined,
    };
  }
  if (lastRenderedFrame) {
    void drawVideoFrame(lastRenderedFrame, null);
  }
}

function shouldShowDetectedRects(profile: unknown): boolean {
  if (!profile || typeof profile !== 'object') {
    return false;
  }
  const capture = (profile as { capture?: unknown }).capture;
  if (!capture || typeof capture !== 'object') {
    return false;
  }
  const pageProcessing = (capture as { page_processing?: unknown }).page_processing;
  const singlePage = (capture as { single_page?: unknown }).single_page;
  if (!singlePage || typeof singlePage !== 'object') {
    return false;
  }
  return pageProcessing === 'single_page' &&
    (singlePage as { realtime_detect_rects?: unknown }).realtime_detect_rects === true;
}

function isDetectedRect(rect: DetectedRect): rect is Required<DetectedRect> {
  return Array.isArray(rect.left_top) &&
    Array.isArray(rect.right_top) &&
    Array.isArray(rect.right_down) &&
    Array.isArray(rect.left_down);
}

function clearCanvas(): void {
  if (!videoCanvas || !videoContext) {
    return;
  }
  videoContext.clearRect(0, 0, videoCanvas.width, videoCanvas.height);
}
