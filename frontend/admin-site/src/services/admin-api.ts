export interface AdminStatus {
  running?: boolean;
  uptimeSec?: number;
  bindHost?: string;
  process?: {
    pid?: number;
    executablePath?: string;
  };
  http?: {
    enabled?: boolean;
    sites?: Record<string, number>;
    assetBaseUrl?: string;
  };
  ports?: Record<string, number>;
  providers?: Record<string, string>;
  authDiagnostics?: Record<string, boolean>;
  ws?: {
    command?: Record<string, number>;
    video?: Record<string, number>;
  };
  application?: Record<string, number>;
}

export interface AdminSystem {
  software?: Record<string, string>;
  process?: Record<string, string | number>;
  os?: Record<string, string>;
  hardware?: Record<string, string | number>;
  runtime?: Record<string, string | boolean>;
  ports?: Record<string, number>;
}

export interface AdminAuthSession {
  connectionId: string;
  apiKeyType: string;
  maskedToken: string;
  maskedSessionToken: string;
  licenseMode: string;
  hostAuthMode: string;
  accountType: string;
  accountTypeCode?: number;
  licensedAccountType?: string;
  licensedAccountTypeCode?: number;
  entitlementState: string;
  machineCode: string;
  offlineActivationRequired: boolean;
  activationPayload: string;
  activationQrUrl?: string;
  createdAt: number;
  lastSeenAt: number;
  expiresAt: number;
  expiresIn: number;
  capabilities: string[];
  capabilityCount: number;
  deviceScopeCount: number;
  quotaBuckets: Array<Record<string, string | number>>;
}

export interface AdminAuth {
  providers?: Record<string, string>;
  activeSessionCount?: number;
  sessions?: AdminAuthSession[];
  capabilityCatalog?: {
    methods?: Array<{ method: string; requires_session: boolean; summary: string }>;
  };
}

export interface AdminLogFile {
  id: string;
  label: string;
  path: string;
  exists: boolean;
  size: number;
  mtime: number;
}

export interface AdminLogs {
  logDir?: string;
  logs?: AdminLogFile[];
}

export interface AdminLogContent {
  code: number;
  message: string;
  id: string;
  path: string;
  size: number;
  mtime: number;
  tailBytes: number;
  content: string;
}

export interface AdminRecord {
  id: number;
  ts: number;
  type: 'request' | 'event' | 'error' | string;
  source: string;
  connectionId: string;
  method: string;
  requestId: string;
  status: string;
  code: number;
  message: string;
  durationMs: number;
  payloadPreview: string;
}

export interface AdminRecords {
  records?: AdminRecord[];
  totalRetained?: number;
  capacity?: number;
}

export interface EndpointConfig {
  baseUrl: string;
  effectiveBaseUrl: string;
  source: string;
}

export interface CentralAuthConfig {
  baseUrl: string;
}

export interface RuntimeConfig {
  onlineImageEnhance: EndpointConfig;
  authz: EndpointConfig;
  centralAuth: CentralAuthConfig;
}

interface ConfigResponse {
  online_image_enhance?: {
    base_url?: string;
    effective_base_url?: string;
    source?: string;
  };
  authz?: {
    base_url?: string;
    effective_base_url?: string;
    source?: string;
  };
  central_auth?: {
    base_url?: string;
  };
}

function adminToken(): string {
  const params = new URLSearchParams(window.location.search);
  const queryToken = params.get('auth_token') ?? params.get('token');
  if (queryToken) {
    window.localStorage.setItem('sdk_admin_auth_token', queryToken);
    return queryToken;
  }
  return window.localStorage.getItem('sdk_admin_auth_token') ?? '';
}

async function requestJson<T>(path: string, init: RequestInit = {}): Promise<T> {
  const token = adminToken();
  const headers = new Headers(init.headers);
  headers.set('Accept', 'application/json');
  if (token) {
    headers.set('Authorization', `Bearer ${token}`);
  }
  const response = await fetch(path, { ...init, headers });
  if (!response.ok) {
    let message = `${init.method ?? 'GET'} ${path} ${response.status}`;
    try {
      const errorPayload = (await response.json()) as { message?: string };
      if (errorPayload.message) {
        message = errorPayload.message;
      }
    } catch {
      // Keep the transport-level fallback when the response is not JSON.
    }
    throw new Error(message);
  }
  return (await response.json()) as T;
}

function normalizeEndpoint(payload: { base_url?: string; effective_base_url?: string; source?: string } | undefined): EndpointConfig {
  return {
    baseUrl: payload?.base_url ?? '',
    effectiveBaseUrl: payload?.effective_base_url ?? '',
    source: payload?.source ?? 'default',
  };
}

function normalizeConfig(payload: ConfigResponse): RuntimeConfig {
  return {
    onlineImageEnhance: normalizeEndpoint(payload.online_image_enhance),
    authz: normalizeEndpoint(payload.authz),
    centralAuth: {
      baseUrl: payload.central_auth?.base_url ?? '',
    },
  };
}

export function fetchStatus(): Promise<AdminStatus> {
  return requestJson<AdminStatus>('/api/status');
}

export function fetchSystem(): Promise<AdminSystem> {
  return requestJson<AdminSystem>('/api/system');
}

export function fetchAuth(): Promise<AdminAuth> {
  return requestJson<AdminAuth>('/api/auth');
}

export function activateOfflineSession(connectionId: string, authCode: string): Promise<{ code: number; message: string; session?: AdminAuthSession }> {
  return requestJson<{ code: number; message: string; session?: AdminAuthSession }>(
    `/api/auth/sessions/${encodeURIComponent(connectionId)}/offline-activation`,
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ auth_code: authCode }),
    },
  );
}

export function fetchLogs(): Promise<AdminLogs> {
  return requestJson<AdminLogs>('/api/logs');
}

export function fetchLogContent(id: string, tailBytes = 262144): Promise<AdminLogContent> {
  return requestJson<AdminLogContent>(`/api/logs/${encodeURIComponent(id)}?tailBytes=${tailBytes}`);
}

export function fetchRecords(): Promise<AdminRecords> {
  return requestJson<AdminRecords>('/api/records');
}

export async function fetchConfig(): Promise<RuntimeConfig> {
  return normalizeConfig(await requestJson<ConfigResponse>('/api/config'));
}

export async function saveRuntimeConfig(config: { onlineImageEnhanceBaseUrl: string; authzBaseUrl: string; centralAuth: CentralAuthConfig }): Promise<RuntimeConfig> {
  const payload = await requestJson<ConfigResponse>('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      online_image_enhance: { base_url: config.onlineImageEnhanceBaseUrl },
      authz: { base_url: config.authzBaseUrl },
      central_auth: { base_url: config.centralAuth.baseUrl },
    }),
  });
  return normalizeConfig(payload);
}
