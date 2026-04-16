<template>
  <div class="space-y-6">
    <div class="grid gap-4 xl:grid-cols-3">
      <MetricCard
        v-for="metric in deviceMetrics"
        :key="metric.id"
        :label="t(metric.labelKey)"
        :value="metric.value"
        :detail="metric.detail"
        :trend="metric.trend"
        :tone="metric.tone"
      />
    </div>

    <SectionPanel :title="t('sections.deviceInventory')" :description="t('pages.deviceVideo.subtitle')">
      <template #actions>
        <button
          type="button"
          class="inline-flex items-center rounded-2xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-700 shadow-sm transition hover:border-slate-300 hover:text-slate-950 disabled:cursor-not-allowed disabled:opacity-60"
          :disabled="deviceInventoryState.listState === 'running'"
          @click="handleRefresh"
        >
          {{ t('actions.refresh') }}
        </button>
      </template>

      <DataTableCard :columns="deviceColumns" :rows="deviceRows">
        <template #empty>
          <div class="space-y-2">
            <p class="text-base font-semibold text-slate-700">
              {{ deviceInventoryEmptyTitle }}
            </p>
            <p>{{ deviceInventoryEmptyDescription }}</p>
          </div>
        </template>
      </DataTableCard>
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-[1.1fr_0.9fr]">
      <SectionPanel :title="t('sections.deviceControl')" :description="t('common.actions')">
        <div class="grid gap-4 xl:grid-cols-2">
          <InfoCard
            v-for="card in [...deviceControlCards, ...streamCards]"
            :key="card.id"
            :eyebrow="card.eyebrow"
            :title="card.title"
            :description="card.description"
            :meta="card.meta"
            :badge-label="card.state ? t(executionStateLabelKey(card.state)) : undefined"
            :badge-tone="card.state ? executionStateTone(card.state) : 'neutral'"
          >
            <template #footer>
              <StatusPill
                v-for="tag in card.tags"
                :key="tag"
                :label="t(requirementTagLabelKey(tag))"
                :tone="requirementTagTone(tag)"
              />
            </template>
          </InfoCard>
        </div>
      </SectionPanel>

      <div class="space-y-6">
        <SectionPanel :title="t('sections.videoPreview')" :description="t('common.preview')">
          <PreviewStage
            :eyebrow="t('labels.selectedDevice')"
            title="CZUR ET18 Pro preview lane"
            description="Live mock preview keeps frame cadence, lane posture, and current resolution visible without binding to a real camera."
            :metrics="videoPreviewMetrics"
            :badge-label="t('actions.subscribe')"
            badge-tone="primary"
            variant="video"
          />
        </SectionPanel>

        <SectionPanel :title="t('sections.realtimeEvents')" :description="t('common.timeline')">
          <EventTimeline :items="deviceVideoEvents" />
        </SectionPanel>
      </div>
    </div>

    <JsonPanel
      :title="sharedJsonSnippets.deviceVideo.title"
      :caption="sharedJsonSnippets.deviceVideo.caption"
      :payload="sharedJsonSnippets.deviceVideo.payload"
    />
  </div>
</template>

<script setup lang="ts">
import { computed, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import DataTableCard from '../components/blocks/DataTableCard.vue';
import EventTimeline from '../components/blocks/EventTimeline.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import PreviewStage from '../components/blocks/PreviewStage.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import MetricCard from '../components/cards/MetricCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState } from '../services/auth-session';
import { deviceInventoryState, loadDeviceInventory, resetDeviceInventory } from '../services/device-inventory';
import {
  deviceControlCards,
  deviceVideoEvents,
  sharedJsonSnippets,
  streamCards,
  videoPreviewMetrics,
} from '../data/demoSite';
import { executionStateLabelKey, executionStateTone, requirementTagLabelKey, requirementTagTone } from '../utils/presentation';

const { t } = useI18n();

const deviceMetrics = computed(() => [
  {
    id: 'online-devices',
    value: String(deviceInventoryState.count),
    labelKey: 'sections.deviceInventory',
    detail:
      deviceInventoryState.listState === 'running'
        ? 'Refreshing the latest device inventory from runtime.'
        : 'Devices currently reachable from demo runtime',
    trend: deviceInventoryState.lastLoadedAt
      ? `updated at ${deviceInventoryState.lastLoadedAt}`
      : deviceInventoryState.listState === 'error'
        ? 'inventory load failed'
        : 'awaiting first inventory request',
    tone: deviceInventoryState.listState === 'error' ? 'warning' : 'success',
  },
  {
    id: 'frame-rate',
    value: '29.7fps',
    labelKey: 'labels.resolution',
    detail: 'Current stream cadence measured on frame meta events',
    trend: '1920 x 1080',
    tone: 'primary',
  },
  {
    id: 'event-lag',
    value: '18ms',
    labelKey: 'common.timeline',
    detail: 'Synthetic event fan-out delay from provider to demo page',
    trend: 'well within target',
    tone: 'warning',
  },
]);

const deviceColumns = computed(() => [
  { key: 'deviceId', label: 'device_id', monospace: true },
  { key: 'model', label: t('labels.model') },
  { key: 'displayName', label: 'display_name' },
  { key: 'vid', label: 'vid', monospace: true },
  { key: 'pid', label: 'pid', monospace: true },
  { key: 'status', label: t('labels.status') },
  { key: 'authorized', label: t('labels.auth') },
  { key: 'supportsVideo', label: t('labels.supportsVideo') },
]);

const deviceRows = computed(() => deviceInventoryState.rows);

const deviceInventoryEmptyTitle = computed(() => {
  if (deviceInventoryState.listState === 'running') {
    return t('common.loading');
  }
  if (deviceInventoryState.listState === 'error') {
    return t('common.loadFailed');
  }
  return t('common.noDevicesYet');
});

const deviceInventoryEmptyDescription = computed(() => {
  if (deviceInventoryState.listState === 'running') {
    return t('common.loadingDevices');
  }
  if (deviceInventoryState.listState === 'error') {
    return deviceInventoryState.errorMessage || t('common.retryLater');
  }
  return t('common.noDevicesDetected');
});

async function handleRefresh() {
  await loadDeviceInventory();
}

watch(
  () => [authSessionState.commandState, authSessionState.sessionToken] as const,
  async ([commandState, sessionToken]) => {
    if (commandState === 'success' && sessionToken) {
      await loadDeviceInventory();
      return;
    }
    resetDeviceInventory();
  },
  { immediate: true },
);

</script>
