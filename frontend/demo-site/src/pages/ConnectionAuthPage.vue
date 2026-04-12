<template>
  <div class="space-y-6">
    <div class="grid gap-6 xl:grid-cols-[1.2fr_0.8fr]">
      <SectionPanel :title="t('sections.authPanel')" :description="t('common.parameters')">
        <div class="grid gap-4 xl:grid-cols-3">
          <InfoCard
            v-for="action in authActions"
            :key="action.id"
            :eyebrow="action.eyebrow"
            :title="action.title"
            :description="action.description"
            :meta="action.meta"
            :badge-label="action.state ? t(executionStateLabelKey(action.state)) : undefined"
            :badge-tone="action.state ? executionStateTone(action.state) : 'neutral'"
          >
            <template #footer>
              <StatusPill
                :label="t(action.id === 'validate' ? 'actions.validate' : action.id === 'refresh' ? 'actions.refresh' : 'actions.getContext')"
                :tone="action.tone ?? 'neutral'"
              />
            </template>
          </InfoCard>
        </div>

        <div class="mt-4">
          <KeyValueGrid :items="authFormItems" />
        </div>
      </SectionPanel>

      <SectionPanel :title="t('sections.authContext')" :description="t('common.results')">
        <KeyValueGrid :items="authContextItems" />
      </SectionPanel>
    </div>

    <SectionPanel :title="t('sections.authFailures')" :description="t('common.recentErrors')">
      <div class="grid gap-4 xl:grid-cols-3">
        <InfoCard
          v-for="error in authFailureItems"
          :key="error.id"
          :eyebrow="error.method"
          :title="error.name"
          :description="error.message"
          :meta="`${error.occurredAt} · ${error.traceId}`"
          :badge-label="error.code"
          badge-tone="danger"
        />
      </div>
    </SectionPanel>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import KeyValueGrid from '../components/blocks/KeyValueGrid.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState } from '../services/auth-session';
import { runtimeRecordState } from '../services/runtime-records';
import { executionStateLabelKey, executionStateTone } from '../utils/presentation';

const { t } = useI18n();

const authActions = computed(() => [
  {
    id: 'validate',
    eyebrow: 'auth.create_session',
    title: 'Command connect',
    description: authSessionState.token
      ? 'The command lane connects anonymously and creates a bound session through auth.create_session.'
      : 'The command lane stays idle until the browser has a locally stored token.',
    meta: authSessionState.lastConnectedAt
      ? `connected at ${authSessionState.lastConnectedAt}`
      : 'connect first, then create the session in-band',
    tone: 'primary',
    state: authSessionState.commandState,
  },
  {
    id: 'refresh',
    eyebrow: 'auth.refresh_session',
    title: 'Session token issue',
    description: authSessionState.sessionToken
      ? `session_token cached locally, ttl=${authSessionState.sessionExpiresIn}s`
      : 'The runtime returns session_token from auth.create_session, and auth.refresh_session can rotate it later.',
    meta: authSessionState.sessionToken ? maskSecret(authSessionState.sessionToken) : 'session_token not available',
    tone: 'info',
    state: authSessionState.refreshState,
  },
  {
    id: 'context',
    eyebrow: 'Context',
    title: 'Context snapshot',
    description: authSessionState.authContext
      ? 'The runtime resolved account scope, device grants, and capabilities from the bound session token.'
      : 'Context is fetched after session creation and fills the granted capability surface.',
    meta: authSessionState.authContext?.auth_scene || 'awaiting auth context',
    tone: 'success',
    state: authSessionState.contextState,
  },
]);

const authFormItems = computed(() => [
  { label: t('labels.endpoint'), value: authSessionState.connectionEndpoint, monospace: true },
  {
    label: t('labels.apiKey'),
    value: authSessionState.token ? maskSecret(authSessionState.token) : t('common.notSet'),
    monospace: true,
  },
  { label: t('labels.sessionKey'), value: authSessionState.sessionToken ? maskSecret(authSessionState.sessionToken) : t('common.notSet'), monospace: true },
  { label: t('labels.providerMode'), value: 'mock-auth-provider / runtime ws' },
  { label: t('labels.handshake'), value: 'anonymous ws + auth.create_session' },
  { label: t('common.status'), value: t(executionStateLabelKey(authSessionState.commandState)) },
]);

const authContextItems = computed(() => {
  const authContext = authSessionState.authContext;
  if (!authContext) {
    return [
      { label: 'is_valid', value: 'false' },
      { label: t('labels.accountType'), value: t('common.notSet') },
      { label: t('labels.licenseMode'), value: t('common.notSet') },
      { label: t('labels.deviceScope'), value: t('common.notSet') },
      { label: t('labels.expiresAt'), value: t('common.notSet') },
      { label: t('labels.capabilities'), value: t('common.notSet') },
    ];
  }

  const deviceScope = Array.isArray(authContext.device_scope)
    ? authContext.device_scope.map((item) => `${item.vid}:${item.pid}`).join(', ')
    : t('common.notSet');
  const capabilities = Array.isArray(authContext.capabilities)
    ? authContext.capabilities.join(', ')
    : t('common.notSet');

  return [
    { label: 'is_valid', value: String(Boolean(authContext.is_valid)) },
    { label: t('labels.accountType'), value: authContext.account_type ?? t('common.notSet') },
    { label: t('labels.licenseMode'), value: authContext.license_mode ?? t('common.notSet') },
    { label: t('labels.deviceScope'), value: deviceScope || t('common.notSet') },
    {
      label: t('labels.expiresAt'),
      value: typeof authContext.expires_at === 'number' && authContext.expires_at > 0 ? String(authContext.expires_at) : '0',
      monospace: true,
    },
    { label: t('labels.capabilities'), value: capabilities || t('common.notSet') },
  ];
});

const authFailureItems = computed(() =>
  runtimeRecordState.errors
    .filter((error) => error.method.startsWith('auth.') || error.method.startsWith('command.'))
    .slice(0, 3),
);

function maskSecret(value: string): string {
  if (!value) {
    return '';
  }
  if (value.length <= 16) {
    return value;
  }
  return `${value.slice(0, 8)}...${value.slice(-8)}`;
}
</script>
