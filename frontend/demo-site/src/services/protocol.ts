export interface CommandRequestOptions {
  requestId?: string;
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

export interface CommandEvent<TPayload = Record<string, unknown>> {
  event: string;
  code: number;
  message: string;
  payload: TPayload;
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
    method,
    params: options.params ?? {},
    client: {
      source: 'demo-site',
      protocol_version: '2.0.0',
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

export function resolveDemoHttpPort(): number {
  const parsed = Number.parseInt(window.location.port, 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : 17081;
}

export function resolveAssetHttpPort(): number {
  return resolveDemoHttpPort() + 1;
}

export function resolveCommandWsPort(): number {
  return resolveDemoHttpPort() + 9;
}

export function resolveVideoWsPort(): number {
  return resolveDemoHttpPort() + 10;
}

export function buildAssetApiUrl(path: string): string {
  const protocol = window.location.protocol === 'https:' ? 'https' : 'http';
  return `${protocol}://${resolveRuntimeHost()}:${resolveAssetHttpPort()}${path.startsWith('/') ? path : `/${path}`}`;
}

export function buildCommandWsUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${protocol}://${resolveRuntimeHost()}:${resolveCommandWsPort()}`;
}

export function buildCommandEndpointLabel(): string {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${protocol}://${resolveRuntimeHost()}:${resolveCommandWsPort()}`;
}

export function buildCommandProbeUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'https' : 'http';
  return `${protocol}://${resolveRuntimeHost()}:${resolveCommandWsPort()}/__sdk_open_probe__`;
}

export function buildVideoEndpointLabel(): string {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${protocol}://${resolveRuntimeHost()}:${resolveVideoWsPort()}`;
}

export function buildVideoWsUrl(sessionToken: string, streamId: string): string {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  const params = new URLSearchParams({
    session_token: sessionToken,
    stream_id: streamId,
  });
  return `${protocol}://${resolveRuntimeHost()}:${resolveVideoWsPort()}?${params.toString()}`;
}

export function extractSessionToken(data: Record<string, unknown> | null | undefined): string {
  if (!data) {
    return '';
  }
  const sessionToken = data.session_token;
  return typeof sessionToken === 'string' ? sessionToken : '';
}
