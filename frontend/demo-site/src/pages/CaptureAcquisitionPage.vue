<template>
  <div class="space-y-6">
    <div class="grid gap-4 xl:grid-cols-4">
      <MetricCard v-for="metric in captureMetrics" :key="metric.label" v-bind="metric" />
    </div>

    <div class="grid gap-6 xl:grid-cols-[minmax(0,1.35fr)_minmax(360px,0.65fr)]">
      <div class="space-y-6">
        <SectionPanel :title="t('pages.captureAcquisition.capturePreview')" :description="t('pages.captureAcquisition.previewPanelDescription')">
          <div class="space-y-5">
            <PreviewStage
              :eyebrow="t('pages.captureAcquisition.liveDeviceStream')"
              :title="previewTitle"
              :description="previewDescription"
              :metrics="[]"
              :badge-label="deviceVideoState.videoState"
              :badge-tone="videoBadgeTone"
              stage-size="large"
              :show-text-overlay="false"
              variant="video"
              @video-canvas="attachVideoCanvas"
            />

            <div class="grid gap-4 lg:grid-cols-[minmax(0,1fr)_260px]">
              <div class="rounded-2xl border border-slate-200 bg-white/80 p-4 shadow-sm">
                <div class="mb-3 flex items-center justify-between gap-3">
                  <div>
                    <p class="text-sm font-semibold text-slate-900">{{ t('pages.captureAcquisition.devices') }}</p>
                    <p class="text-xs text-slate-500">{{ t('pages.captureAcquisition.availableCount', { count: deviceInventoryState.devices.length }) }}</p>
                  </div>
                  <button
                    type="button"
                    class="rounded-full border border-slate-200 px-3 py-1.5 text-xs font-semibold text-slate-600 transition hover:border-cyan-300 hover:text-cyan-700 disabled:cursor-not-allowed disabled:opacity-50"
                    :disabled="!canLoadDevices || deviceInventoryState.loading"
                    @click="loadDeviceInventory"
                  >
                    device.list
                  </button>
                </div>

                <div class="max-h-48 overflow-auto rounded-xl border border-slate-100">
                  <table class="min-w-full divide-y divide-slate-100 text-left text-sm">
                    <thead class="bg-slate-50 text-xs uppercase tracking-wide text-slate-500">
                      <tr>
                        <th class="px-3 py-2 font-semibold">{{ t('labels.selectedDevice') }}</th>
                        <th class="px-3 py-2 font-semibold">{{ t('labels.model') }}</th>
                        <th class="px-3 py-2 font-semibold">{{ t('labels.status') }}</th>
                      </tr>
                    </thead>
                    <tbody class="divide-y divide-slate-100 bg-white">
                      <tr
                        v-for="device in deviceInventoryState.devices"
                        :key="device.device_id"
                        class="cursor-pointer transition hover:bg-cyan-50/60"
                        :class="device.device_id === deviceVideoState.selectedDeviceId ? 'bg-cyan-50' : ''"
                        @click="selectDevice(device.device_id)"
                      >
                        <td class="px-3 py-2 font-mono text-xs text-slate-800">{{ device.device_id }}</td>
                        <td class="px-3 py-2 text-slate-700">{{ device.model || '-' }}</td>
                        <td class="px-3 py-2">
                          <StatusPill :label="device.status || 'unknown'" :tone="device.status === 'online' ? 'success' : 'neutral'" />
                        </td>
                      </tr>
                      <tr v-if="deviceInventoryState.devices.length === 0">
                        <td colspan="3" class="px-3 py-8 text-center text-sm text-slate-500">{{ t('pages.captureAcquisition.noDeviceData') }}</td>
                      </tr>
                    </tbody>
                  </table>
                </div>
              </div>

              <div class="rounded-2xl border border-slate-200 bg-white/80 p-4 shadow-sm">
                <p class="mb-3 text-sm font-semibold text-slate-900">{{ t('sections.deviceControl') }}</p>
                <div class="space-y-3">
                  <label class="block text-xs font-semibold uppercase tracking-wide text-slate-500">
                    {{ t('labels.resolution') }}
                    <select
                      class="mt-1 w-full rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition focus:border-cyan-400 focus:ring-2 focus:ring-cyan-100 disabled:cursor-not-allowed disabled:opacity-60"
                      :value="deviceVideoState.selectedResolutionKey"
                      :disabled="captureResolutionOptions.length === 0 || deviceVideoState.features.image_transfer_protocol"
                      @change="handleResolutionChange"
                    >
                      <option v-if="!deviceVideoState.features.image_transfer_protocol" value="">{{ t('pages.captureAcquisition.autoResolution') }}</option>
                      <option v-for="resolution in captureResolutionOptions" :key="resolutionKey(resolution)" :value="resolutionKey(resolution)">
                        {{ resolution.width }} x {{ resolution.height }} @ {{ resolution.fps }} fps
                      </option>
                    </select>
                    <span v-if="deviceVideoState.features.image_transfer_protocol" class="mt-2 block text-xs font-medium normal-case tracking-normal text-amber-600">
                      {{ t('pages.captureAcquisition.imageTransferCaptureResolutionLocked') }}
                    </span>
                  </label>

                  <div class="grid grid-cols-2 gap-2">
                    <button type="button" class="control-button" :disabled="!deviceVideoState.selectedDeviceId" @click="loadDeviceDetail">
                      device.get
                    </button>
                    <button
                      type="button"
                      class="control-button"
                      :disabled="!deviceVideoState.selectedDeviceId || deviceVideoState.opened || Boolean(deviceVideoState.streamId) || deviceVideoState.openState === 'running'"
                      @click="openCaptureDevice"
                    >
                      device.open
                    </button>
                    <button
                      type="button"
                      class="control-button"
                      :disabled="!deviceVideoState.opened || Boolean(deviceVideoState.streamId) || deviceVideoState.startState === 'running'"
                      @click="startCaptureVideo"
                    >
                      video.start
                    </button>
                    <button type="button" class="control-button" :disabled="!deviceVideoState.streamId" @click="stopVideo">
                      video.stop
                    </button>
                    <button type="button" class="control-button col-span-2" :disabled="!deviceVideoState.selectedDeviceId" @click="closeSelectedDevice">
                      device.close
                    </button>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </SectionPanel>

        <SectionPanel :title="t('pages.captureAcquisition.currentCaptureFiles')" :description="t('pages.captureAcquisition.currentCaptureFilesDescription')">
          <template #actions>
            <button
              type="button"
              class="rounded-full border border-slate-200 px-3 py-1.5 text-xs font-semibold text-slate-600 transition hover:border-rose-300 hover:text-rose-700 disabled:cursor-not-allowed disabled:opacity-50"
              :disabled="captureResults.length === 0"
              @click="clearCaptureResults"
            >
              Clear
            </button>
          </template>
          <DataTableCard :columns="captureColumns" :rows="captureRows" :empty-label="t('pages.captureAcquisition.noCaptures')" />
        </SectionPanel>
      </div>

      <div class="space-y-6">
        <SectionPanel :title="t('pages.captureAcquisition.captureProfile')" :description="t('pages.captureAcquisition.captureProfileDescription')">
          <div class="space-y-5">
            <div>
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.pageProcessing') }}</p>
              <div class="grid gap-2">
                <button
                  v-for="option in pageProcessingOptions"
                  :key="option.value"
                  type="button"
                  class="option-button"
                  :class="captureConfig.pageProcessing === option.value ? 'option-button-active' : ''"
                  @click="captureConfig.pageProcessing = option.value"
                >
                  <span class="font-semibold">{{ option.label }}</span>
                  <span class="text-xs text-slate-500">{{ option.description }}</span>
                </button>
              </div>
            </div>

            <div>
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.colorMode') }}</p>
              <div class="grid gap-2 sm:grid-cols-2">
                <button
                  v-for="option in colorModeOptions"
                  :key="option.value"
                  type="button"
                  class="option-button"
                  :class="captureConfig.colorMode === option.value ? 'option-button-active' : ''"
                  @click="captureConfig.colorMode = option.value"
                >
                  <span class="font-semibold">{{ option.label }}</span>
                  <span class="text-xs text-slate-500">{{ option.description }}</span>
                </button>
              </div>
            </div>

            <div>
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.outputFormat') }}</p>
              <div class="grid grid-cols-3 gap-2">
                <button
                  v-for="format in outputFormats"
                  :key="format"
                  type="button"
                  class="rounded-xl border px-3 py-2 text-sm font-semibold uppercase transition"
                  :class="
                    captureConfig.outputFormat === format
                      ? 'border-cyan-400 bg-cyan-50 text-cyan-800'
                      : 'border-slate-200 bg-white text-slate-600 hover:border-cyan-200'
                  "
                  @click="captureConfig.outputFormat = format"
                >
                  {{ format }}
                </button>
              </div>
            </div>

            <div>
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.thumbnailOutputs') }}</p>
              <div class="space-y-2">
                <label v-for="item in thumbnailOptions" :key="item.key" class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                  <span>{{ item.label }}</span>
                  <input v-model="captureConfig.thumbnails[item.key]" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
                </label>
              </div>
            </div>

            <button
              type="button"
              class="w-full rounded-xl bg-cyan-600 px-4 py-3 text-sm font-semibold text-white transition hover:bg-cyan-700 disabled:cursor-not-allowed disabled:bg-slate-300"
              :disabled="!canCapture"
              @click="handleCapture"
            >
              {{ t('pages.captureAcquisition.capture') }}
            </button>
          </div>
        </SectionPanel>

        <JsonPanel :title="t('pages.captureAcquisition.profileJson')" :caption="t('pages.captureAcquisition.initializationPayload')" :payload="profilePayload" />

        <SectionPanel :title="t('sections.realtimeEvents')" :description="t('pages.captureAcquisition.realtimeEventsDescription')">
          <div class="max-h-72 overflow-y-auto pr-1">
            <EventTimeline v-if="eventItems.length > 0" :items="eventItems" />
            <p v-else class="rounded-2xl border border-dashed border-slate-200 px-4 py-8 text-center text-sm text-slate-500">
              {{ t('pages.captureAcquisition.noRealtimeEvents') }}
            </p>
          </div>
        </SectionPanel>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, reactive, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import DataTableCard from '../components/blocks/DataTableCard.vue';
import EventTimeline from '../components/blocks/EventTimeline.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import PreviewStage from '../components/blocks/PreviewStage.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import MetricCard from '../components/cards/MetricCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState, onCommandEvent, sendBoundCommand } from '../services/auth-session';
import { deviceInventoryState, loadDeviceInventory, resetDeviceInventory } from '../services/device-inventory';
import {
  attachVideoCanvas,
  applyCaptureAcquisitionResolution,
  closeSelectedDevice,
  type DeviceResolution,
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
import { recordRuntimeEvent, runtimeRecordState } from '../services/runtime-records';
import type { TableColumn, TableRow, TimelineItem } from '../types/demoSite';

type PageProcessingMode = 'single_page' | 'curved_book' | 'selected_area';
type ColorMode = 'auto_optimize' | 'black_white' | 'color' | 'white_paper_seal';
type OutputFormat = 'jpg' | 'png' | 'tiff';
type ThumbnailKey = 'original' | 'pageProcessed' | 'colorProcessed';

interface CaptureResult {
  id: string;
  taskId: string;
  filename: string;
  format: OutputFormat;
  pageProcessing: string;
  colorMode: string;
  thumbnails: string;
  resolution: string;
  createdAt: string;
  status: string;
  path: string;
}

const { t } = useI18n();

const pageProcessingOptions = computed<Array<{ value: PageProcessingMode; label: string; description: string }>>(() => [
  {
    value: 'single_page',
    label: t('pages.captureAcquisition.pageSingle'),
    description: t('pages.captureAcquisition.pageSingleDescription'),
  },
  {
    value: 'curved_book',
    label: t('pages.captureAcquisition.pageCurved'),
    description: t('pages.captureAcquisition.pageCurvedDescription'),
  },
  {
    value: 'selected_area',
    label: t('pages.captureAcquisition.pageSelectedArea'),
    description: t('pages.captureAcquisition.pageSelectedAreaDescription'),
  },
]);

const colorModeOptions = computed<Array<{ value: ColorMode; label: string; description: string }>>(() => [
  {
    value: 'auto_optimize',
    label: t('pages.captureAcquisition.colorAutoOptimize'),
    description: t('pages.captureAcquisition.colorAutoOptimizeDescription'),
  },
  {
    value: 'black_white',
    label: t('pages.captureAcquisition.colorBlackWhite'),
    description: t('pages.captureAcquisition.colorBlackWhiteDescription'),
  },
  {
    value: 'color',
    label: t('pages.captureAcquisition.colorFullColor'),
    description: t('pages.captureAcquisition.colorFullColorDescription'),
  },
  {
    value: 'white_paper_seal',
    label: t('pages.captureAcquisition.colorWhitePaperSeal'),
    description: t('pages.captureAcquisition.colorWhitePaperSealDescription'),
  },
]);

const outputFormats: OutputFormat[] = ['jpg', 'png', 'tiff'];

const thumbnailOptions = computed<Array<{ key: ThumbnailKey; label: string }>>(() => [
  { key: 'original', label: t('pages.captureAcquisition.originalThumbnail') },
  { key: 'pageProcessed', label: t('pages.captureAcquisition.pageProcessedThumbnail') },
  { key: 'colorProcessed', label: t('pages.captureAcquisition.colorProcessedThumbnail') },
]);

const captureConfig = reactive({
  pageProcessing: 'single_page' as PageProcessingMode,
  colorMode: 'auto_optimize' as ColorMode,
  outputFormat: 'jpg' as OutputFormat,
  thumbnails: {
    original: true,
    pageProcessed: true,
    colorProcessed: false,
  } as Record<ThumbnailKey, boolean>,
});

const profileRevision = ref(1);
const profileInitializedAt = ref(timeLabel());
const captureResults = ref<CaptureResult[]>([]);
const activeCaptureTasks = new Set<string>();
let commandEventUnsubscribe: (() => void) | null = null;

const canLoadDevices = computed(() => authSessionState.commandState === 'success' && Boolean(authSessionState.sessionToken));
const canCapture = computed(() => Boolean(deviceVideoState.selectedDeviceId && deviceVideoState.opened && deviceVideoState.streamId && deviceVideoState.videoState === 'success'));

const captureResolutionOptions = computed<DeviceResolution[]>(() => {
  if (!deviceVideoState.features.image_transfer_protocol) {
    return deviceVideoState.resolutions;
  }
  const lockedResolution = deviceVideoState.resolutions.find((resolution) => resolution.width === 1536 && resolution.height === 1152);
  return lockedResolution
    ? [lockedResolution]
    : [{ width: 1536, height: 1152, real_width: 1536, real_height: 1152, fps: 15, pixel_format: 'jpeg', is_default: false }];
});

const selectedResolution = computed(() =>
  deviceVideoState.resolutions.find((resolution) => resolutionKey(resolution) === deviceVideoState.selectedResolutionKey),
);

const selectedResolutionLabel = computed(() => {
  if (!selectedResolution.value) {
    return t('pages.captureAcquisition.autoResolution');
  }
  return `${selectedResolution.value.width} x ${selectedResolution.value.height} @ ${selectedResolution.value.fps} fps`;
});

const previewTitle = computed(() => deviceVideoState.selectedDeviceId || t('pages.captureAcquisition.selectConnectedDevice'));
const previewDescription = computed(() => {
  if (!canLoadDevices.value) {
    return t('pages.captureAcquisition.authorizeBeforeDevices');
  }
  if (!deviceVideoState.selectedDeviceId) {
    return t('pages.captureAcquisition.chooseDeviceDescription');
  }
  return t('pages.captureAcquisition.resolutionValue', { value: selectedResolutionLabel.value });
});

const videoBadgeTone = computed(() => {
  if (deviceVideoState.videoState === 'success') {
    return 'success';
  }
  if (deviceVideoState.videoState === 'error') {
    return 'danger';
  }
  if (deviceVideoState.videoState === 'running') {
    return 'warning';
  }
  return 'neutral';
});

const activePageProcessing = computed(() => pageProcessingOptions.value.find((item) => item.value === captureConfig.pageProcessing));
const activeColorMode = computed(() => colorModeOptions.value.find((item) => item.value === captureConfig.colorMode));

const enabledThumbnailLabels = computed(() =>
  thumbnailOptions.value.filter((item) => captureConfig.thumbnails[item.key]).map((item) => item.label),
);

const captureMetrics = computed(() => [
  {
    label: t('pages.captureAcquisition.deviceState'),
    value: deviceVideoState.opened
      ? t('pages.captureAcquisition.opened')
      : deviceVideoState.selectedDeviceId
        ? t('pages.captureAcquisition.selected')
        : t('pages.captureAcquisition.noDevice'),
    helper: deviceVideoState.selectedDeviceId || t('pages.captureAcquisition.deviceCount', { count: deviceInventoryState.devices.length }),
    tone: deviceVideoState.opened ? 'success' : 'neutral',
  },
  {
    label: t('labels.resolution'),
    value: selectedResolutionLabel.value,
    helper: t('pages.captureAcquisition.resolutionOptionCount', { count: deviceVideoState.resolutions.length }),
    tone: selectedResolution.value ? 'success' : 'neutral',
  },
  {
    label: t('pages.captureAcquisition.profileRevision'),
    value: `r${profileRevision.value}`,
    helper: t('pages.captureAcquisition.initializedAt', { time: profileInitializedAt.value }),
    tone: 'warning',
  },
  {
    label: t('pages.captureAcquisition.sessionFiles'),
    value: String(captureResults.value.length),
    helper: captureConfig.outputFormat.toUpperCase(),
    tone: captureResults.value.length > 0 ? 'success' : 'neutral',
  },
]);

const captureProfile = computed(() => ({
  profile_version: 'capture.profile.v1',
  revision: profileRevision.value,
  initialized_at: profileInitializedAt.value,
  device: {
    device_id: deviceVideoState.selectedDeviceId || null,
    resolution: selectedResolution.value
      ? {
          width: selectedResolution.value.width,
          height: selectedResolution.value.height,
          fps: selectedResolution.value.fps,
        }
      : null,
  },
  capture: {
    page_processing: captureConfig.pageProcessing,
    color_mode: captureConfig.colorMode,
  },
  output: {
    format: captureConfig.outputFormat,
    thumbnails: {
      original: captureConfig.thumbnails.original,
      page_processed: captureConfig.thumbnails.pageProcessed,
      color_processed: captureConfig.thumbnails.colorProcessed,
    },
  },
}));

const profilePayload = computed(() => JSON.stringify(captureProfile.value, null, 2));

const captureColumns = computed<TableColumn[]>(() => [
  { key: 'filename', label: t('pages.captureAcquisition.file') },
  { key: 'format', label: t('labels.format') },
  { key: 'pageProcessing', label: t('pages.captureAcquisition.page') },
  { key: 'colorMode', label: t('pages.captureAcquisition.color') },
  { key: 'thumbnails', label: t('pages.captureAcquisition.thumbnails') },
  { key: 'resolution', label: t('labels.resolution') },
  { key: 'createdAt', label: t('labels.time') },
  { key: 'status', label: t('labels.status') },
]);

const captureRows = computed<TableRow[]>(() =>
  captureResults.value.map((result) => ({
    id: result.id,
    filename: result.filename,
    format: result.format.toUpperCase(),
    pageProcessing: result.pageProcessing,
    colorMode: result.colorMode,
    thumbnails: result.thumbnails,
    resolution: result.resolution,
    createdAt: result.createdAt,
    status: result.status,
  })),
);

const eventItems = computed<TimelineItem[]>(() =>
  runtimeRecordState.events
    .filter((item) => /^(capture|device|video|stream)\./.test(item.title))
    .slice(0, 20)
    .map((item) => ({
      id: item.id,
      time: item.time,
      title: item.title,
      detail: item.detail,
      tone: item.tone,
    })),
);

watch(
  () => [authSessionState.commandState, authSessionState.sessionToken] as const,
  ([state, sessionToken]) => {
    commandEventUnsubscribe?.();
    commandEventUnsubscribe = null;
    if (state === 'success' && sessionToken) {
      commandEventUnsubscribe = onCommandEvent(handleCommandEvent);
      void loadDeviceInventory();
      return;
    }
    resetDeviceInventory();
    resetDeviceVideo();
  },
  { immediate: true },
);

watch(
  () => deviceInventoryState.devices.map((device) => device.device_id).join('|'),
  () => {
    if (!deviceVideoState.selectedDeviceId && deviceInventoryState.devices.length > 0) {
      void openDefaultCaptureDevice();
    }
  },
  { immediate: true },
);

watch(
  () => [
    captureConfig.pageProcessing,
    captureConfig.colorMode,
    captureConfig.outputFormat,
    captureConfig.thumbnails.original,
    captureConfig.thumbnails.pageProcessed,
    captureConfig.thumbnails.colorProcessed,
    deviceVideoState.selectedDeviceId,
    deviceVideoState.selectedResolutionKey,
  ],
  () => {
    profileRevision.value += 1;
    profileInitializedAt.value = timeLabel();
  },
);

onBeforeUnmount(() => {
  commandEventUnsubscribe?.();
  commandEventUnsubscribe = null;
  void closeSelectedDevice();
  resetDeviceVideo();
});

function handleResolutionChange(event: Event): void {
  const value = (event.target as HTMLSelectElement).value;
  void setSelectedResolution(value);
}

async function openDefaultCaptureDevice(): Promise<void> {
  const defaultDeviceId = deviceInventoryState.devices[0]?.device_id;
  if (!defaultDeviceId) {
    return;
  }
  await selectDevice(defaultDeviceId);
  await openCaptureDevice();
  await startCaptureVideo();
}

async function openCaptureDevice(): Promise<void> {
  await applyCaptureAcquisitionResolution();
  await openSelectedDevice();
}

async function startCaptureVideo(): Promise<void> {
  await applyCaptureAcquisitionResolution();
  await startVideo();
}

async function handleCapture(): Promise<void> {
  if (!canCapture.value) {
    return;
  }

  const index = captureResults.value.length + 1;
  const pendingId = `capture-${Date.now()}-${index}`;
  const filename = `capture-${String(index).padStart(3, '0')}.${captureConfig.outputFormat}`;
  const result: CaptureResult = {
    id: pendingId,
    taskId: '',
    filename,
    format: captureConfig.outputFormat,
    pageProcessing: activePageProcessing.value?.label ?? captureConfig.pageProcessing,
    colorMode: activeColorMode.value?.label ?? captureConfig.colorMode,
    thumbnails: enabledThumbnailLabels.value.length > 0 ? enabledThumbnailLabels.value.join(', ') : t('pages.captureAcquisition.none'),
    resolution: selectedResolutionLabel.value,
    createdAt: timeLabel(),
    status: 'pending',
    path: '',
  };

  captureResults.value = [result, ...captureResults.value];
  try {
    const response = await sendBoundCommand('capture.take', {
      params: {
        device_id: deviceVideoState.selectedDeviceId,
        profile: captureProfile.value,
        timeout_ms: 20000,
      },
    });
    if (!response || response.code !== 0) {
      updateCaptureResult(pendingId, { status: response?.message || 'failed' });
      return;
    }
    const taskId = asString(response.data.task_id);
    updateCaptureResult(pendingId, { taskId, status: asString(response.data.status) || 'queued' });
    if (taskId) {
      activeCaptureTasks.add(taskId);
      void pollCaptureTask(taskId);
    }
    recordRuntimeEvent({
      title: 'capture.submitted',
      detail: taskId ? `capture.take accepted ${taskId}` : 'capture.take accepted',
      tone: 'success',
    });
  } catch (error) {
    updateCaptureResult(pendingId, { status: error instanceof Error ? error.message : 'failed' });
  }
}

async function pollCaptureTask(taskId: string): Promise<void> {
  for (let attempt = 0; attempt < 40 && activeCaptureTasks.has(taskId); attempt += 1) {
    await delay(750);
    try {
      const response = await sendBoundCommand('capture.get', {
        params: { task_id: taskId },
      });
      if (response.code !== 0) {
        continue;
      }
      const task = asTaskPayload(response.data);
      if (!task.task_id) {
        continue;
      }
      applyCaptureTask(task);
      if (task.status === 'succeeded' || task.status === 'failed') {
        activeCaptureTasks.delete(taskId);
        return;
      }
    } catch {
      // Keep polling; command events may still complete the row.
    }
  }
}

function handleCommandEvent(event: { event: string; payload?: Record<string, unknown> }): void {
  if (!event.event.startsWith('capture.')) {
    return;
  }
  const task = asTaskPayload(event.payload);
  if (!task.task_id || !activeCaptureTasks.has(task.task_id)) {
    return;
  }
  applyCaptureTask(task);
  if (event.event === 'capture.completed' || event.event === 'capture.failed' || task.status === 'succeeded' || task.status === 'failed') {
    activeCaptureTasks.delete(task.task_id);
  }
}

function applyCaptureTask(task: CaptureTaskPayload): void {
  const finalAsset = task.assets.find((asset) => asset.kind === 'final') ?? task.assets.find((asset) => asset.kind === 'original');
  const filename = finalAsset?.path ? finalAsset.path.split('/').pop() || finalAsset.path : undefined;
  updateCaptureByTaskId(task.task_id, {
    filename,
    path: finalAsset?.path,
    status: task.status === 'succeeded' ? 'succeeded' : task.status === 'failed' ? task.error || task.message || 'failed' : task.status,
  });
}

function updateCaptureByTaskId(taskId: string, patch: Partial<CaptureResult>): void {
  const row = captureResults.value.find((item) => item.taskId === taskId);
  if (!row) {
    return;
  }
  updateCaptureResult(row.id, patch);
}

function updateCaptureResult(id: string, patch: Partial<CaptureResult>): void {
  captureResults.value = captureResults.value.map((item) => (item.id === id ? { ...item, ...withoutUndefinedCapturePatch(patch) } : item));
}

function clearCaptureResults(): void {
  captureResults.value = [];
  activeCaptureTasks.clear();
  recordRuntimeEvent({
    title: 'capture.results_cleared',
    detail: t('pages.captureAcquisition.resultsCleared'),
    tone: 'neutral',
  });
}

interface CaptureAssetPayload {
  kind: string;
  path: string;
}

interface CaptureTaskPayload {
  task_id: string;
  status: string;
  message: string;
  error: string;
  assets: CaptureAssetPayload[];
}

function asTaskPayload(value: unknown): CaptureTaskPayload {
  const record = value && typeof value === 'object' ? (value as Record<string, unknown>) : {};
  return {
    task_id: asString(record.task_id),
    status: asString(record.status),
    message: asString(record.message),
    error: asString(record.error),
    assets: asAssets(record.assets),
  };
}

function asAssets(value: unknown): CaptureAssetPayload[] {
  if (!Array.isArray(value)) {
    return [];
  }
  return value
    .filter((item): item is Record<string, unknown> => Boolean(item) && typeof item === 'object')
    .map((item) => ({
      kind: asString(item.kind),
      path: asString(item.path),
    }));
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function withoutUndefinedCapturePatch(value: Partial<CaptureResult>): Partial<CaptureResult> {
  const result: Partial<CaptureResult> = {};
  for (const key of Object.keys(value) as Array<keyof CaptureResult>) {
    if (value[key] !== undefined) {
      result[key] = value[key] as never;
    }
  }
  return result;
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => {
    window.setTimeout(resolve, ms);
  });
}

function timeLabel(): string {
  return new Date().toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });
}
</script>

<style scoped>
.control-button {
  cursor: pointer;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.75rem;
  background: rgb(255 255 255);
  padding: 0.5rem 0.75rem;
  color: rgb(51 65 85);
  font-size: 0.875rem;
  font-weight: 600;
  transition:
    border-color 160ms ease,
    color 160ms ease,
    opacity 160ms ease;
}

.control-button:hover:not(:disabled) {
  border-color: rgb(103 232 249);
  color: rgb(14 116 144);
}

.control-button:disabled {
  cursor: not-allowed;
  opacity: 0.5;
}

.option-button {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 0.125rem;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.75rem;
  background: rgb(255 255 255);
  padding: 0.5rem 0.75rem;
  color: rgb(51 65 85);
  text-align: left;
  font-size: 0.875rem;
  transition:
    background-color 160ms ease,
    border-color 160ms ease,
    box-shadow 160ms ease;
}

.option-button:hover {
  border-color: rgb(165 243 252);
  background: rgb(236 254 255 / 0.4);
}

.option-button-active {
  border-color: rgb(34 211 238);
  background: rgb(236 254 255);
  color: rgb(22 78 99);
  box-shadow: 0 1px 2px rgb(15 23 42 / 0.08);
}
</style>
