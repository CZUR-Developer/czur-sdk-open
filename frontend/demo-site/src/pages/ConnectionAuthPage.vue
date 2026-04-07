<template>
  <div class="space-y-6">
    <SectionPanel :title="t('sections.channelPanels')" :description="t('pages.connectionAuth.subtitle')">
      <div class="grid gap-4 xl:grid-cols-2">
        <InfoCard
          v-for="endpoint in connectionEndpoints"
          :key="endpoint.id"
          :eyebrow="endpoint.protocol"
          :title="endpoint.title"
          :description="endpoint.note"
          :meta="endpoint.url"
          :badge-label="t(executionStateLabelKey(endpoint.state))"
          :badge-tone="executionStateTone(endpoint.state)"
        >
          <template #footer>
            <StatusPill :label="t(endpoint.state === 'success' ? 'actions.disconnect' : 'actions.connect')" tone="primary" />
            <StatusPill :label="t('labels.handshake')" tone="info" />
          </template>
        </InfoCard>
      </div>
    </SectionPanel>

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

const connectionEndpoints = computed(() => [
  {
    id: 'command-lane',
    title: 'Command Lane',
    url: authSessionState.connectionEndpoint,
    protocol: 'WS',
    note: describeCommandLane(),
    state: authSessionState.commandState,
  },
  {
    id: 'video-lane',
    title: 'Video Lane',
    url: authSessionState.videoEndpoint,
    protocol: 'WS',
    note: authSessionState.sessionKey
      ? 'Waiting for a stream subscription after the auth bootstrap completed.'
      : 'Video lane is unchanged in this phase and still waits for a later subscription flow.',
    state: authSessionState.sessionKey ? 'running' : 'planned',
  },
]);

const authActions = computed(() => [
  {
    id: 'validate',
    eyebrow: 'ws://...?...api_key=***',
    title: 'Command handshake',
    description: authSessionState.apiKey
      ? 'Connection-level API key validation now runs before the command lane opens.'
      : 'The command lane stays idle until the browser has a locally stored API key.',
    meta: authSessionState.lastConnectedAt
      ? `connected at ${authSessionState.lastConnectedAt}`
      : 'handshake happens before auth.refresh',
    tone: 'primary',
    state: authSessionState.commandState,
  },
  {
    id: 'refresh',
    eyebrow: 'auth.refresh',
    title: 'Session key issue',
    description: authSessionState.sessionKey
      ? `session_key received and cached locally, ttl=${authSessionState.sessionExpiresIn}s`
      : 'The first command after connect exchanges api_key for a short-lived session_key.',
    meta: authSessionState.sessionKey || 'session_key not available',
    tone: 'info',
    state: authSessionState.refreshState,
  },
  {
    id: 'context',
    eyebrow: 'auth.get_context',
    title: 'Context snapshot',
    description: authSessionState.authContext
      ? 'The runtime resolved account scope, device grants, and capabilities from the session key.'
      : 'Context is fetched after session issue and fills the granted capability surface.',
    meta: authSessionState.authContext?.auth_scene || 'awaiting auth context',
    tone: 'success',
    state: authSessionState.contextState,
  },
]);

const authFormItems = computed(() => [
  { label: t('labels.endpoint'), value: authSessionState.connectionEndpoint, monospace: true },
  {
    label: t('labels.apiKey'),
    value: authSessionState.apiKey ? `${authSessionState.apiKey.slice(0, 4)}••••${authSessionState.apiKey.slice(-4)}` : t('common.notSet'),
    monospace: true,
  },
  { label: t('labels.sessionKey'), value: authSessionState.sessionKey || t('common.notSet'), monospace: true },
  { label: t('labels.providerMode'), value: 'mock-auth-provider / runtime ws' },
  { label: t('labels.handshake'), value: 'ws query api_key' },
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

function describeCommandLane(): string {
  switch (authSessionState.commandState) {
    case 'success':
      return authSessionState.sessionKey
        ? 'api_key handshake succeeded and auth.refresh already returned a session_key.'
        : 'api_key handshake succeeded and auth.refresh is the next step.';
    case 'running':
      return 'Opening the command channel and waiting for the runtime to validate the api_key.';
    case 'blocked':
      return 'Set an API key locally first, then the page will reconnect and issue auth.refresh automatically.';
    case 'error':
      return 'The runtime rejected the command lane handshake or the follow-up auth bootstrap failed.';
    default:
      return 'The page initializes the command lane before any interactive demo action runs.';
  }
}
</script>
