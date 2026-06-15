<template>
  <div class="space-y-6">
    <SectionPanel title-key="pages.config.onlineEnhanceTitle" subtitle-key="pages.config.onlineEnhanceSubtitle">
      <div class="grid gap-4 lg:grid-cols-[minmax(0,1fr)_minmax(280px,0.6fr)]">
        <form class="space-y-4" @submit.prevent="saveConfig">
          <label class="block">
            <span class="text-sm font-semibold text-slate-800">{{ t('pages.config.onlineEnhanceBaseUrl') }}</span>
            <input
              v-model.trim="onlineEnhanceBaseUrl"
              class="mt-2 w-full rounded-2xl border border-slate-200 bg-white px-4 py-3 font-mono text-sm text-slate-900 shadow-sm outline-none transition focus:border-sky-400 focus:ring-4 focus:ring-sky-100"
              placeholder="https://gateway-cn.czur.com"
            />
          </label>
          <label class="block">
            <span class="text-sm font-semibold text-slate-800">{{ t('pages.config.authzBaseUrl') }}</span>
            <input
              v-model.trim="authzBaseUrl"
              class="mt-2 w-full rounded-2xl border border-slate-200 bg-white px-4 py-3 font-mono text-sm text-slate-900 shadow-sm outline-none transition focus:border-sky-400 focus:ring-4 focus:ring-sky-100"
              placeholder="https://gateway-cn.czur.com"
            />
          </label>
          <div class="flex flex-wrap gap-3">
            <button type="submit" class="rounded-2xl bg-slate-950 px-4 py-2.5 text-sm font-semibold text-white shadow-sm transition hover:bg-slate-800">
              {{ saving ? t('pages.config.saving') : t('pages.config.save') }}
            </button>
            <button type="button" class="rounded-2xl border border-slate-200 px-4 py-2.5 text-sm font-semibold text-slate-700 transition hover:bg-slate-50" @click="loadConfig">
              {{ t('pages.config.reload') }}
            </button>
          </div>
          <p v-if="errorMessage" class="rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700">{{ errorMessage }}</p>
          <p v-if="savedMessage" class="rounded-2xl border border-emerald-200 bg-emerald-50 px-4 py-3 text-sm text-emerald-700">{{ savedMessage }}</p>
        </form>

        <dl class="grid gap-3 rounded-2xl bg-slate-50 p-4">
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.onlineEnhanceEffectiveBaseUrl') }}</dt>
            <dd class="mt-1 break-all font-mono text-sm text-slate-900">{{ config?.onlineImageEnhance.effectiveBaseUrl || 'https://gateway-cn.czur.com' }}</dd>
          </div>
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.onlineEnhanceSource') }}</dt>
            <dd class="mt-1 font-mono text-sm text-slate-900">{{ config?.onlineImageEnhance.source || 'default' }}</dd>
          </div>
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.authzEffectiveBaseUrl') }}</dt>
            <dd class="mt-1 break-all font-mono text-sm text-slate-900">{{ config?.authz.effectiveBaseUrl || 'https://gateway-cn.czur.com' }}</dd>
          </div>
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.authzSource') }}</dt>
            <dd class="mt-1 font-mono text-sm text-slate-900">{{ config?.authz.source || 'default' }}</dd>
          </div>
        </dl>
      </div>
    </SectionPanel>

    <SectionPanel title-key="pages.config.centralAuthTitle" subtitle-key="pages.config.centralAuthSubtitle">
      <div class="grid gap-4 lg:grid-cols-[minmax(0,1fr)_minmax(280px,0.6fr)]">
        <div class="space-y-4">
          <label class="block">
            <span class="text-sm font-semibold text-slate-800">{{ t('pages.config.centralAuthBaseUrl') }}</span>
            <input
              v-model.trim="centralAuthBaseUrl"
              class="mt-2 w-full rounded-2xl border border-slate-200 bg-white px-4 py-3 font-mono text-sm text-slate-900 shadow-sm outline-none transition focus:border-sky-400 focus:ring-4 focus:ring-sky-100"
              placeholder="http://license-hub.local:18080"
            />
          </label>

          <div class="flex flex-wrap gap-3">
            <button
              type="button"
              class="rounded-2xl border border-slate-200 px-4 py-2.5 text-sm font-semibold text-slate-700 transition hover:bg-slate-50 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="!centralAuthBaseUrl || verifyingCentralAuth"
              @click="verifyCentralAuth"
            >
              {{ verifyingCentralAuth ? t('pages.config.centralAuthVerifying') : t('pages.config.centralAuthVerify') }}
            </button>
            <span
              v-if="centralAuthVerifyMessage"
              class="rounded-2xl px-4 py-2.5 text-sm font-semibold"
              :class="centralAuthVerifyOk ? 'bg-emerald-50 text-emerald-700' : 'bg-rose-50 text-rose-700'"
            >
              {{ centralAuthVerifyMessage }}
            </span>
          </div>
        </div>

        <dl class="grid gap-3 rounded-2xl bg-slate-50 p-4">
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.centralAuthStatus') }}</dt>
            <dd class="mt-1 font-mono text-sm text-slate-900">{{ centralAuthBaseUrl ? t('pages.config.centralAuthOn') : t('pages.config.centralAuthOff') }}</dd>
          </div>
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.centralAuthFlow') }}</dt>
            <dd class="mt-1 text-sm text-slate-600">{{ t('pages.config.centralAuthFlowText') }}</dd>
          </div>
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.centralAuthGuardrail') }}</dt>
            <dd class="mt-1 text-sm text-slate-600">{{ t('pages.config.centralAuthGuardrailText') }}</dd>
          </div>
        </dl>
      </div>

      <div class="mt-6 border-t border-slate-200 pt-5">
        <div class="flex flex-col gap-3 lg:flex-row lg:items-start lg:justify-between">
          <div>
            <h3 class="text-base font-semibold text-slate-950">{{ t('pages.config.centralActivationTitle') }}</h3>
            <p class="mt-1 text-sm text-slate-500">{{ t('pages.config.centralActivationSubtitle') }}</p>
          </div>
          <button
            type="button"
            class="rounded-2xl border border-slate-200 px-4 py-2.5 text-sm font-semibold text-slate-700 transition hover:bg-slate-50 disabled:cursor-not-allowed disabled:opacity-60"
            :disabled="loadingAuthSessions"
            @click="loadAuthSessions"
          >
            {{ loadingAuthSessions ? t('pages.config.centralActivationLoadingSessions') : t('pages.config.centralActivationReloadSessions') }}
          </button>
        </div>

        <div class="mt-4 grid gap-4 lg:grid-cols-[minmax(0,1fr)_minmax(280px,0.6fr)]">
          <div class="space-y-4">
            <div class="rounded-2xl border border-slate-200 bg-white p-4">
              <label class="block">
                <span class="text-sm font-semibold text-slate-800">{{ t('pages.config.centralActivationApiKey') }}</span>
                <input
                  v-model.trim="offlineApiKey"
                  type="password"
                  autocomplete="off"
                  class="mt-2 w-full rounded-2xl border border-slate-200 bg-white px-4 py-3 font-mono text-sm text-slate-900 shadow-sm outline-none transition focus:border-sky-400 focus:ring-4 focus:ring-sky-100"
                  :placeholder="t('pages.config.centralActivationApiKeyPlaceholder')"
                />
              </label>
              <div class="mt-3 flex flex-wrap items-center gap-3">
                <button
                  type="button"
                  class="rounded-2xl bg-slate-950 px-4 py-2.5 text-sm font-semibold text-white shadow-sm transition hover:bg-slate-800 disabled:cursor-not-allowed disabled:opacity-60"
                  :disabled="creatingSdkSession || requestingCentralActivation || pollingCentralActivation"
                  @click="createOfflineApiKeySession"
                >
                  {{ creatingSdkSession ? t('pages.config.centralActivationCreatingSession') : t('pages.config.centralActivationCreateSession') }}
                </button>
                <span v-if="activeCommandSessionMachineCode" class="break-all rounded-2xl bg-emerald-50 px-4 py-2.5 font-mono text-xs font-semibold text-emerald-700">
                  {{ activeCommandSessionMachineCode }}
                </span>
              </div>
            </div>

            <label class="block">
              <span class="text-sm font-semibold text-slate-800">{{ t('pages.config.centralActivationSession') }}</span>
              <select
                v-model="selectedActivationConnectionId"
                class="mt-2 w-full rounded-2xl border border-slate-200 bg-white px-4 py-3 text-sm text-slate-900 shadow-sm outline-none transition focus:border-sky-400 focus:ring-4 focus:ring-sky-100 disabled:bg-slate-50"
                :disabled="!activationSessions.length || requestingCentralActivation || pollingCentralActivation"
              >
                <option value="">{{ t('pages.config.centralActivationSelectSession') }}</option>
                <option v-for="session in activationSessions" :key="session.connectionId" :value="session.connectionId">
                  {{ activationSessionLabel(session) }}
                </option>
              </select>
            </label>

            <div class="grid gap-3 md:grid-cols-2">
              <div class="rounded-2xl bg-slate-50 px-4 py-3">
                <div class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.centralActivationMachineCode') }}</div>
                <div class="mt-2 break-all font-mono text-sm text-slate-900">{{ selectedActivationSession?.machineCode || '-' }}</div>
              </div>
              <div class="rounded-2xl bg-slate-50 px-4 py-3">
                <div class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.centralActivationHubUrl') }}</div>
                <div class="mt-2 break-all font-mono text-sm text-slate-900">{{ centralAuthBaseUrl || '-' }}</div>
              </div>
            </div>

            <div class="flex flex-wrap gap-3">
              <button
                type="button"
                class="rounded-2xl bg-slate-950 px-4 py-2.5 text-sm font-semibold text-white shadow-sm transition hover:bg-slate-800 disabled:cursor-not-allowed disabled:opacity-60"
                :disabled="!selectedActivationSession || !centralAuthBaseUrl || requestingCentralActivation || pollingCentralActivation"
                @click="submitCentralActivationRequest"
              >
                {{ requestingCentralActivation ? t('pages.config.centralActivationRequesting') : t('pages.config.centralActivationRequest') }}
              </button>
              <button
                type="button"
                class="rounded-2xl border border-slate-200 px-4 py-2.5 text-sm font-semibold text-slate-700 transition hover:bg-slate-50 disabled:cursor-not-allowed disabled:opacity-60"
                :disabled="!centralActivationRequestId || !centralActivationPollToken || pollingCentralActivation"
                @click="pollCentralActivationRequest"
              >
                {{ pollingCentralActivation ? t('pages.config.centralActivationPolling') : t('pages.config.centralActivationPoll') }}
              </button>
              <span
                v-if="centralActivationMessage"
                class="rounded-2xl px-4 py-2.5 text-sm font-semibold"
                :class="centralActivationError ? 'bg-rose-50 text-rose-700' : 'bg-emerald-50 text-emerald-700'"
              >
                {{ centralActivationMessage }}
              </span>
            </div>

            <p v-if="!activationSessions.length" class="rounded-2xl bg-amber-50 px-4 py-3 text-sm text-amber-800">
              {{ t('pages.config.centralActivationNoSessions') }}
            </p>
          </div>

          <dl class="grid gap-3 rounded-2xl bg-slate-50 p-4">
            <div>
              <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.centralActivationRequestId') }}</dt>
              <dd class="mt-1 break-all font-mono text-sm text-slate-900">{{ centralActivationRequestId || '-' }}</dd>
            </div>
            <div>
              <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.centralActivationRequestStatus') }}</dt>
              <dd class="mt-1 font-mono text-sm text-slate-900">{{ centralActivationStatus || '-' }}</dd>
            </div>
            <div>
              <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.centralActivationSecurity') }}</dt>
              <dd class="mt-1 text-sm text-slate-600">{{ t('pages.config.centralActivationSecurityText') }}</dd>
            </div>
          </dl>
        </div>
      </div>
    </SectionPanel>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import SectionPanel from '../components/blocks/SectionPanel.vue';
import type { AdminAuthSession, RuntimeConfig } from '../services/admin-api';
import { fetchAuth, fetchConfig, saveRuntimeConfig } from '../services/admin-api';
import { activateCurrentSdkCommandSession, createSdkCommandSession, hasCurrentSdkCommandSessionFor } from '../services/command-session';
import { fetchHubServiceInfo, pollOfflineActivation, requestOfflineActivation, verifyLicenseHub } from '../services/license-hub-client';

const { t } = useI18n();

const onlineEnhanceBaseUrl = ref('');
const authzBaseUrl = ref('');
const centralAuthBaseUrl = ref('');
const verifyingCentralAuth = ref(false);
const centralAuthVerifyMessage = ref('');
const centralAuthVerifyOk = ref(false);
const config = ref<RuntimeConfig | null>(null);
const saving = ref(false);
const errorMessage = ref('');
const savedMessage = ref('');
const authSessions = ref<AdminAuthSession[]>([]);
const loadingAuthSessions = ref(false);
const selectedActivationConnectionId = ref('');
const offlineApiKey = ref('');
const creatingSdkSession = ref(false);
const activeCommandSessionMachineCode = ref('');
const requestingCentralActivation = ref(false);
const pollingCentralActivation = ref(false);
const centralActivationRequestId = ref('');
const centralActivationPollToken = ref('');
const centralActivationStatus = ref('');
const centralActivationMessage = ref('');
const centralActivationError = ref(false);

interface StoredActivationState {
  hubBaseUrl: string;
  serviceId: string;
  machineCode: string;
  idempotencyKey: string;
  requestId: string;
  pollToken: string;
  lastStatus: string;
  savedAt: number;
}

const activationSessions = computed(() => authSessions.value.filter((session) => session.offlineActivationRequired));
const selectedActivationSession = computed(() =>
  activationSessions.value.find((session) => session.connectionId === selectedActivationConnectionId.value) ?? null,
);

function normalizeHubBaseUrl(baseUrl: string): string {
  return baseUrl.trim().replace(/\/+$/, '');
}

function activationStorageKey(hubBaseUrl: string, machineCode: string): string {
  return `sdk-admin-central-activation:${normalizeHubBaseUrl(hubBaseUrl)}:${machineCode}`;
}

function readStoredActivationState(hubBaseUrl: string, machineCode: string): StoredActivationState | null {
  if (!hubBaseUrl || !machineCode) {
    return null;
  }
  try {
    const payload = window.localStorage.getItem(activationStorageKey(hubBaseUrl, machineCode));
    if (!payload) {
      return null;
    }
    const parsed = JSON.parse(payload) as Partial<StoredActivationState>;
    if (!parsed.requestId || !parsed.pollToken || parsed.machineCode !== machineCode) {
      return null;
    }
    return {
      hubBaseUrl: parsed.hubBaseUrl ?? normalizeHubBaseUrl(hubBaseUrl),
      serviceId: parsed.serviceId ?? '',
      machineCode,
      idempotencyKey: parsed.idempotencyKey ?? '',
      requestId: parsed.requestId,
      pollToken: parsed.pollToken,
      lastStatus: parsed.lastStatus ?? '',
      savedAt: typeof parsed.savedAt === 'number' ? parsed.savedAt : 0,
    };
  } catch {
    return null;
  }
}

function saveStoredActivationState(state: StoredActivationState): void {
  window.localStorage.setItem(activationStorageKey(state.hubBaseUrl, state.machineCode), JSON.stringify(state));
}

function removeStoredActivationState(hubBaseUrl: string, machineCode: string): void {
  if (!hubBaseUrl || !machineCode) {
    return;
  }
  window.localStorage.removeItem(activationStorageKey(hubBaseUrl, machineCode));
}

function clearActivationRequestState(): void {
  centralActivationRequestId.value = '';
  centralActivationPollToken.value = '';
  centralActivationStatus.value = '';
}

function restoreActivationStateForSession(session: AdminAuthSession | null): void {
  clearActivationRequestState();
  if (!session) {
    return;
  }
  const stored = readStoredActivationState(centralAuthBaseUrl.value, session.machineCode);
  if (!stored) {
    return;
  }
  centralActivationRequestId.value = stored.requestId;
  centralActivationPollToken.value = stored.pollToken;
  centralActivationStatus.value = stored.lastStatus;
}

async function resolveHubServiceId(): Promise<string> {
  try {
    const info = await fetchHubServiceInfo(centralAuthBaseUrl.value);
    return info.serviceId ?? '';
  } catch {
    return '';
  }
}

function buildActivationIdempotencyKey(session: AdminAuthSession, serviceId: string): string {
  return `sdk-admin|${serviceId || normalizeHubBaseUrl(centralAuthBaseUrl.value)}|${session.machineCode}`;
}

function selectedSessionNeedsCurrentCommandSession(session: AdminAuthSession): boolean {
  return !hasCurrentSdkCommandSessionFor(session.machineCode);
}

async function activateApprovedAuthCode(session: AdminAuthSession, authCode: string): Promise<void> {
  if (selectedSessionNeedsCurrentCommandSession(session)) {
    throw new Error(t('pages.config.centralActivationCommandSessionRequired'));
  }
  await activateCurrentSdkCommandSession(authCode);
  removeStoredActivationState(centralAuthBaseUrl.value, session.machineCode);
  clearActivationRequestState();
  centralActivationMessage.value = t('pages.config.centralActivationSucceeded');
  await loadAuthSessions();
}

async function loadConfig(): Promise<void> {
  errorMessage.value = '';
  savedMessage.value = '';
  const nextConfig = await fetchConfig();
  config.value = nextConfig;
  onlineEnhanceBaseUrl.value = nextConfig.onlineImageEnhance.baseUrl;
  authzBaseUrl.value = nextConfig.authz.baseUrl;
  centralAuthBaseUrl.value = nextConfig.centralAuth.baseUrl;
}

async function verifyCentralAuth(): Promise<void> {
  verifyingCentralAuth.value = true;
  centralAuthVerifyMessage.value = '';
  centralAuthVerifyOk.value = false;
  try {
    const result = await verifyLicenseHub(centralAuthBaseUrl.value);
    centralAuthVerifyOk.value = Boolean(result.ok);
    centralAuthVerifyMessage.value = result.ok ? t('pages.config.centralAuthVerifyOk') : result.message;
  } catch (error) {
    centralAuthVerifyOk.value = false;
    centralAuthVerifyMessage.value = error instanceof Error ? error.message : String(error);
  } finally {
    verifyingCentralAuth.value = false;
  }
}

async function loadAuthSessions(): Promise<void> {
  loadingAuthSessions.value = true;
  try {
    const auth = await fetchAuth();
    authSessions.value = auth.sessions ?? [];
    if (!activationSessions.value.some((session) => session.connectionId === selectedActivationConnectionId.value)) {
      const activeMachineSession = activationSessions.value.find((session) => session.machineCode === activeCommandSessionMachineCode.value);
      selectedActivationConnectionId.value = activeMachineSession?.connectionId ?? activationSessions.value[0]?.connectionId ?? '';
    }
    restoreActivationStateForSession(selectedActivationSession.value);
  } catch (error) {
    centralActivationError.value = true;
    centralActivationMessage.value = error instanceof Error ? error.message : String(error);
  } finally {
    loadingAuthSessions.value = false;
  }
}

async function createOfflineApiKeySession(): Promise<void> {
  if (!offlineApiKey.value) {
    centralActivationError.value = true;
    centralActivationMessage.value = t('pages.config.centralActivationApiKeyRequired');
    return;
  }

  creatingSdkSession.value = true;
  centralActivationError.value = false;
  centralActivationMessage.value = '';
  try {
    const result = await createSdkCommandSession(offlineApiKey.value);
    offlineApiKey.value = '';
    activeCommandSessionMachineCode.value = result.authContext.machine_code ?? '';
    await loadAuthSessions();
    const matchedSession = activationSessions.value.find((session) => session.machineCode === activeCommandSessionMachineCode.value);
    if (matchedSession) {
      selectedActivationConnectionId.value = matchedSession.connectionId;
      restoreActivationStateForSession(matchedSession);
      centralActivationMessage.value = t('pages.config.centralActivationSessionCreated');
      return;
    }
    centralActivationMessage.value = t('pages.config.centralActivationSessionNotRequired');
  } catch (error) {
    centralActivationError.value = true;
    centralActivationMessage.value = error instanceof Error ? error.message : String(error);
  } finally {
    creatingSdkSession.value = false;
  }
}

function activationSessionLabel(session: AdminAuthSession): string {
  const type = session.apiKeyType || 'API Key';
  const state = session.entitlementState || 'unknown';
  const machineCode = session.machineCode || '-';
  return `${type} · ${state} · ${machineCode}`;
}

async function submitCentralActivationRequest(): Promise<void> {
  const session = selectedActivationSession.value;
  if (!session) {
    centralActivationError.value = true;
    centralActivationMessage.value = t('pages.config.centralActivationSelectSessionRequired');
    return;
  }
  if (!centralAuthBaseUrl.value) {
    centralActivationError.value = true;
    centralActivationMessage.value = t('pages.config.centralActivationHubUrlRequired');
    return;
  }
  if (selectedSessionNeedsCurrentCommandSession(session)) {
    centralActivationError.value = true;
    centralActivationMessage.value = t('pages.config.centralActivationCommandSessionRequired');
    return;
  }

  requestingCentralActivation.value = true;
  centralActivationError.value = false;
  centralActivationMessage.value = '';
  centralActivationRequestId.value = '';
  centralActivationPollToken.value = '';
  centralActivationStatus.value = '';
  try {
    const serviceId = await resolveHubServiceId();
    const idempotencyKey = buildActivationIdempotencyKey(session, serviceId);
    const result = await requestOfflineActivation(centralAuthBaseUrl.value, {
      machineCode: session.machineCode,
      clientName: 'sdk_admin_site',
      clientVersion: '0.1.0',
      hostName: window.location.hostname || undefined,
      idempotencyKey,
      metadata: {
        source: 'sdk_open_admin_config',
        connectionId: session.connectionId,
        apiKeyType: session.apiKeyType,
      },
    });
    centralActivationRequestId.value = result.requestId;
    centralActivationStatus.value = result.status;

    if (result.authCode) {
      await activateApprovedAuthCode(session, result.authCode);
      return;
    }

    const stored = readStoredActivationState(centralAuthBaseUrl.value, session.machineCode);
    const pollToken = result.pollToken || stored?.pollToken || '';
    centralActivationPollToken.value = pollToken;
    if (pollToken) {
      saveStoredActivationState({
        hubBaseUrl: normalizeHubBaseUrl(centralAuthBaseUrl.value),
        serviceId,
        machineCode: session.machineCode,
        idempotencyKey,
        requestId: result.requestId,
        pollToken,
        lastStatus: result.status,
        savedAt: Date.now(),
      });
      centralActivationMessage.value = result.message || t('pages.config.centralActivationRequestSubmitted');
      return;
    }

    centralActivationError.value = true;
    centralActivationMessage.value = t('pages.config.centralActivationMissingPollToken');
  } catch (error) {
    centralActivationError.value = true;
    centralActivationMessage.value = error instanceof Error ? error.message : String(error);
  } finally {
    requestingCentralActivation.value = false;
  }
}

async function pollCentralActivationRequest(): Promise<void> {
  const session = selectedActivationSession.value;
  if (!session) {
    centralActivationError.value = true;
    centralActivationMessage.value = t('pages.config.centralActivationSelectSessionRequired');
    return;
  }
  if (!centralActivationPollToken.value) {
    centralActivationError.value = true;
    centralActivationMessage.value = t('pages.config.centralActivationMissingPollToken');
    return;
  }
  if (selectedSessionNeedsCurrentCommandSession(session)) {
    centralActivationError.value = true;
    centralActivationMessage.value = t('pages.config.centralActivationCommandSessionRequired');
    return;
  }

  pollingCentralActivation.value = true;
  centralActivationError.value = false;
  centralActivationMessage.value = '';
  try {
    const result = await pollOfflineActivation(centralAuthBaseUrl.value, centralActivationRequestId.value, centralActivationPollToken.value);
    centralActivationStatus.value = result.status;
    const normalizedStatus = String(result.status || '').toUpperCase();
    if (normalizedStatus === 'APPROVED' && result.authCode) {
      await activateApprovedAuthCode(session, result.authCode);
      return;
    }
    if (centralActivationRequestId.value && centralActivationPollToken.value) {
      const stored = readStoredActivationState(centralAuthBaseUrl.value, session.machineCode);
      saveStoredActivationState({
        hubBaseUrl: normalizeHubBaseUrl(centralAuthBaseUrl.value),
        serviceId: stored?.serviceId ?? '',
        machineCode: session.machineCode,
        idempotencyKey: stored?.idempotencyKey ?? '',
        requestId: centralActivationRequestId.value,
        pollToken: centralActivationPollToken.value,
        lastStatus: result.status,
        savedAt: Date.now(),
      });
    }
    centralActivationMessage.value = result.message || t('pages.config.centralActivationStatusMessage', { status: result.status });
  } catch (error) {
    centralActivationError.value = true;
    centralActivationMessage.value = error instanceof Error ? error.message : String(error);
  } finally {
    pollingCentralActivation.value = false;
  }
}

async function saveConfig(): Promise<void> {
  saving.value = true;
  errorMessage.value = '';
  savedMessage.value = '';
  try {
    const nextConfig = await saveRuntimeConfig({
      onlineImageEnhanceBaseUrl: onlineEnhanceBaseUrl.value,
      authzBaseUrl: authzBaseUrl.value,
      centralAuth: {
        baseUrl: centralAuthBaseUrl.value,
      },
    });
    config.value = nextConfig;
    onlineEnhanceBaseUrl.value = nextConfig.onlineImageEnhance.baseUrl;
    authzBaseUrl.value = nextConfig.authz.baseUrl;
    centralAuthBaseUrl.value = nextConfig.centralAuth.baseUrl;
    savedMessage.value = t('pages.config.saved');
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  } finally {
    saving.value = false;
  }
}

watch([selectedActivationSession, centralAuthBaseUrl], ([session]) => {
  restoreActivationStateForSession(session);
});

onMounted(() => {
  loadConfig().catch((error) => {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  });
  loadAuthSessions();
});
</script>
