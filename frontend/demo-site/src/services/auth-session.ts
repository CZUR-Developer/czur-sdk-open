import { reactive, readonly } from 'vue';

import type { ExecutionState } from '../types/demo';
import {
  buildCommandProbeUrl,
  buildCommandEndpointLabel,
  buildCommandRequest,
  buildCommandWsUrl,
  buildVideoEndpointLabel,
  extractSessionKey,
  isOkResponse,
  type CommandEvent,
  type CommandResponse,
} from './protocol';
import { nowTimeLabel, recordAlert, recordCommandRequest, recordRuntimeEvent } from './runtime-records';
import { CommandWsClient, CommandWsConnectError } from './ws-client';

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

interface AuthSessionState {
  apiKey: string;
  sessionKey: string;
  sessionExpiresIn: number;
  commandState: ExecutionState;
  refreshState: ExecutionState;
  contextState: ExecutionState;
  connectionEndpoint: string;
  videoEndpoint: string;
  authContext: AuthContextPayload | null;
  lastConnectedAt: string;
}

interface SessionIssuedPayload {
  session_key?: string;
  session_token?: string;
  expires_in?: number;
  auth_context?: AuthContextPayload;
}

interface HandshakeFailureDiagnosis {
  alertName: string;
  alertCode: string;
  alertMessage: string;
}

const STORAGE_KEYS = {
  apiKey: 'sdk-demo-site-api-key',
  sessionKey: 'sdk-demo-site-session-key',
} as const;

const state = reactive<AuthSessionState>({
  apiKey: loadStoredValue(STORAGE_KEYS.apiKey) ?? 'demo-key-42F8',
  sessionKey: loadStoredValue(STORAGE_KEYS.sessionKey) ?? '',
  sessionExpiresIn: 0,
  commandState: 'idle',
  refreshState: 'idle',
  contextState: 'idle',
  connectionEndpoint: buildCommandEndpointLabel(),
  videoEndpoint: buildVideoEndpointLabel(),
  authContext: null,
  lastConnectedAt: '',
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

  if (!state.apiKey) {
    state.commandState = 'blocked';
    state.refreshState = 'blocked';
    state.contextState = 'blocked';
    recordRuntimeEvent({
      title: 'command.idle',
      detail: 'Waiting for a locally stored API key before opening the command channel.',
      tone: 'warning',
    });
    disconnectClient('api key missing');
    return;
  }

  state.commandState = 'running';
  recordRuntimeEvent({
    title: 'command.connecting',
    detail: `Opening ${state.connectionEndpoint} with api_key handshake.`,
    tone: 'primary',
  });
  disconnectClient('reconnect');

  const nextClient = new CommandWsClient(buildCommandWsUrl(state.apiKey));
  client = nextClient;

  try {
    await nextClient.connect();
    if (runToken !== activeRunToken) {
      nextClient.disconnect('stale auth run');
      return;
    }

    state.commandState = 'success';
    state.lastConnectedAt = nowTimeLabel();
    recordRuntimeEvent({
      title: 'command.connected',
      detail: `Command channel connected at ${state.lastConnectedAt}.`,
      tone: 'success',
    });

    state.refreshState = 'running';
    const sessionIssued = await waitForRuntimeEvent<SessionIssuedPayload>(nextClient, 'auth.session_issued', 2500);
    if (runToken !== activeRunToken) {
      nextClient.disconnect('stale auth run');
      return;
    }
    if (!sessionIssued || sessionIssued.code !== 0) {
      clearSession();
      state.refreshState = 'error';
      state.contextState = 'blocked';
      recordAlert({
        name: 'SESSION_ISSUE_FAILED',
        method: 'auth.session_issued',
        code: String(sessionIssued?.code ?? 1900),
        message: sessionIssued?.message || 'session issue event failed',
      });
      return;
    }

    const payload = asSessionIssuedPayload(sessionIssued.payload);
    state.refreshState = 'success';
    state.sessionKey = extractSessionKey(payload);
    state.sessionExpiresIn = asNumber(payload.expires_in);
    persistValue(STORAGE_KEYS.sessionKey, state.sessionKey);
    state.authContext = asAuthContext(payload.auth_context);
    recordRuntimeEvent({
      title: 'auth.session_issued',
      detail: `session_key issued during command handshake, ttl=${state.sessionExpiresIn}s.`,
      tone: 'success',
    });

    if (!state.sessionKey) {
      clearSession();
      state.refreshState = 'error';
      state.contextState = 'blocked';
      recordAlert({
        name: 'SESSION_KEY_MISSING',
        method: 'auth.session_issued',
        code: '1900',
        message: 'The command lane connected, but no session_key was included in auth.session_issued.',
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
    state.refreshState = 'blocked';
    state.contextState = 'blocked';

    if (error instanceof Error && error.message === 'session issue timeout') {
      recordAlert({
        name: 'SESSION_ISSUE_TIMEOUT',
        method: 'auth.session_issued',
        code: 'TIMEOUT',
        message: 'The command lane connected, but auth.session_issued was not received before timeout.',
      });
      return;
    }

    const diagnosis = await diagnoseHandshakeFailure(state.apiKey, error);
    recordAlert({
      name: diagnosis.alertName,
      method: 'command.connect',
      code: diagnosis.alertCode,
      message: diagnosis.alertMessage,
    });
  }
}

export async function refreshSession(): Promise<void> {
  if (!client || state.commandState !== 'success') {
    await initializeAuthSession();
    return;
  }

  state.refreshState = 'running';
  const refreshResponse = await sendTrackedCommand(client, 'auth.refresh', {
    auth: { api_key: state.apiKey },
  });
  if (!isOkResponse(refreshResponse)) {
    clearSession();
    state.refreshState = 'error';
    state.contextState = 'blocked';
    return;
  }

  state.refreshState = 'success';
  state.sessionKey = extractSessionKey(refreshResponse.data);
  state.sessionExpiresIn = asNumber(refreshResponse.data.expires_in);
  persistValue(STORAGE_KEYS.sessionKey, state.sessionKey);
  state.authContext = asAuthContext(refreshResponse.data.auth_context);
  recordRuntimeEvent({
    title: 'auth.session_issued',
    detail: `session_key rotated with ttl=${state.sessionExpiresIn}s.`,
    tone: 'success',
  });
}

export async function loadAuthContext(activeClient: CommandWsClient | null = client): Promise<void> {
  if (!activeClient) {
    state.contextState = 'blocked';
    return;
  }

  state.contextState = 'running';
  const authPayload = state.sessionKey ? { session_key: state.sessionKey } : { api_key: state.apiKey };
  const response = await sendTrackedCommand(activeClient, 'auth.get_context', { auth: authPayload });
  if (!isOkResponse(response)) {
    state.contextState = 'error';
    return;
  }

  state.contextState = 'success';
  state.authContext = asAuthContext(response.data.auth_context);
  recordRuntimeEvent({
    title: 'auth.context_ready',
    detail: 'Auth context loaded from the runtime command lane.',
    tone: 'info',
  });
  recordRuntimeEvent({
    title: 'auth.validation_completed',
    detail: 'Credential bootstrap completed and session_key is ready for command traffic.',
    tone: 'success',
    meta: nowTimeLabel(),
  });
}

export function saveApiKey(value: string): void {
  state.apiKey = value.trim();
  persistValue(STORAGE_KEYS.apiKey, state.apiKey);
}

export function clearApiKey(): void {
  activeRunToken += 1;
  state.apiKey = '';
  persistValue(STORAGE_KEYS.apiKey, '');
  clearSession();
  disconnectClient('api key cleared');
  state.commandState = 'blocked';
  state.refreshState = 'blocked';
  state.contextState = 'blocked';
  recordRuntimeEvent({
    title: 'command.idle',
    detail: 'Cleared api_key and disconnected the command channel.',
    tone: 'warning',
  });
}

export function disconnectCommandChannel(): void {
  activeRunToken += 1;
  disconnectClient('manual disconnect');
  state.commandState = state.apiKey ? 'idle' : 'blocked';
}

function clearRuntimeState(): void {
  clearSession();
  state.commandState = 'idle';
  state.refreshState = 'idle';
  state.contextState = 'idle';
  state.lastConnectedAt = '';
}

function clearSession(): void {
  state.sessionKey = '';
  state.sessionExpiresIn = 0;
  state.authContext = null;
  persistValue(STORAGE_KEYS.sessionKey, '');
}

function disconnectClient(reason: string): void {
  client?.disconnect(reason);
  client = null;
}

async function sendTrackedCommand(
  activeClient: CommandWsClient,
  method: string,
  options: { params?: Record<string, unknown>; auth?: Record<string, unknown> } = {},
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

function asSessionIssuedPayload(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== 'object') {
    return {};
  }
  return value as Record<string, unknown>;
}

function asNumber(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : 'unknown error';
}

async function diagnoseHandshakeFailure(apiKey: string, error: unknown): Promise<HandshakeFailureDiagnosis> {
  if (!apiKey) {
    return {
      alertName: 'API_KEY_NOT_CONFIGURED',
      alertCode: 'CONFIG',
      alertMessage: 'No api_key is stored locally, so the command channel was not opened.',
    };
  }

  const commandReachable = await probeCommandEndpoint();
  if (!commandReachable) {
    return {
      alertName: 'COMMAND_SERVER_UNREACHABLE',
      alertCode: 'NETWORK',
      alertMessage: `The command lane at ${state.connectionEndpoint} is unreachable. Confirm sdk_open_app is running and listening on port 17090.`,
    };
  }

  if (error instanceof CommandWsConnectError && typeof error.closeCode === 'number' && error.closeCode > 0) {
    return {
      alertName: 'WS_HANDSHAKE_REJECTED',
      alertCode: String(error.closeCode),
      alertMessage: 'The runtime rejected the WebSocket handshake. Check whether the api_key is valid and accepted by the command lane.',
    };
  }

  return {
    alertName: 'WS_HANDSHAKE_REJECTED',
    alertCode: '1101',
    alertMessage: 'The command lane was reachable, but it rejected the WebSocket handshake. Check whether the api_key is valid.',
  };
}

async function probeCommandEndpoint(): Promise<boolean> {
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), 1200);

  try {
    await fetch(buildCommandProbeUrl(), {
      method: 'GET',
      mode: 'no-cors',
      cache: 'no-store',
      signal: controller.signal,
    });
    return true;
  } catch {
    return false;
  } finally {
    window.clearTimeout(timeout);
  }
}

function statusName(code: number): string {
  switch (code) {
    case 1100:
      return 'AUTH_REQUIRED';
    case 1101:
      return 'API_KEY_INVALID';
    case 1102:
      return 'API_KEY_EXPIRED';
    case 1103:
      return 'SESSION_KEY_INVALID';
    case 1107:
      return 'CAPABILITY_NOT_ALLOWED';
    case 1901:
      return 'PROVIDER_NOT_READY';
    default:
      return code === 0 ? 'OK' : 'SDK_ERROR';
  }
}

function waitForRuntimeEvent<TPayload extends Record<string, unknown>>(
  activeClient: CommandWsClient,
  eventName: string,
  timeoutMs: number,
): Promise<CommandEvent<TPayload>> {
  return new Promise<CommandEvent<TPayload>>((resolve, reject) => {
    const timeoutId = window.setTimeout(() => {
      unsubscribe();
      reject(new Error('session issue timeout'));
    }, timeoutMs);

    const unsubscribe = activeClient.onEvent((event) => {
      if (event.event !== eventName) {
        return;
      }
      window.clearTimeout(timeoutId);
      unsubscribe();
      resolve(event as CommandEvent<TPayload>);
    });
  });
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
