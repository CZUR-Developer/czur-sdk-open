export interface HubServiceInfo {
  serviceName?: string;
  serviceType?: string;
  apiBasePath?: string;
  version?: string;
  serviceId?: string;
}

export interface OfflineActivationRequestBody {
  machineCode: string;
  clientName?: string;
  clientVersion?: string;
  hostName?: string;
  idempotencyKey?: string;
  metadata?: Record<string, string>;
}

export interface OfflineActivationRequestResult {
  requestId: string;
  pollToken?: string | null;
  status: string;
  message?: string;
  createdAt?: string;
  authCode?: string | null;
}

export interface OfflineActivationPollResult {
  requestId: string;
  status: string;
  message?: string;
  authCode?: string | null;
  updatedAt?: string;
}

export interface HubVerifyResult {
  ok: boolean;
  message: string;
  health: unknown;
  serviceInfo: HubServiceInfo;
}

function normalizeHubBaseUrl(baseUrl: string): string {
  const normalized = baseUrl.trim().replace(/\/+$/, '');
  if (!normalized) {
    throw new Error('License Hub URL is required.');
  }
  return normalized;
}

async function requestHubJson<T>(baseUrl: string, path: string, init: RequestInit = {}): Promise<T> {
  const headers = new Headers(init.headers);
  headers.set('Accept', 'application/json');
  const response = await fetch(`${normalizeHubBaseUrl(baseUrl)}${path}`, { ...init, headers });
  if (!response.ok) {
    let message = `${init.method ?? 'GET'} ${path} ${response.status}`;
    try {
      const errorPayload = (await response.json()) as { message?: string; error?: string };
      message = errorPayload.message || errorPayload.error || message;
    } catch {
      // Keep transport-level fallback when the response is not JSON.
    }
    throw new Error(message);
  }
  return (await response.json()) as T;
}

export function fetchHubServiceInfo(baseUrl: string): Promise<HubServiceInfo> {
  return requestHubJson<HubServiceInfo>(baseUrl, '/api/v1/central-auth/service-info');
}

export async function verifyLicenseHub(baseUrl: string): Promise<HubVerifyResult> {
  const health = await requestHubJson<unknown>(baseUrl, '/health');
  const serviceInfo = await fetchHubServiceInfo(baseUrl);
  return {
    ok: true,
    message: 'ok',
    health,
    serviceInfo,
  };
}

export function requestOfflineActivation(baseUrl: string, body: OfflineActivationRequestBody): Promise<OfflineActivationRequestResult> {
  return requestHubJson<OfflineActivationRequestResult>(baseUrl, '/api/v1/central-auth/offline-activation-requests', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
}

export function pollOfflineActivation(baseUrl: string, requestId: string, pollToken: string): Promise<OfflineActivationPollResult> {
  const query = new URLSearchParams({ pollToken });
  return requestHubJson<OfflineActivationPollResult>(
    baseUrl,
    `/api/v1/central-auth/offline-activation-requests/${encodeURIComponent(requestId)}?${query.toString()}`,
  );
}
