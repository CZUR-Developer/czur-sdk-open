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
      title="device/video runtime state"
      caption="Current live state assembled from device inventory, video stream, and canvas rendering counters."
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
import { authSessionState } from '../services/auth-session';
import { deviceInventoryState, loadDeviceInventory, resetDeviceInventory } from '../services/device-inventory';
import {
  attachVideoCanvas,
  closeSelectedDevice,
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
import { runtimeRecordState } from '../services/runtime-records';
import { executionStateLabelKey, executionStateTone, requirementTagLabelKey, requirementTagTone } from '../utils/presentation';
import type { InfoCardItem } from '../types/demo';

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
    value: String(deviceVideoState.renderedFrameCount),
    labelKey: 'labels.resolution',
    detail: 'Rendered frames committed to the canvas',
    trend: `${deviceVideoState.receivedFrameCount} received`,
    tone: 'primary',
  },
  {
    id: 'event-lag',
    value: String(deviceVideoState.droppedFrameCount),
    labelKey: 'common.timeline',
    detail: 'Frames dropped before canvas draw',
    trend: `${deviceVideoState.decodeFailedCount} decode failed`,
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
    title: 'Device inventory',
    description: deviceListDescription.value,
    meta: deviceListMeta.value,
    tone: stateTone(deviceInventoryState.listState),
    state: deviceInventoryState.listState,
    tags: ['device'],
  },
  {
    id: 'device-get',
    eyebrow: 'device.get',
    title: 'Device detail',
    description: deviceGetDescription.value,
    meta: deviceGetMeta.value,
    tone: stateTone(deviceVideoState.detailState),
    state: deviceVideoState.detailState,
    tags: ['device'],
  },
  {
    id: 'device-open',
    eyebrow: 'device.open',
    title: 'Open device',
    description: deviceOpenDescription.value,
    meta: deviceOpenMeta.value,
    tone: stateTone(deviceVideoState.openState),
    state: deviceVideoState.openState,
    tags: ['device'],
  },
  {
    id: 'device-close',
    eyebrow: 'device.close',
    title: 'Close device',
    description: deviceCloseDescription.value,
    meta: deviceCloseMeta.value,
    tone: stateTone(deviceVideoState.closeState),
    state: deviceVideoState.closeState,
    tags: ['device'],
  },
  {
    id: 'video-start',
    eyebrow: 'video.start',
    title: 'Start video',
    description: videoStartDescription.value,
    meta: videoStartMeta.value,
    tone: stateTone(deviceVideoState.startState),
    state: deviceVideoState.startState,
    tags: ['device'],
  },
  {
    id: 'video-stop',
    eyebrow: 'video.stop',
    title: 'Stop video',
    description: videoStopDescription.value,
    meta: videoStopMeta.value,
    tone: stateTone(deviceVideoState.stopState),
    state: deviceVideoState.stopState,
    tags: ['device'],
  },
]);

const deviceListDescription = computed(() => {
  if (deviceInventoryState.listState === 'running') {
    return 'Refreshing the device inventory from runtime.';
  }
  if (deviceInventoryState.listState === 'error') {
    return deviceInventoryState.errorMessage || 'device.list failed.';
  }
  if (deviceInventoryState.listState === 'success') {
    return `device.list resolved ${deviceInventoryState.count} device(s).`;
  }
  return authSessionState.sessionToken ? 'Ready to request device.list.' : 'Create an auth session before requesting device.list.';
});

const deviceListMeta = computed(() =>
  deviceInventoryState.lastLoadedAt ? `updated at ${deviceInventoryState.lastLoadedAt}` : 'awaiting inventory refresh',
);

const deviceGetDescription = computed(() => {
  if (deviceVideoState.detailState === 'running') {
    return `Loading detail for ${deviceVideoState.selectedDeviceId}.`;
  }
  if (deviceVideoState.detailState === 'error') {
    return deviceVideoState.errorMessage || 'device.get failed.';
  }
  if (deviceVideoState.detailState === 'success') {
    return `Loaded ${deviceVideoState.resolutions.length} preview resolution(s).`;
  }
  return deviceVideoState.selectedDeviceId ? 'Ready to load device detail.' : 'Select a device before requesting device.get.';
});

const deviceGetMeta = computed(() => deviceVideoState.selectedDeviceId || 'no device selected');

const deviceOpenDescription = computed(() => {
  if (deviceVideoState.openState === 'running') {
    return `Opening ${deviceVideoState.selectedDeviceId} for preview.`;
  }
  if (deviceVideoState.openState === 'error') {
    return deviceVideoState.errorMessage || 'device.open failed.';
  }
  if (deviceVideoState.opened) {
    return 'Device handle is open and ready for video.start.';
  }
  return deviceVideoState.selectedResolutionKey ? 'Ready to open the selected resolution.' : 'Select a preview resolution before device.open.';
});

const deviceOpenMeta = computed(() => deviceVideoState.selectedResolutionKey || 'no resolution selected');

const deviceCloseDescription = computed(() => {
  if (deviceVideoState.closeState === 'running') {
    return `Closing ${deviceVideoState.selectedDeviceId} and draining preview resources.`;
  }
  if (deviceVideoState.closeState === 'error') {
    return deviceVideoState.errorMessage || 'device.close failed.';
  }
  if (deviceVideoState.closeState === 'success') {
    return 'Device handle released and preview resources are idle.';
  }
  return deviceVideoState.opened || deviceVideoState.streamId ? 'Ready to close the opened device.' : 'No opened device to close.';
});

const deviceCloseMeta = computed(() =>
  deviceVideoState.streamId ? `will stop ${deviceVideoState.streamId}` : deviceVideoState.opened ? 'opened' : 'standby',
);

const videoStartDescription = computed(() => {
  if (deviceVideoState.startState === 'running') {
    return 'Creating stream session and connecting the video lane.';
  }
  if (deviceVideoState.startState === 'error') {
    return deviceVideoState.errorMessage || 'video.start failed.';
  }
  if (deviceVideoState.streamId) {
    return 'Video stream is active and bound to the current command session.';
  }
  return deviceVideoState.opened ? 'Ready to start video.' : 'Open the device before video.start.';
});

const videoStartMeta = computed(() => deviceVideoState.streamId || 'no active stream');

const videoStopDescription = computed(() => {
  if (deviceVideoState.stopState === 'running') {
    return `Stopping ${deviceVideoState.streamId}.`;
  }
  if (deviceVideoState.stopState === 'error') {
    return deviceVideoState.errorMessage || 'video.stop failed.';
  }
  if (deviceVideoState.stopState === 'success') {
    return 'Video stream stopped and preview lane drained.';
  }
  return deviceVideoState.streamId ? 'Ready to stop the active stream.' : 'No active stream to stop.';
});

const videoStopMeta = computed(() => deviceVideoState.streamId || 'standby');

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
  { label: 'rendered', value: String(deviceVideoState.renderedFrameCount) },
  { label: 'received', value: String(deviceVideoState.receivedFrameCount) },
  { label: 'dropped', value: String(deviceVideoState.droppedFrameCount) },
  { label: 'decode failed', value: String(deviceVideoState.decodeFailedCount) },
  { label: 'last seq', value: String(deviceVideoState.lastRenderedSeq || '-') },
  { label: 'last bytes', value: String(deviceVideoState.lastFrameBytes || '-') },
  { label: 'last frame', value: deviceVideoState.lastFrameAt || '-' },
  { label: 'resolution', value: currentResolutionLabel.value },
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
  void closeSelectedDevice();
  resetDeviceVideo();
});

</script>
