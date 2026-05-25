<template>
  <div class="space-y-6">
    <div v-if="errorMessage" class="rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700">{{ errorMessage }}</div>

    <SectionPanel title-key="sections.authSurface" subtitle-key="sections.authSurfaceHint">
      <div class="grid gap-3 md:grid-cols-2 xl:grid-cols-4">
        <div v-for="provider in providerRows" :key="provider.label" class="rounded-2xl bg-slate-50 px-4 py-4">
          <div class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ provider.label }}</div>
          <div class="mt-2 break-all font-mono text-sm font-semibold text-slate-950">{{ provider.value || '-' }}</div>
        </div>
      </div>
    </SectionPanel>

    <SectionPanel title-key="sections.authTimeline" subtitle-key="sections.authTimelineHint">
      <div class="space-y-4">
        <article v-for="session in sessions" :key="session.connectionId" class="rounded-3xl border border-slate-200 p-5">
          <div class="flex flex-col gap-3 lg:flex-row lg:items-start lg:justify-between">
            <div>
              <p class="font-mono text-xs text-slate-500">{{ session.connectionId }}</p>
              <h3 class="mt-2 text-lg font-semibold text-slate-950">{{ session.apiKeyType }} · {{ session.entitlementState || 'unknown' }}</h3>
              <p class="mt-1 text-sm text-slate-600">{{ session.licenseMode || '-' }} / {{ session.hostAuthMode || '-' }}</p>
            </div>
            <span class="rounded-full px-3 py-1 text-sm font-semibold" :class="session.offlineActivationRequired ? 'bg-amber-50 text-amber-700' : 'bg-emerald-50 text-emerald-700'">
              {{ session.offlineActivationRequired ? 'activation required' : 'granted' }}
            </span>
          </div>

          <dl class="mt-5 grid gap-3 md:grid-cols-2 xl:grid-cols-4">
            <div class="rounded-2xl bg-slate-50 px-4 py-3">
              <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">API Key</dt>
              <dd class="mt-2 break-all font-mono text-sm text-slate-900">{{ session.maskedToken || '-' }}</dd>
            </div>
            <div class="rounded-2xl bg-slate-50 px-4 py-3">
              <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('auth.apiKeyVipType') }}</dt>
              <dd class="mt-2 font-mono text-sm text-slate-900">{{ accountTierLabel(session) }}</dd>
            </div>
            <div class="rounded-2xl bg-slate-50 px-4 py-3">
              <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">Machine code</dt>
              <dd class="mt-2 break-all font-mono text-sm text-slate-900">{{ session.machineCode || '-' }}</dd>
            </div>
            <div class="rounded-2xl bg-slate-50 px-4 py-3">
              <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">Device scope</dt>
              <dd class="mt-2 font-mono text-sm text-slate-900">{{ session.deviceScopeCount }}</dd>
            </div>
          </dl>

          <div class="mt-5 rounded-2xl bg-slate-50 px-4 py-4">
            <div class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">Capabilities</div>
            <div class="mt-3 flex flex-wrap gap-2">
              <span v-for="capability in session.capabilities" :key="capability" class="rounded-full bg-white px-3 py-1 font-mono text-xs text-slate-700 shadow-sm">{{ capability }}</span>
              <span v-if="!session.capabilities.length" class="text-sm text-slate-500">No capabilities granted.</span>
            </div>
          </div>

          <div v-if="session.offlineActivationRequired" class="mt-5 grid gap-4 lg:grid-cols-[176px_minmax(0,1fr)]">
            <div class="flex size-44 items-center justify-center rounded-2xl border border-slate-200 bg-white p-3">
              <Qrcode
                :value="activationQrValue(session)"
                type="image/png"
                :size="152"
                :margin="1"
                :color="{ dark: '#020617', light: '#ffffff' }"
              />
            </div>
            <form class="rounded-2xl bg-amber-50 px-4 py-4" @submit.prevent="submitOfflineActivation(session)">
              <div class="text-sm font-semibold text-amber-900">{{ t('auth.offlineActivation') }}</div>
              <label class="mt-4 block">
                <span class="text-xs font-semibold uppercase tracking-[0.16em] text-amber-800">{{ t('auth.authCode') }}</span>
                <input
                  v-model.trim="authCodeByConnection[session.connectionId]"
                  class="mt-2 w-full rounded-xl border border-amber-200 bg-white px-3 py-2 font-mono text-sm text-slate-950 outline-none transition focus:border-amber-500 focus:ring-2 focus:ring-amber-200"
                  :placeholder="t('auth.authCodePlaceholder')"
                  autocomplete="off"
                />
              </label>
              <div class="mt-3 flex flex-wrap items-center gap-3">
                <button
                  type="submit"
                  class="rounded-xl bg-slate-950 px-4 py-2 text-sm font-semibold text-white shadow-sm transition hover:bg-slate-800 disabled:cursor-not-allowed disabled:opacity-60"
                  :disabled="Boolean(activatingByConnection[session.connectionId])"
                >
                  {{ activatingByConnection[session.connectionId] ? t('auth.activating') : t('auth.activate') }}
                </button>
                <span
                  v-if="activationMessageByConnection[session.connectionId]"
                  class="text-sm font-medium"
                  :class="activationErrorByConnection[session.connectionId] ? 'text-rose-700' : 'text-emerald-700'"
                >
                  {{ activationMessageByConnection[session.connectionId] }}
                </span>
              </div>
            </form>
          </div>
        </article>

        <div v-if="!sessions.length" class="rounded-2xl bg-slate-50 px-4 py-8 text-center text-sm text-slate-500">
          No active authorization sessions. Create a session from the Demo site to inspect API Key type, capabilities, and offline activation state.
        </div>
      </div>
    </SectionPanel>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import Qrcode from 'vue-qrcode';

import SectionPanel from '../components/blocks/SectionPanel.vue';
import type { AdminAuth, AdminAuthSession } from '../services/admin-api';
import { activateOfflineSession, fetchAuth } from '../services/admin-api';

const auth = ref<AdminAuth | null>(null);
const errorMessage = ref('');
const authCodeByConnection = ref<Record<string, string>>({});
const activatingByConnection = ref<Record<string, boolean>>({});
const activationMessageByConnection = ref<Record<string, string>>({});
const activationErrorByConnection = ref<Record<string, boolean>>({});
const { t } = useI18n();

const providerRows = computed(() =>
  Object.entries(auth.value?.providers ?? {}).map(([label, value]) => ({ label, value })),
);

const sessions = computed(() => auth.value?.sessions ?? []);

function activationQrValue(session: AdminAuthSession): string {
  if (session.activationQrUrl) {
    return session.activationQrUrl;
  }
  const payload = session.activationPayload || session.machineCode;
  if (!payload) {
    return '';
  }
  if (/^https?:\/\//i.test(payload)) {
    return payload;
  }
  const params = new URLSearchParams({ code: payload });
  return `https://rainbow.czur.com/mp/auth/code?${params.toString()}`;
}

function formatTier(tier: string | undefined, code: number | undefined): string {
  if (!tier) {
    return '-';
  }
  const label = tier.replace(/_/g, ' ').toUpperCase();
  return typeof code === 'number' ? `${label} (${code})` : label;
}

function accountTierLabel(session: AdminAuthSession): string {
  const apiKeyTier = formatTier(session.accountType, session.accountTypeCode);
  const licensedTier = formatTier(session.licensedAccountType, session.licensedAccountTypeCode);
  if (!session.licensedAccountType || session.licensedAccountType === session.accountType) {
    return apiKeyTier;
  }
  return `${apiKeyTier} / ${t('auth.licensedVipType')}: ${licensedTier}`;
}

async function submitOfflineActivation(session: AdminAuthSession): Promise<void> {
  const connectionId = session.connectionId;
  const authCode = (authCodeByConnection.value[connectionId] ?? '').trim();
  if (!authCode) {
    activationErrorByConnection.value[connectionId] = true;
    activationMessageByConnection.value[connectionId] = t('auth.authCodeRequired');
    return;
  }

  activatingByConnection.value[connectionId] = true;
  activationErrorByConnection.value[connectionId] = false;
  activationMessageByConnection.value[connectionId] = '';
  try {
    await activateOfflineSession(connectionId, authCode);
    authCodeByConnection.value[connectionId] = '';
    activationMessageByConnection.value[connectionId] = t('auth.activationSucceeded');
    await load();
  } catch (error) {
    activationErrorByConnection.value[connectionId] = true;
    activationMessageByConnection.value[connectionId] = activationErrorMessage(error, session);
  } finally {
    activatingByConnection.value[connectionId] = false;
  }
}

function activationErrorMessage(error: unknown, session: AdminAuthSession): string {
  const message = error instanceof Error ? error.message : String(error);
  if (message.includes('offline auth code tier is lower than api key tier')) {
    return t('auth.authCodeTierTooLow', { tier: accountTierLabel(session) });
  }
  return message;
}

async function load(): Promise<void> {
  try {
    errorMessage.value = '';
    auth.value = await fetchAuth();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  }
}

onMounted(() => {
  load();
  window.setInterval(load, 5000);
});
</script>
