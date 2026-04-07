export interface CommandRequestOptions {
  requestId?: string;
  params?: Record<string, unknown>;
  auth?: Record<string, unknown>;
  client?: Record<string, unknown>;
}

export interface CommandRequest {
  request_id: string;
  id: string;
  method: string;
  params: Record<string, unknown>;
  auth: Record<string, unknown>;
  client: Record<string, unknown>;
}

export interface CommandResponse<TData = Record<string, unknown>> {
  code: number;
  message: string;
  data: TData;
  request_id: string;
  id: string;
  ts: number;
}

let requestCounter = 0;
let traceCounter = 0;

function nextCounterValue(prefix: string, counter: number): string {
  return `${prefix}-${Date.now().toString(36)}-${counter.toString(36)}`;
}

export function createRequestId(prefix = 'req'): string {
  requestCounter += 1;
  return nextCounterValue(prefix, requestCounter);
}

export function createTraceId(prefix = 'trc-demo'): string {
  traceCounter += 1;
  return nextCounterValue(prefix, traceCounter);
}

export function buildCommandRequest(method: string, options: CommandRequestOptions = {}): CommandRequest {
  const requestId = options.requestId ?? createRequestId(method.replace(/[^a-z0-9]+/gi, '-').toLowerCase());

  return {
    request_id: requestId,
    id: requestId,
    method,
    params: options.params ?? {},
    auth: options.auth ?? {},
    client: {
      source: 'demo-site',
      protocol_version: '1.0.0',
      trace_id: createTraceId(),
      ...options.client,
    },
  };
}

export function isOkResponse(response: CommandResponse<unknown>): boolean {
  return response.code === 0;
}

export function resolveRuntimeHost(): string {
  return window.location.hostname || '127.0.0.1';
}

export function buildCommandWsUrl(apiKey: string): string {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  const query = new URLSearchParams({ api_key: apiKey });
  return `${protocol}://${resolveRuntimeHost()}:17090?${query.toString()}`;
}

export function buildCommandEndpointLabel(): string {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${protocol}://${resolveRuntimeHost()}:17090`;
}

export function buildCommandProbeUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'https' : 'http';
  return `${protocol}://${resolveRuntimeHost()}:17090/__sdk_open_probe__`;
}

export function buildVideoEndpointLabel(): string {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${protocol}://${resolveRuntimeHost()}:17091`;
}

export function extractSessionKey(data: Record<string, unknown> | null | undefined): string {
  if (!data) {
    return '';
  }

  const sessionKey = data.session_key;
  if (typeof sessionKey === 'string') {
    return sessionKey;
  }

  const sessionToken = data.session_token;
  return typeof sessionToken === 'string' ? sessionToken : '';
}
