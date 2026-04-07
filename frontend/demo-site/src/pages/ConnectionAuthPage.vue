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
import { authActions, authContextFields, authFailureItems, connectionEndpoints } from '../data/demoSite';
import { executionStateLabelKey, executionStateTone } from '../utils/presentation';

const { t } = useI18n();

const authFormItems = computed(() => [
  { label: t('labels.endpoint'), value: 'ws://127.0.0.1:17090', monospace: true },
  { label: t('labels.token'), value: 'demo-session-token', monospace: true },
  { label: t('labels.providerMode'), value: 'hybrid-runtime' },
  { label: t('labels.selectedDevice'), value: 'dev-01' },
  { label: t('labels.handshake'), value: 'bearer + trace header' },
  { label: t('common.status'), value: t('status.success') },
]);

const authContextItems = computed(() => [
  { label: 'is_valid', value: authContextFields.isValid },
  { label: t('labels.accountType'), value: authContextFields.accountType },
  { label: t('labels.licenseMode'), value: authContextFields.licenseMode },
  { label: t('labels.deviceScope'), value: authContextFields.deviceScope },
  { label: t('labels.expiresAt'), value: authContextFields.expiresAt, monospace: true },
  { label: t('labels.capabilities'), value: authContextFields.capabilities },
]);
</script>
