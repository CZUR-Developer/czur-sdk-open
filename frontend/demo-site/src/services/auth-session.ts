import { reactive, readonly } from 'vue';

import type { ExecutionState } from '../types/demo';
import {
  buildCommandEndpointLabel,
  buildCommandRequest,
  buildCommandWsUrl,
  buildVideoEndpointLabel,
  extractSessionToken,
  isOkResponse,
  type CommandEvent,
  type CommandResponse,
} from './protocol';
import { nowTimeLabel, recordAlert, recordCommandRequest, recordRuntimeEvent } from './runtime-records';
import { CommandWsClient } from './ws-client';

interface DeviceGrant {
  vid: number;
  pid: number;
}

interface AuthContextPayload {
  is_valid?: boolean;
  account_type?: string;
  account_type_code?: number;
  auth_scene?: string;
  license_mode?: string;
  device_scope?: DeviceGrant[];
  expires_at?: number;
  capabilities?: string[];
}

interface RuntimeAuthDiagnostics {
  authzBaseUrlConfigured?: boolean;
  imageEnhanceBaseUrlConfigured?: boolean;
}

interface RuntimeProcessInfo {
  pid?: number;
  executablePath?: string;
}

interface AuthSessionState {
  token: string;
  sessionToken: string;
  sessionExpiresIn: number;
  commandState: ExecutionState;
  refreshState: ExecutionState;
  contextState: ExecutionState;
  connectionEndpoint: string;
  videoEndpoint: string;
  authContext: AuthContextPayload | null;
  authDiagnostics: RuntimeAuthDiagnostics | null;
  runtimeProcess: RuntimeProcessInfo | null;
  lastConnectedAt: string;
  commandCheckedAt: string;
  commandLatencyMs: number;
  commandErrorMessage: string;
}

const STORAGE_KEYS = {
  token: 'sdk-demo-site-token',
  sessionToken: 'sdk-demo-site-session-token',
} as const;

const state = reactive<AuthSessionState>({
  token: loadStoredValue(STORAGE_KEYS.token) ?? 'demo-token-42F8',
  sessionToken: loadStoredValue(STORAGE_KEYS.sessionToken) ?? '',
  sessionExpiresIn: 0,
  commandState: 'idle',
  refreshState: 'idle',
  contextState: 'idle',
  connectionEndpoint: buildCommandEndpointLabel(),
  videoEndpoint: buildVideoEndpointLabel(),
  authContext: null,
  authDiagnostics: null,
  runtimeProcess: null,
  lastConnectedAt: '',
  commandCheckedAt: '',
  commandLatencyMs: 0,
  commandErrorMessage: '',
});

let client: CommandWsClient | null = null;
let activeRunToken = 0;

export const authSessionState = readonly(state);

export async function initializeAuthSession(): Promise<void> {
  activeRunToken += 1;
  const runToken = activeRunToken;

  state.connectionEndpoint = buildCommandEndpointLabel();
  state.videoEndpoint = buildVideoEndpointLabel();
  clearRuntimeState();

  state.commandState = 'running';
  recordRuntimeEvent({
    title: 'command.connecting',
    detail: `Checking ${state.connectionEndpoint} with anonymous command-channel ping.`,
    tone: 'primary',
  });
  disconnectClient('reconnect');

  const nextClient = new CommandWsClient(buildCommandWsUrl());
  client = nextClient;

  try {
    await nextClient.connect();
    if (runToken !== activeRunToken) {
      nextClient.disconnect('stale auth run');
      return;
    }

    state.lastConnectedAt = nowTimeLabel();
    nextClient.onEvent(handleCommandEvent);
    recordRuntimeEvent({
      title: 'command.connected',
      detail: `Command channel connected at ${state.lastConnectedAt}.`,
      tone: 'success',
    });

    const pingOk = await checkCommandConnectivity(nextClient);
    if (runToken !== activeRunToken) {
      nextClient.disconnect('stale auth run');
      return;
    }
    if (!pingOk) {
      state.refreshState = 'blocked';
      state.contextState = 'blocked';
      return;
    }

    await loadRuntimeInfo(nextClient);

    if (!state.token) {
      state.refreshState = 'blocked';
      state.contextState = 'blocked';
      recordRuntimeEvent({
        title: 'auth.blocked',
        detail: 'Command channel is reachable, but token is required before creating a bound session.',
        tone: 'warning',
      });
      return;
    }

    state.refreshState = 'running';
    const createResponse = await sendTrackedCommand(nextClient, 'auth.create_session', {
      params: { token: state.token },
    });
    if (!isOkResponse(createResponse)) {
      clearSession();
      state.refreshState = 'error';
      state.contextState = 'blocked';
      return;
    }

    state.refreshState = 'success';
    state.sessionToken = extractSessionToken(createResponse.data);
    state.sessionExpiresIn = asNumber(createResponse.data.expires_in);
    state.authContext = asAuthContext(createResponse.data.auth_context);
    persistValue(STORAGE_KEYS.sessionToken, state.sessionToken);
    recordRuntimeEvent({
      title: 'auth.session_created',
      detail: `session_token issued through auth.create_session, ttl=${state.sessionExpiresIn}s.`,
      tone: 'success',
    });

    if (!state.sessionToken) {
      clearSession();
      state.refreshState = 'error';
      state.contextState = 'blocked';
      recordAlert({
        name: 'SESSION_TOKEN_MISSING',
        method: 'auth.create_session',
        code: '1900',
        message: 'The runtime created a session response without session_token.',
      });
      return;
    }

    await loadAuthContext(nextClient);
  } catch (error) {
    if (runToken !== activeRunToken) {
      return;
    }

    clearSession();
    state.commandState = 'error';
    state.commandErrorMessage = errorMessage(error);
    state.refreshState = 'blocked';
    state.contextState = 'blocked';
    recordAlert({
      name: 'COMMAND_CONNECT_FAILED',
      method: 'command.connect',
      code: 'NETWORK',
      message: errorMessage(error),
    });
  }
}

export async function refreshSession(): Promise<void> {
  if (!client || state.commandState !== 'success') {
    await initializeAuthSession();
    return;
  }

  state.refreshState = 'running';
  const refreshResponse = await sendTrackedCommand(client, 'auth.refresh_session');
  if (!isOkResponse(refreshResponse)) {
    clearSession();
    state.refreshState = 'error';
    state.contextState = 'blocked';
    return;
  }

  state.refreshState = 'success';
  state.sessionToken = extractSessionToken(refreshResponse.data);
  state.sessionExpiresIn = asNumber(refreshResponse.data.expires_in);
  state.authContext = asAuthContext(refreshResponse.data.auth_context);
  persistValue(STORAGE_KEYS.sessionToken, state.sessionToken);
  recordRuntimeEvent({
    title: 'auth.session_refreshed',
    detail: `session_token rotated with ttl=${state.sessionExpiresIn}s.`,
    tone: 'success',
  });
}

export async function loadAuthContext(activeClient: CommandWsClient | null = client): Promise<void> {
  if (!activeClient) {
    state.contextState = 'blocked';
    return;
  }

  state.contextState = 'running';
  const response = await sendTrackedCommand(activeClient, 'auth.get_context');
  if (!isOkResponse(response)) {
    state.contextState = 'error';
    return;
  }

  state.contextState = 'success';
  state.authContext = asAuthContext(response.data.auth_context);
  recordRuntimeEvent({
    title: 'auth.context_ready',
    detail: 'Auth context loaded from the bound command session.',
    tone: 'info',
  });
  recordRuntimeEvent({
    title: 'auth.bootstrap_completed',
    detail: 'Command connection and bound session are ready for business methods.',
    tone: 'success',
    meta: nowTimeLabel(),
  });
}

export async function checkCommandConnectivity(activeClient: CommandWsClient | null = client): Promise<boolean> {
  if (!activeClient) {
    state.commandState = 'error';
    state.commandErrorMessage = 'command channel not connected';
    return false;
  }

  state.commandState = 'running';
  state.commandErrorMessage = '';
  const startedAt = performance.now();

  try {
    const response = await sendTrackedCommand(activeClient, 'system.ping');
    state.commandLatencyMs = Math.round(performance.now() - startedAt);
    state.commandCheckedAt = nowTimeLabel();
    if (!isOkResponse(response) || response.data.pong !== true) {
      state.commandState = 'error';
      state.commandErrorMessage = response.message || 'system.ping failed';
      return false;
    }

    state.commandState = 'success';
    recordRuntimeEvent({
      title: 'command.ping_ok',
      detail: `system.ping returned pong=true in ${state.commandLatencyMs}ms.`,
      tone: 'success',
      meta: state.commandCheckedAt,
    });
    return true;
  } catch (error) {
    state.commandLatencyMs = Math.round(performance.now() - startedAt);
    state.commandCheckedAt = nowTimeLabel();
    state.commandState = 'error';
    state.commandErrorMessage = errorMessage(error);
    return false;
  }
}

export async function loadRuntimeInfo(activeClient: CommandWsClient | null = client): Promise<void> {
  if (!activeClient) {
    state.authDiagnostics = null;
    state.runtimeProcess = null;
    return;
  }

  const response = await sendTrackedCommand(activeClient, 'system.info');
  if (!isOkResponse(response)) {
    state.authDiagnostics = null;
    state.runtimeProcess = null;
    return;
  }
  state.authDiagnostics = asRuntimeAuthDiagnostics(response.data.authDiagnostics);
  state.runtimeProcess = asRuntimeProcessInfo(response.data.process);
}

export function saveApiKey(value: string): void {
  state.token = value.trim();
  persistValue(STORAGE_KEYS.token, state.token);
}

export function clearApiKey(): void {
  activeRunToken += 1;
  state.token = '';
  persistValue(STORAGE_KEYS.token, '');
  clearSession();
  clearCommandConnectivity();
  disconnectClient('token cleared');
  state.commandState = 'blocked';
  state.refreshState = 'blocked';
  state.contextState = 'blocked';
  recordRuntimeEvent({
    title: 'command.idle',
    detail: 'Cleared token and disconnected the command channel.',
    tone: 'warning',
  });
}

export function disconnectCommandChannel(): void {
  activeRunToken += 1;
  disconnectClient('manual disconnect');
  clearCommandConnectivity();
  state.commandState = state.token ? 'idle' : 'blocked';
}

export async function sendBoundCommand(
  method: string,
  options: { params?: Record<string, unknown> } = {},
): Promise<CommandResponse<Record<string, unknown>>> {
  if (!client || state.commandState !== 'success') {
    if (state.token) {
      await initializeAuthSession();
    }
  }
  if (!client || state.commandState !== 'success') {
    throw new Error('command channel not connected');
  }
  return sendTrackedCommand(client, method, options);
}

export function onCommandEvent(listener: (event: CommandEvent<Record<string, unknown>>) => void): () => void {
  if (!client) {
    return () => {};
  }
  return client.onEvent(listener);
}

function clearRuntimeState(): void {
  clearSession();
  state.authDiagnostics = null;
  state.runtimeProcess = null;
  state.commandState = 'idle';
  state.refreshState = 'idle';
  state.contextState = 'idle';
  clearCommandConnectivity();
}

function clearCommandConnectivity(): void {
  state.lastConnectedAt = '';
  state.commandCheckedAt = '';
  state.commandLatencyMs = 0;
  state.commandErrorMessage = '';
}

function clearSession(): void {
  state.sessionToken = '';
  state.sessionExpiresIn = 0;
  state.authContext = null;
  persistValue(STORAGE_KEYS.sessionToken, '');
}

function disconnectClient(reason: string): void {
  client?.disconnect(reason);
  client = null;
}

function handleCommandEvent(event: CommandEvent<Record<string, unknown>>): void {
  if (!event.event) {
    return;
  }
  if (/^(capture|device|video|stream|sane)\./.test(event.event)) {
    recordRuntimeEvent({
      title: event.event,
      detail: event.message || event.event,
      tone: event.code === 0 ? 'info' : 'danger',
    });
  }
}

async function sendTrackedCommand(
  activeClient: CommandWsClient,
  method: string,
  options: { params?: Record<string, unknown> } = {},
): Promise<CommandResponse<Record<string, unknown>>> {
  const request = buildCommandRequest(method, options);
  const traceId =
    typeof request.client.trace_id === 'string' && request.client.trace_id ? request.client.trace_id : request.request_id;
  const startedAt = performance.now();

  try {
    const response = await activeClient.send(request);
    recordCommandRequest({
      method,
      requestId: request.request_id,
      code: response.code,
      traceId,
      durationMs: performance.now() - startedAt,
    });
    if (!isOkResponse(response)) {
      recordAlert({
        name: statusName(response.code),
        method,
        code: String(response.code),
        message: response.message,
        traceId,
      });
    }
    return response;
  } catch (error) {
    recordCommandRequest({
      method,
      requestId: request.request_id,
      code: 1900,
      traceId,
      durationMs: performance.now() - startedAt,
    });
    recordAlert({
      name: 'COMMAND_SEND_FAILED',
      method,
      code: '1900',
      message: errorMessage(error),
      traceId,
    });
    throw error;
  }
}

function asAuthContext(value: unknown): AuthContextPayload | null {
  if (!value || typeof value !== 'object') {
    return null;
  }
  return value as AuthContextPayload;
}

function asRuntimeAuthDiagnostics(value: unknown): RuntimeAuthDiagnostics | null {
  if (!value || typeof value !== 'object') {
    return null;
  }
  return value as RuntimeAuthDiagnostics;
}

function asRuntimeProcessInfo(value: unknown): RuntimeProcessInfo | null {
  if (!value || typeof value !== 'object') {
    return null;
  }
  return value as RuntimeProcessInfo;
}

function asNumber(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : 'unknown error';
}

function statusName(code: number): string {
  switch (code) {
    case 1100:
      return 'AUTH_REQUIRED';
    case 1101:
      return 'TOKEN_INVALID';
    case 1102:
      return 'TOKEN_EXPIRED';
    case 1103:
      return 'SESSION_TOKEN_INVALID';
    case 1107:
      return 'CAPABILITY_NOT_ALLOWED';
    case 1300:
      return 'STREAM_NOT_FOUND';
    case 1901:
      return 'PROVIDER_NOT_READY';
    default:
      return code === 0 ? 'OK' : 'SDK_ERROR';
  }
}

function loadStoredValue(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

function persistValue(key: string, value: string): void {
  try {
    if (value) {
      localStorage.setItem(key, value);
    } else {
      localStorage.removeItem(key);
    }
  } catch {
    // Ignore storage failures in restricted environments.
  }
}
