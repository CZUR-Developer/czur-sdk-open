export interface CommandRequestOptions {
  params?: Record<string, unknown>;
  client?: Record<string, unknown>;
}

export interface CommandRequest {
  request_id: string;
  method: string;
  params: Record<string, unknown>;
  client: Record<string, unknown>;
}

export interface CommandResponse<TData = Record<string, unknown>> {
  code: number;
  message: string;
  data: TData;
  request_id: string;
  ts: number;
}

export interface CommandAuthContext {
  is_valid?: boolean;
  account_type?: string;
  license_mode?: string;
  host_auth_mode?: string;
  entitlement_state?: string;
  machine_code?: string;
  capabilities?: string[];
}

export interface CommandSessionResult {
  sessionToken: string;
  expiresIn: number;
  authContext: CommandAuthContext;
}

interface PendingRequest {
  resolve: (response: CommandResponse<Record<string, unknown>>) => void;
  reject: (error: Error) => void;
}

let requestCounter = 0;
let traceCounter = 0;
let activeClient: CommandWsClient | null = null;
let activeAuthContext: CommandAuthContext | null = null;

function nextCounterValue(prefix: string, counter: number): string {
  return `${prefix}-${Date.now().toString(36)}-${counter.toString(36)}`;
}

function createRequestId(method: string): string {
  requestCounter += 1;
  return nextCounterValue(method.replace(/[^a-z0-9]+/gi, '-').toLowerCase(), requestCounter);
}

function createTraceId(): string {
  traceCounter += 1;
  return nextCounterValue('trc-admin', traceCounter);
}

function buildCommandRequest(method: string, options: CommandRequestOptions = {}): CommandRequest {
  return {
    request_id: createRequestId(method),
    method,
    params: options.params ?? {},
    client: {
      source: 'admin-site',
      protocol_version: '2.0.0',
      trace_id: createTraceId(),
      ...options.client,
    },
  };
}

function buildCommandWsUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  const host = window.location.hostname || '127.0.0.1';
  return `${protocol}://${host}:17090`;
}

function asObject(value: unknown): Record<string, unknown> {
  return value && typeof value === 'object' && !Array.isArray(value) ? (value as Record<string, unknown>) : {};
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function asNumber(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function asAuthContext(value: unknown): CommandAuthContext {
  const payload = asObject(value);
  return {
    is_valid: Boolean(payload.is_valid),
    account_type: asString(payload.account_type),
    license_mode: asString(payload.license_mode),
    host_auth_mode: asString(payload.host_auth_mode),
    entitlement_state: asString(payload.entitlement_state),
    machine_code: asString(payload.machine_code),
    capabilities: Array.isArray(payload.capabilities) ? payload.capabilities.filter((item): item is string => typeof item === 'string') : [],
  };
}

function asSessionResult(data: Record<string, unknown>): CommandSessionResult {
  return {
    sessionToken: asString(data.session_token),
    expiresIn: asNumber(data.expires_in),
    authContext: asAuthContext(data.auth_context),
  };
}

function assertOkResponse(response: CommandResponse<Record<string, unknown>>): void {
  if (response.code !== 0) {
    throw new Error(response.message || `command failed: ${response.code}`);
  }
}

class CommandWsClient {
  private readonly url: string;
  private socket: WebSocket | null = null;
  private connectPromise: Promise<void> | null = null;
  private pendingRequests = new Map<string, PendingRequest>();

  constructor(url: string) {
    this.url = url;
  }

  async connect(): Promise<void> {
    if (this.socket?.readyState === WebSocket.OPEN) {
      return;
    }
    if (this.connectPromise) {
      return this.connectPromise;
    }

    this.connectPromise = new Promise<void>((resolve, reject) => {
      const socket = new WebSocket(this.url);
      let settled = false;

      socket.addEventListener('open', () => {
        this.socket = socket;
        settled = true;
        resolve();
      });

      socket.addEventListener('message', (event) => {
        this.handleMessage(event.data);
      });

      socket.addEventListener('error', () => {
        if (!settled) {
          settled = true;
          reject(new Error('command websocket connect failed'));
        }
      });

      socket.addEventListener('close', (event) => {
        if (this.socket === socket) {
          this.socket = null;
        }
        this.rejectPendingRequests(new Error(event.reason || 'command websocket closed'));
        if (!settled) {
          settled = true;
          reject(new Error(event.reason || 'command websocket connect failed'));
        }
      });
    }).finally(() => {
      this.connectPromise = null;
    });

    return this.connectPromise;
  }

  disconnect(reason = 'manual disconnect'): void {
    const socket = this.socket;
    this.socket = null;
    if (socket && socket.readyState < WebSocket.CLOSING) {
      socket.close(1000, reason);
    }
    this.rejectPendingRequests(new Error(reason));
  }

  isConnected(): boolean {
    return this.socket?.readyState === WebSocket.OPEN;
  }

  async send(method: string, options: CommandRequestOptions = {}): Promise<CommandResponse<Record<string, unknown>>> {
    if (this.socket?.readyState !== WebSocket.OPEN) {
      throw new Error('command websocket not connected');
    }

    const request = buildCommandRequest(method, options);
    return new Promise<CommandResponse<Record<string, unknown>>>((resolve, reject) => {
      this.pendingRequests.set(request.request_id, { resolve, reject });
      this.socket?.send(JSON.stringify(request));
    });
  }

  private handleMessage(data: unknown): void {
    if (typeof data !== 'string') {
      return;
    }

    let payload: Record<string, unknown>;
    try {
      payload = JSON.parse(data) as Record<string, unknown>;
    } catch {
      return;
    }

    const requestId = asString(payload.request_id);
    if (!requestId) {
      return;
    }

    const pendingRequest = this.pendingRequests.get(requestId);
    if (!pendingRequest) {
      return;
    }

    this.pendingRequests.delete(requestId);
    pendingRequest.resolve(payload as CommandResponse<Record<string, unknown>>);
  }

  private rejectPendingRequests(error: Error): void {
    for (const [, pendingRequest] of this.pendingRequests) {
      pendingRequest.reject(error);
    }
    this.pendingRequests.clear();
  }
}

async function activeCommandClient(): Promise<CommandWsClient> {
  if (!activeClient) {
    activeClient = new CommandWsClient(buildCommandWsUrl());
  }
  await activeClient.connect();
  return activeClient;
}

export async function createSdkCommandSession(apiKey: string): Promise<CommandSessionResult> {
  if (!apiKey.trim()) {
    throw new Error('offline API Key is required');
  }

  if (activeClient) {
    activeClient.disconnect('replace command session');
    activeClient = null;
    activeAuthContext = null;
  }

  const client = await activeCommandClient();
  const response = await client.send('auth.create_session', {
    params: { token: apiKey.trim() },
  });
  assertOkResponse(response);
  const result = asSessionResult(response.data ?? {});
  activeAuthContext = result.authContext;
  return result;
}

export async function activateCurrentSdkCommandSession(authCode: string): Promise<CommandSessionResult> {
  if (!authCode.trim()) {
    throw new Error('authorization code is required');
  }
  if (!activeClient?.isConnected() || !activeAuthContext?.machine_code) {
    throw new Error('create an offline SDK session on this page before activation');
  }

  const client = await activeCommandClient();
  const response = await client.send('auth.activate_offline', {
    params: { auth_code: authCode.trim() },
  });
  assertOkResponse(response);
  const result = asSessionResult(response.data ?? {});
  activeAuthContext = result.authContext;
  return result;
}

export async function refreshCurrentSdkCommandContext(): Promise<CommandSessionResult> {
  const client = await activeCommandClient();
  const response = await client.send('auth.get_context');
  assertOkResponse(response);
  const result = asSessionResult(response.data ?? {});
  activeAuthContext = result.authContext;
  return result;
}

export function currentSdkCommandMachineCode(): string {
  return activeAuthContext?.machine_code ?? '';
}

export function hasCurrentSdkCommandSessionFor(machineCode: string): boolean {
  return Boolean(machineCode) && Boolean(activeClient?.isConnected()) && currentSdkCommandMachineCode() === machineCode;
}
