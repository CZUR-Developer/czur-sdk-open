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
        <div class="space-y-4">
          <div class="grid gap-4 md:grid-cols-2">
            <label class="space-y-2 text-sm font-medium text-slate-700">
              <span>{{ t('labels.selectedDevice') }}</span>
              <select
                class="w-full rounded-2xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-900 shadow-sm outline-none transition focus:border-cyan-500"
                :value="deviceVideoState.selectedDeviceId"
                @change="handleSelectDevice"
              >
                <option value="">-</option>
                <option v-for="device in deviceInventoryState.devices" :key="device.device_id" :value="device.device_id">
                  {{ device.display_name || device.device_id }}
                </option>
              </select>
            </label>

            <label class="space-y-2 text-sm font-medium text-slate-700">
              <span>{{ t('labels.resolution') }}</span>
              <select
                class="w-full rounded-2xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-900 shadow-sm outline-none transition focus:border-cyan-500 disabled:cursor-not-allowed disabled:opacity-60"
                :disabled="deviceVideoState.resolutions.length === 0"
                :value="deviceVideoState.selectedResolutionKey"
                @change="handleSelectResolution"
              >
                <option value="">-</option>
                <option v-for="resolution in deviceVideoState.resolutions" :key="resolutionKey(resolution)" :value="resolutionKey(resolution)">
                  {{ resolution.width }} x {{ resolution.height }} @ {{ resolution.fps }}fps
                </option>
              </select>
            </label>
          </div>

          <div class="flex flex-wrap gap-3">
            <button
              type="button"
              class="inline-flex items-center rounded-2xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-700 shadow-sm transition hover:border-slate-300 hover:text-slate-950 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="!deviceVideoState.selectedDeviceId || deviceVideoState.detailState === 'running'"
              @click="loadDeviceDetail"
            >
              device.get
            </button>
            <button
              type="button"
              class="inline-flex items-center rounded-2xl border border-cyan-200 bg-cyan-50 px-3 py-2 text-sm font-medium text-cyan-800 shadow-sm transition hover:border-cyan-300 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="!deviceVideoState.selectedResolutionKey || deviceVideoState.openState === 'running'"
              @click="openSelectedDevice"
            >
              device.open
            </button>
            <button
              type="button"
              class="inline-flex items-center rounded-2xl border border-emerald-200 bg-emerald-50 px-3 py-2 text-sm font-medium text-emerald-800 shadow-sm transition hover:border-emerald-300 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="!deviceVideoState.opened || deviceVideoState.startState === 'running' || Boolean(deviceVideoState.streamId)"
              @click="startVideo"
            >
              video.start
            </button>
            <button
              type="button"
              class="inline-flex items-center rounded-2xl border border-rose-200 bg-rose-50 px-3 py-2 text-sm font-medium text-rose-800 shadow-sm transition hover:border-rose-300 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="!deviceVideoState.streamId || deviceVideoState.stopState === 'running'"
              @click="stopVideo"
            >
              video.stop
            </button>
          </div>

          <p v-if="deviceVideoState.errorMessage" class="text-sm font-medium text-rose-700">
            {{ deviceVideoState.errorMessage }}
          </p>
        </div>

        <div class="mt-5 grid gap-4 xl:grid-cols-2">
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
            :title="previewTitle"
            :description="previewDescription"
            :metrics="liveVideoPreviewMetrics"
            :image-url="deviceVideoState.previewUrl"
            :badge-label="videoBadgeLabel"
            :badge-tone="deviceVideoState.videoState === 'success' ? 'success' : 'primary'"
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
import { computed, onBeforeUnmount, watch } from 'vue';
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
  deviceVideoState,
  loadDeviceDetail,
  openSelectedDevice,
  resetDeviceVideo,
  resolutionKey,
  selectDevice,
  setSelectedResolution,
  startVideo,
  stopVideo,
} from '../services/device-video';
import {
  deviceControlCards,
  deviceVideoEvents,
  sharedJsonSnippets,
  streamCards,
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
    value: String(deviceVideoState.frameCount),
    labelKey: 'labels.resolution',
    detail: 'Current stream cadence measured on frame meta events',
    trend: currentResolutionLabel.value,
    tone: 'primary',
  },
  {
    id: 'event-lag',
    value: deviceVideoState.lastFrameAt || '-',
    labelKey: 'common.timeline',
    detail: 'Latest video frame timestamp observed by the demo page',
    trend: deviceVideoState.streamId || 'no active stream',
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

const currentResolutionLabel = computed(() => {
  const meta = deviceVideoState.frameMeta;
  if (meta?.width && meta.height) {
    return `${meta.width} x ${meta.height}`;
  }
  const selected = deviceVideoState.resolutions.find((resolution) => resolutionKey(resolution) === deviceVideoState.selectedResolutionKey);
  return selected ? `${selected.width} x ${selected.height}` : '-';
});

const liveVideoPreviewMetrics = computed(() => [
  { label: 'stream_id', value: deviceVideoState.streamId || '-', monospace: true },
  { label: 'frames', value: String(deviceVideoState.frameCount) },
  { label: 'last frame', value: deviceVideoState.lastFrameAt || '-' },
  { label: 'resolution', value: currentResolutionLabel.value },
]);

const previewTitle = computed(() => deviceVideoState.selectedDeviceId || 'No device selected');
const previewDescription = computed(() =>
  deviceVideoState.streamId
    ? 'Video WS is receiving stream.frame_meta events and JPEG binary frames.'
    : 'Select a device, load resolutions, open it, then start video to render frames here.',
);
const videoBadgeLabel = computed(() =>
  deviceVideoState.videoState === 'success' ? 'video.connected' : deviceVideoState.videoState,
);

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

function handleSelectDevice(event: Event) {
  const target = event.target as HTMLSelectElement;
  void selectDevice(target.value);
}

function handleSelectResolution(event: Event) {
  const target = event.target as HTMLSelectElement;
  setSelectedResolution(target.value);
}

watch(
  () => [authSessionState.commandState, authSessionState.sessionToken] as const,
  async ([commandState, sessionToken]) => {
    if (commandState === 'success' && sessionToken) {
      await loadDeviceInventory();
      return;
    }
    resetDeviceInventory();
    resetDeviceVideo();
  },
  { immediate: true },
);

watch(
  () => deviceInventoryState.devices,
  async (devices) => {
    if (!deviceVideoState.selectedDeviceId && devices.length > 0 && devices[0].device_id) {
      await selectDevice(devices[0].device_id);
    }
  },
);

onBeforeUnmount(() => {
  resetDeviceVideo();
});

</script>
