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
          class="inline-flex items-center rounded-2xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-700 shadow-sm transition enabled:cursor-pointer hover:border-slate-300 hover:text-slate-950 disabled:cursor-not-allowed disabled:opacity-60"
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
              class="inline-flex items-center rounded-2xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-700 shadow-sm transition enabled:cursor-pointer hover:border-slate-300 hover:text-slate-950 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="!deviceVideoState.selectedDeviceId || deviceVideoState.detailState === 'running'"
              @click="loadDeviceDetail"
            >
              device.get
            </button>
            <button
              type="button"
              class="inline-flex items-center rounded-2xl border border-cyan-200 bg-cyan-50 px-3 py-2 text-sm font-medium text-cyan-800 shadow-sm transition enabled:cursor-pointer hover:border-cyan-300 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="!deviceVideoState.selectedResolutionKey || deviceVideoState.openState === 'running'"
              @click="openSelectedDevice"
            >
              device.open
            </button>
            <button
              type="button"
              class="inline-flex items-center rounded-2xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-700 shadow-sm transition enabled:cursor-pointer hover:border-slate-300 hover:text-slate-950 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="(!deviceVideoState.opened && !deviceVideoState.streamId) || deviceVideoState.closeState === 'running'"
              @click="closeSelectedDevice"
            >
              device.close
            </button>
            <button
              type="button"
              class="inline-flex items-center rounded-2xl border border-emerald-200 bg-emerald-50 px-3 py-2 text-sm font-medium text-emerald-800 shadow-sm transition enabled:cursor-pointer hover:border-emerald-300 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="!deviceVideoState.opened || deviceVideoState.startState === 'running' || Boolean(deviceVideoState.streamId)"
              @click="startVideo"
            >
              video.start
            </button>
            <button
              type="button"
              class="inline-flex items-center rounded-2xl border border-rose-200 bg-rose-50 px-3 py-2 text-sm font-medium text-rose-800 shadow-sm transition enabled:cursor-pointer hover:border-rose-300 disabled:cursor-not-allowed disabled:opacity-60"
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
            v-for="card in deviceControlStatusCards"
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
            :badge-label="videoBadgeLabel"
            :badge-tone="deviceVideoState.videoState === 'success' ? 'success' : 'primary'"
            variant="video"
            @video-canvas="attachVideoCanvas"
          />
        </SectionPanel>

        <SectionPanel :title="t('sections.realtimeEvents')" :description="t('common.timeline')">
          <div class="max-h-[360px] overflow-y-auto pr-2">
            <EventTimeline :items="deviceVideoRuntimeEvents" />
          </div>
        </SectionPanel>
      </div>
    </div>

    <JsonPanel
      :title="t('pages.deviceVideo.runtimeStateTitle')"
      :caption="t('pages.deviceVideo.runtimeStateCaption')"
      :payload="liveDeviceVideoJson"
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
import { authSessionState, onCommandEvent } from '../services/auth-session';
import { deviceInventoryState, loadDeviceInventory, removeInventoryDevice, resetDeviceInventory } from '../services/device-inventory';
import {
  attachVideoCanvas,
  closeSelectedDevice,
  clearSelectionIfDeviceMissing,
  deviceVideoState,
  handleDeviceRemoved,
  loadDeviceDetail,
  openSelectedDevice,
  resetDeviceVideo,
  resolutionKey,
  selectDevice,
  setSelectedResolution,
  startVideo,
  stopVideo,
} from '../services/device-video';
import { runtimeRecordState } from '../services/runtime-records';
import type { CommandEvent } from '../services/protocol';
import { executionStateLabelKey, executionStateTone, requirementTagLabelKey, requirementTagTone } from '../utils/presentation';
import type { InfoCardItem } from '../types/demo';

const { t } = useI18n();
let commandEventUnsubscribe: (() => void) | null = null;

const deviceMetrics = computed(() => [
  {
    id: 'online-devices',
    value: String(deviceInventoryState.count),
    labelKey: 'sections.deviceInventory',
    detail:
      deviceInventoryState.listState === 'running'
        ? t('pages.deviceVideo.metricInventoryRefreshing')
        : t('pages.deviceVideo.metricInventoryReachable'),
    trend: deviceInventoryState.lastLoadedAt
      ? t('pages.deviceVideo.updatedAt', { time: deviceInventoryState.lastLoadedAt })
      : deviceInventoryState.listState === 'error'
        ? t('pages.deviceVideo.inventoryLoadFailed')
        : t('pages.deviceVideo.awaitingFirstInventoryRequest'),
    tone: deviceInventoryState.listState === 'error' ? 'warning' : 'success',
  },
  {
    id: 'frame-rate',
    value: String(deviceVideoState.renderedFrameCount),
    labelKey: 'labels.resolution',
    detail: t('pages.deviceVideo.metricRenderedFramesDetail'),
    trend: t('pages.deviceVideo.receivedFramesTrend', { count: deviceVideoState.receivedFrameCount }),
    tone: 'primary',
  },
  {
    id: 'event-lag',
    value: String(deviceVideoState.droppedFrameCount),
    labelKey: 'common.timeline',
    detail: t('pages.deviceVideo.metricDroppedFramesDetail'),
    trend: t('pages.deviceVideo.decodeFailedTrend', { count: deviceVideoState.decodeFailedCount }),
    tone: deviceVideoState.decodeFailedCount > 0 ? 'warning' : 'success',
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

const deviceControlStatusCards = computed<InfoCardItem[]>(() => [
  {
    id: 'device-list',
    eyebrow: 'device.list',
    title: t('pages.deviceVideo.deviceInventoryCardTitle'),
    description: deviceListDescription.value,
    meta: deviceListMeta.value,
    tone: stateTone(deviceInventoryState.listState),
    state: deviceInventoryState.listState,
    tags: ['device'],
  },
  {
    id: 'device-get',
    eyebrow: 'device.get',
    title: t('pages.deviceVideo.deviceDetailCardTitle'),
    description: deviceGetDescription.value,
    meta: deviceGetMeta.value,
    tone: stateTone(deviceVideoState.detailState),
    state: deviceVideoState.detailState,
    tags: ['device'],
  },
  {
    id: 'device-open',
    eyebrow: 'device.open',
    title: t('pages.deviceVideo.openDeviceCardTitle'),
    description: deviceOpenDescription.value,
    meta: deviceOpenMeta.value,
    tone: stateTone(deviceVideoState.openState),
    state: deviceVideoState.openState,
    tags: ['device'],
  },
  {
    id: 'device-close',
    eyebrow: 'device.close',
    title: t('pages.deviceVideo.closeDeviceCardTitle'),
    description: deviceCloseDescription.value,
    meta: deviceCloseMeta.value,
    tone: stateTone(deviceVideoState.closeState),
    state: deviceVideoState.closeState,
    tags: ['device'],
  },
  {
    id: 'video-start',
    eyebrow: 'video.start',
    title: t('pages.deviceVideo.startVideoCardTitle'),
    description: videoStartDescription.value,
    meta: videoStartMeta.value,
    tone: stateTone(deviceVideoState.startState),
    state: deviceVideoState.startState,
    tags: ['device'],
  },
  {
    id: 'video-stop',
    eyebrow: 'video.stop',
    title: t('pages.deviceVideo.stopVideoCardTitle'),
    description: videoStopDescription.value,
    meta: videoStopMeta.value,
    tone: stateTone(deviceVideoState.stopState),
    state: deviceVideoState.stopState,
    tags: ['device'],
  },
]);

const deviceListDescription = computed(() => {
  if (deviceInventoryState.listState === 'running') {
    return t('pages.deviceVideo.deviceListRefreshing');
  }
  if (deviceInventoryState.listState === 'error') {
    return deviceInventoryState.errorMessage || t('pages.deviceVideo.deviceListFailed');
  }
  if (deviceInventoryState.listState === 'success') {
    return t('pages.deviceVideo.deviceListResolved', { count: deviceInventoryState.count });
  }
  return authSessionState.sessionToken ? t('pages.deviceVideo.deviceListReady') : t('pages.deviceVideo.deviceListNeedsAuth');
});

const deviceListMeta = computed(() =>
  deviceInventoryState.lastLoadedAt
    ? t('pages.deviceVideo.updatedAt', { time: deviceInventoryState.lastLoadedAt })
    : t('pages.deviceVideo.awaitingInventoryRefresh'),
);

const deviceGetDescription = computed(() => {
  if (deviceVideoState.detailState === 'running') {
    return t('pages.deviceVideo.deviceGetLoading', { deviceId: deviceVideoState.selectedDeviceId });
  }
  if (deviceVideoState.detailState === 'error') {
    return deviceVideoState.errorMessage || t('pages.deviceVideo.deviceGetFailed');
  }
  if (deviceVideoState.detailState === 'success') {
    return t('pages.deviceVideo.deviceGetLoaded', { count: deviceVideoState.resolutions.length });
  }
  return deviceVideoState.selectedDeviceId ? t('pages.deviceVideo.deviceGetReady') : t('pages.deviceVideo.deviceGetNeedsSelection');
});

const deviceGetMeta = computed(() => deviceVideoState.selectedDeviceId || t('pages.deviceVideo.noDeviceSelected'));

const deviceOpenDescription = computed(() => {
  if (deviceVideoState.openState === 'running') {
    return t('pages.deviceVideo.deviceOpenRunning', { deviceId: deviceVideoState.selectedDeviceId });
  }
  if (deviceVideoState.openState === 'error') {
    return deviceVideoState.errorMessage || t('pages.deviceVideo.deviceOpenFailed');
  }
  if (deviceVideoState.opened) {
    return t('pages.deviceVideo.deviceOpenReadyForVideo');
  }
  return deviceVideoState.selectedResolutionKey ? t('pages.deviceVideo.deviceOpenReady') : t('pages.deviceVideo.deviceOpenNeedsResolution');
});

const deviceOpenMeta = computed(() => deviceVideoState.selectedResolutionKey || t('pages.deviceVideo.noResolutionSelected'));

const deviceCloseDescription = computed(() => {
  if (deviceVideoState.closeState === 'running') {
    return t('pages.deviceVideo.deviceCloseRunning', { deviceId: deviceVideoState.selectedDeviceId });
  }
  if (deviceVideoState.closeState === 'error') {
    return deviceVideoState.errorMessage || t('pages.deviceVideo.deviceCloseFailed');
  }
  if (deviceVideoState.closeState === 'success') {
    return t('pages.deviceVideo.deviceCloseSuccess');
  }
  return deviceVideoState.opened || deviceVideoState.streamId ? t('pages.deviceVideo.deviceCloseReady') : t('pages.deviceVideo.deviceCloseNoDevice');
});

const deviceCloseMeta = computed(() =>
  deviceVideoState.streamId
    ? t('pages.deviceVideo.willStopStream', { streamId: deviceVideoState.streamId })
    : deviceVideoState.opened ? t('pages.deviceVideo.opened') : t('pages.deviceVideo.standby'),
);

const videoStartDescription = computed(() => {
  if (deviceVideoState.startState === 'running') {
    return t('pages.deviceVideo.videoStartRunning');
  }
  if (deviceVideoState.startState === 'error') {
    return deviceVideoState.errorMessage || t('pages.deviceVideo.videoStartFailed');
  }
  if (deviceVideoState.streamId) {
    return t('pages.deviceVideo.videoStartActive');
  }
  return deviceVideoState.opened ? t('pages.deviceVideo.videoStartReady') : t('pages.deviceVideo.videoStartNeedsOpen');
});

const videoStartMeta = computed(() => deviceVideoState.streamId || t('pages.deviceVideo.noActiveStream'));

const videoStopDescription = computed(() => {
  if (deviceVideoState.stopState === 'running') {
    return t('pages.deviceVideo.videoStopRunning', { streamId: deviceVideoState.streamId });
  }
  if (deviceVideoState.stopState === 'error') {
    return deviceVideoState.errorMessage || t('pages.deviceVideo.videoStopFailed');
  }
  if (deviceVideoState.stopState === 'success') {
    return t('pages.deviceVideo.videoStopSuccess');
  }
  return deviceVideoState.streamId ? t('pages.deviceVideo.videoStopReady') : t('pages.deviceVideo.videoStopNoStream');
});

const videoStopMeta = computed(() => deviceVideoState.streamId || t('pages.deviceVideo.standby'));

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
  { label: t('pages.deviceVideo.metricRendered'), value: String(deviceVideoState.renderedFrameCount) },
  { label: t('pages.deviceVideo.metricReceived'), value: String(deviceVideoState.receivedFrameCount) },
  { label: t('pages.deviceVideo.metricDropped'), value: String(deviceVideoState.droppedFrameCount) },
  { label: t('pages.deviceVideo.metricDecodeFailed'), value: String(deviceVideoState.decodeFailedCount) },
  { label: t('pages.deviceVideo.metricLastSeq'), value: String(deviceVideoState.lastRenderedSeq || '-') },
  { label: t('pages.deviceVideo.metricLastBytes'), value: String(deviceVideoState.lastFrameBytes || '-') },
  { label: t('pages.deviceVideo.metricLastFrame'), value: deviceVideoState.lastFrameAt || '-' },
  { label: t('labels.resolution'), value: currentResolutionLabel.value },
]);

const deviceVideoRuntimeEvents = computed(() =>
  runtimeRecordState.events
    .filter((event) => /^(device|video|stream)\./.test(event.title))
    .slice(0, 8),
);

const liveDeviceVideoJson = computed(() =>
  JSON.stringify(
    {
      selected_device_id: deviceVideoState.selectedDeviceId || null,
      device_count: deviceInventoryState.count,
      list_state: deviceInventoryState.listState,
      detail_state: deviceVideoState.detailState,
      open_state: deviceVideoState.openState,
      close_state: deviceVideoState.closeState,
      opened: deviceVideoState.opened,
      selected_resolution: deviceVideoState.selectedResolutionKey || null,
      video_state: deviceVideoState.videoState,
      start_state: deviceVideoState.startState,
      stop_state: deviceVideoState.stopState,
      stream_id: deviceVideoState.streamId || null,
      frame_meta: deviceVideoState.frameMeta,
      rendering: {
        received: deviceVideoState.receivedFrameCount,
        rendered: deviceVideoState.renderedFrameCount,
        dropped: deviceVideoState.droppedFrameCount,
        decode_failed: deviceVideoState.decodeFailedCount,
        last_frame_bytes: deviceVideoState.lastFrameBytes,
        last_rendered_seq: deviceVideoState.lastRenderedSeq,
        last_frame_at: deviceVideoState.lastFrameAt || null,
      },
    },
    null,
    2,
  ),
);

const previewTitle = computed(() => deviceVideoState.selectedDeviceId || t('pages.deviceVideo.noDeviceSelected'));
const previewDescription = computed(() =>
  deviceVideoState.streamId
    ? t('pages.deviceVideo.previewReceiving')
    : t('pages.deviceVideo.previewIdleDescription'),
);
const videoBadgeLabel = computed(() =>
  deviceVideoState.videoState === 'success' ? t('pages.deviceVideo.videoConnected') : t(executionStateLabelKey(deviceVideoState.videoState)),
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
  void setSelectedResolution(target.value);
}

function stateTone(state: InfoCardItem['state']): InfoCardItem['tone'] {
  switch (state) {
    case 'success':
      return 'success';
    case 'running':
      return 'primary';
    case 'error':
      return 'danger';
    case 'blocked':
      return 'warning';
    default:
      return 'neutral';
  }
}

watch(
  () => [authSessionState.commandState, authSessionState.sessionToken] as const,
  async ([commandState, sessionToken]) => {
    commandEventUnsubscribe?.();
    commandEventUnsubscribe = null;
    if (commandState === 'success' && sessionToken) {
      commandEventUnsubscribe = onCommandEvent(handleCommandEvent);
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
    const activeDeviceIds = devices
      .map((device) => device.device_id)
      .filter((deviceId): deviceId is string => Boolean(deviceId));
    clearSelectionIfDeviceMissing(activeDeviceIds);
    if (!deviceVideoState.selectedDeviceId && devices.length > 0 && devices[0].device_id) {
      await selectDevice(devices[0].device_id);
    }
  },
);

onBeforeUnmount(() => {
  commandEventUnsubscribe?.();
  commandEventUnsubscribe = null;
  void closeSelectedDevice();
  resetDeviceVideo();
});

function handleCommandEvent(event: CommandEvent<Record<string, unknown>>): void {
  if (event.event === 'device.added') {
    void loadDeviceInventory();
    return;
  }
  if (event.event !== 'device.removed') {
    return;
  }
  const payload = event.payload ?? {};
  const deviceId = typeof payload.device_id === 'string' ? payload.device_id : '';
  const reason = typeof payload.reason === 'string' ? payload.reason : 'hotplug_removed';
  if (!deviceId) {
    void loadDeviceInventory();
    return;
  }
  handleDeviceRemoved(deviceId, reason);
  removeInventoryDevice(deviceId);
}

</script>
