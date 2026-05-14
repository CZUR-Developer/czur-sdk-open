<template>
  <div class="space-y-6">
    <SectionPanel :title="t('pages.imageProcessing.title')" :description="t('pages.imageProcessing.subtitle')">
      <div class="grid gap-5 xl:grid-cols-[minmax(0,1.2fr)_minmax(340px,0.8fr)]">
        <div class="space-y-4">
          <div class="rounded-2xl border border-slate-200 bg-white/80 p-4 shadow-sm">
            <div class="flex flex-wrap items-center justify-between gap-3">
              <div>
                <p class="text-sm font-semibold text-slate-900">{{ t('pages.imageProcessing.inputImage') }}</p>
                <p class="mt-1 text-xs text-slate-500">{{ selectedFileName || t('pages.imageProcessing.noImageSelected') }}</p>
              </div>
              <label class="primary-button">
                {{ t('pages.imageProcessing.chooseImage') }}
                <input type="file" accept="image/*" class="sr-only" @change="handleFileChange" />
              </label>
            </div>
          </div>

          <div class="grid gap-4 lg:grid-cols-2">
            <article class="preview-card">
              <div class="preview-header">
                <span>{{ t('pages.imageProcessing.originalPreview') }}</span>
                <StatusPill :label="inputPreviewUrl ? t('pages.imageProcessing.localReady') : t('status.idle')" :tone="inputPreviewUrl ? 'success' : 'neutral'" />
              </div>
              <div class="preview-surface">
                <div v-if="inputPreviewUrl" ref="inputStageRef" class="relative inline-block max-h-full max-w-full">
                  <img
                    ref="inputImageRef"
                    :src="inputPreviewUrl"
                    alt=""
                    class="max-h-[420px] max-w-full select-none object-contain"
                    @load="handleInputImageLoad"
                  />
                  <div
                    v-if="form.pageProcessing === 'selected_area'"
                    class="absolute inset-0 cursor-crosshair touch-none"
                    @pointerdown="handleAreaPointerDown"
                    @pointermove="handleAreaPointerMove"
                    @pointerup="handleAreaPointerUp"
                    @pointercancel="handleAreaPointerCancel"
                  >
                    <div
                      v-if="areaOverlayRect"
                      class="absolute border-2 border-cyan-300 bg-cyan-300/10 shadow-[0_0_0_9999px_rgba(15,23,42,0.24)]"
                      :style="{
                        left: `${areaOverlayRect.x}px`,
                        top: `${areaOverlayRect.y}px`,
                        width: `${areaOverlayRect.width}px`,
                        height: `${areaOverlayRect.height}px`,
                      }"
                    />
                    <svg class="pointer-events-none absolute inset-0 h-full w-full">
                      <polygon
                        v-if="areaOverlayPolygon"
                        :points="areaOverlayPolygon"
                        fill="rgba(34, 211, 238, 0.12)"
                        stroke="#22d3ee"
                        stroke-width="2"
                      />
                    </svg>
                    <span
                      v-for="(point, index) in overlayPoints"
                      :key="index"
                      class="pointer-events-none absolute h-3 w-3 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-white bg-cyan-400 shadow"
                      :style="{ left: `${point.x}px`, top: `${point.y}px` }"
                    />
                  </div>
                </div>
                <p v-else class="px-6 text-center text-sm text-slate-500">{{ t('pages.imageProcessing.pickImageHint') }}</p>
              </div>
            </article>

            <article class="preview-card">
              <div class="preview-header">
                <span>{{ t('pages.imageProcessing.processedPreview') }}</span>
                <StatusPill :label="resultStatusLabel" :tone="resultStatusTone" />
              </div>
              <div class="preview-surface">
                <img
                  v-if="activeOutputPreview?.objectUrl"
                  :src="activeOutputPreview.objectUrl"
                  alt=""
                  class="max-h-[420px] max-w-full object-contain"
                />
                <div v-else class="px-6 text-center text-sm text-slate-500">
                  <p>{{ processedPlaceholder }}</p>
                  <p v-if="activeOutputPreview?.error" class="mt-2 text-xs text-rose-500">{{ activeOutputPreview.error }}</p>
                </div>
              </div>
            </article>
          </div>
        </div>

        <div class="space-y-4">
          <KeyValueGrid :items="runtimeItems" />
          <div class="grid grid-cols-2 gap-2">
            <button type="button" class="secondary-button" :disabled="!selectedFile || uploadRunning" @click="handleUploadClick">
              {{ uploadRunning ? t('pages.imageProcessing.uploading') : t('pages.imageProcessing.upload') }}
            </button>
            <button type="button" class="primary-button justify-center" :disabled="!canProcess" @click="runImageProcess">
              {{ running ? t('pages.imageProcessing.processing') : t('pages.imageProcessing.process') }}
            </button>
          </div>
          <p v-if="lastError" class="rounded-xl border border-rose-200 bg-rose-50 px-3 py-2 text-sm text-rose-600">{{ lastError }}</p>
        </div>
      </div>
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-[0.9fr_1.1fr]">
      <SectionPanel :title="t('pages.imageProcessing.processingOptions')" :description="t('pages.imageProcessing.processingOptionsDescription')">
        <div class="space-y-5">
          <div>
            <p class="option-heading">{{ t('pages.captureAcquisition.pageProcessing') }}</p>
            <div class="grid gap-2">
              <button
                v-for="option in pageProcessingOptions"
                :key="option.value"
                type="button"
                class="option-button"
                :class="form.pageProcessing === option.value ? 'option-button-active' : ''"
                @click="setPageProcessing(option.value)"
              >
                <span class="font-semibold">{{ t(option.labelKey) }}</span>
                <span class="text-xs text-slate-500">{{ t(option.descriptionKey) }}</span>
              </button>
            </div>
          </div>

          <div v-if="form.pageProcessing === 'selected_area'" class="space-y-2">
            <div class="flex items-center justify-between gap-3">
              <p class="option-heading mb-0">{{ t('pages.captureAcquisition.selectedAreaOptions') }}</p>
              <button type="button" class="text-xs font-semibold text-cyan-700" @click="clearSelectedArea">{{ t('pages.captureAcquisition.clearSelectedArea') }}</button>
            </div>
            <div class="grid grid-cols-2 gap-2">
              <button type="button" class="format-button" :class="areaMode === 'rectangle' ? 'format-button-active' : ''" @click="setAreaMode('rectangle')">
                {{ t('pages.captureAcquisition.selectedAreaRectangle') }}
              </button>
              <button type="button" class="format-button" :class="areaMode === 'points' ? 'format-button-active' : ''" @click="setAreaMode('points')">
                {{ t('pages.captureAcquisition.selectedAreaPoints') }}
              </button>
            </div>
            <p class="rounded-xl border border-cyan-100 bg-cyan-50 px-3 py-2 text-xs font-medium text-cyan-800">
              {{ selectedAreaHint }}
            </p>
          </div>

          <div>
            <p class="option-heading">{{ t('pages.captureAcquisition.colorMode') }}</p>
            <select v-model="form.colorMode" class="field-input">
              <option v-for="option in colorModeOptions" :key="option.value" :value="option.value">{{ t(option.labelKey) }}</option>
            </select>
          </div>

          <div>
            <p class="option-heading">{{ t('pages.captureAcquisition.outputFormat') }}</p>
            <div class="grid grid-cols-3 gap-2">
              <button
                v-for="format in outputFormats"
                :key="format"
                type="button"
                class="format-button"
                :class="form.outputFormat === format ? 'format-button-active' : ''"
                @click="form.outputFormat = format"
              >
                {{ format }}
              </button>
            </div>
          </div>

          <div v-if="form.pageProcessing === 'single_page'" class="space-y-2">
            <p class="option-heading">{{ t('pages.captureAcquisition.singlePageOptions') }}</p>
            <ToggleRow v-model="form.cropBorder" :label="t('pages.captureAcquisition.cropBorder')" />
            <div v-if="form.cropBorder" class="grid grid-cols-2 gap-2">
              <label class="field-label">
                {{ t('pages.captureAcquisition.cropBorderWidth') }}
                <input v-model.number="form.cropBorderWidth" type="number" min="-100" max="100" class="field-input" />
              </label>
              <label class="field-label">
                {{ t('pages.captureAcquisition.cropBorderHeight') }}
                <input v-model.number="form.cropBorderHeight" type="number" min="-100" max="100" class="field-input" />
              </label>
            </div>
            <ToggleRow v-model="form.idCardRoundCorner" :label="t('pages.captureAcquisition.idCardRoundCorner')" />
            <ToggleRow v-model="form.autoRotate" :label="t('pages.captureAcquisition.autoRotate')" />
            <ToggleRow v-model="form.smartBlackEdgeOptimize" :label="t('pages.captureAcquisition.smartBlackEdgeOptimize')" />
            <ToggleRow v-model="form.multiTargetPaging" :label="t('pages.captureAcquisition.multiTargetPaging')" />
          </div>

          <div v-if="form.pageProcessing === 'curved_book'" class="space-y-2">
            <p class="option-heading">{{ t('pages.captureAcquisition.curvedBookOptions') }}</p>
            <p class="rounded-xl border border-amber-200 bg-amber-50 px-3 py-2 text-xs font-medium text-amber-800">
              {{ t('pages.imageProcessing.curvedBookEdgeOnlyNotice') }}
            </p>
            <ToggleRow v-model="form.removeFinger" :label="t('pages.captureAcquisition.removeFinger')" />
            <select v-model="form.fingerType" class="field-input" :disabled="!form.removeFinger">
              <option value="with_sleeve">{{ t('pages.captureAcquisition.fingerWithSleeve') }}</option>
              <option value="without_sleeve">{{ t('pages.captureAcquisition.fingerWithoutSleeve') }}</option>
            </select>
            <ToggleRow v-model="form.smartPaging" :label="t('pages.captureAcquisition.smartPaging')" />
            <ToggleRow v-model="form.curvedCropBorder" :label="t('pages.captureAcquisition.cropBorder')" />
            <div v-if="form.curvedCropBorder" class="grid grid-cols-2 gap-2">
              <label class="field-label">
                {{ t('pages.captureAcquisition.cropBorderWidth') }}
                <input v-model.number="form.curvedCropBorderWidth" type="number" min="-100" max="100" class="field-input" />
              </label>
              <label class="field-label">
                {{ t('pages.captureAcquisition.cropBorderHeight') }}
                <input v-model.number="form.curvedCropBorderHeight" type="number" min="-100" max="100" class="field-input" />
              </label>
            </div>
            <ToggleRow v-model="form.curvedAutoComplete" :label="t('pages.captureAcquisition.autoComplete')" />
          </div>
        </div>
      </SectionPanel>

      <div class="space-y-6">
        <SectionPanel :title="t('common.results')" :description="t('pages.imageProcessing.resultsDescription')">
          <KeyValueGrid :items="resultItems" />
          <div v-if="outputPreviews.length > 0" class="mt-4 grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
            <button
              v-for="preview in outputPreviews"
              :key="preview.assetId"
              type="button"
              class="result-thumb"
              :class="preview.assetId === activeOutputAssetId ? 'result-thumb-active' : ''"
              @click="activeOutputAssetId = preview.assetId"
            >
              <img v-if="preview.objectUrl" :src="preview.objectUrl" alt="" class="h-24 w-full rounded-lg bg-slate-100 object-contain" />
              <span v-else class="flex h-24 items-center justify-center rounded-lg bg-slate-100 text-xs text-slate-500">
                {{ preview.state === 'loading' ? t('common.loading') : t('pages.imageProcessing.previewUnavailable') }}
              </span>
              <span class="mt-2 block truncate text-xs font-semibold text-slate-700">{{ preview.role }}</span>
            </button>
          </div>
        </SectionPanel>

        <JsonPanel :title="t('pages.imageProcessing.requestJson')" :caption="t('common.request')" :payload="requestPreview" />
        <JsonPanel :title="t('pages.imageProcessing.responseJson')" :caption="t('common.response')" :payload="responsePreview" />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, defineComponent, h, nextTick, onBeforeUnmount, reactive, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import JsonPanel from '../components/blocks/JsonPanel.vue';
import KeyValueGrid from '../components/blocks/KeyValueGrid.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState, sendBoundCommand } from '../services/auth-session';
import { isOkResponse, resolveRuntimeHost } from '../services/protocol';
import type { Tone } from '../types/demo';

type PageProcessing = 'keep_original' | 'single_page' | 'selected_area' | 'curved_book';
type AreaMode = 'rectangle' | 'points';
type PointKey = 'left_top' | 'right_top' | 'right_down' | 'left_down';

interface ProcessOutput {
  asset_id?: string;
  output_id?: string;
  index?: number;
  role?: string;
  path?: string;
  url?: string;
  download_url?: string;
  content_type?: string;
  width?: number;
  height?: number;
  size?: number;
}

interface OutputPreview {
  assetId: string;
  role: string;
  url: string;
  downloadUrl: string;
  contentType: string;
  objectUrl: string;
  state: 'idle' | 'loading' | 'ready' | 'error' | 'unsupported';
  error: string;
}

const ToggleRow = defineComponent({
  props: {
    modelValue: { type: Boolean, required: true },
    label: { type: String, required: true },
  },
  emits: ['update:modelValue'],
  setup(props, { emit }) {
    return () =>
      h('label', { class: 'flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700' }, [
        h('span', props.label),
        h('input', {
          checked: props.modelValue,
          type: 'checkbox',
          class: 'h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500',
          onChange: (event: Event) => emit('update:modelValue', (event.target as HTMLInputElement).checked),
        }),
      ]);
  },
});

const { t } = useI18n();

const pageProcessingOptions: Array<{ value: PageProcessing; labelKey: string; descriptionKey: string }> = [
  { value: 'keep_original', labelKey: 'pages.captureAcquisition.pageKeepOriginal', descriptionKey: 'pages.captureAcquisition.pageKeepOriginalDescription' },
  { value: 'single_page', labelKey: 'pages.captureAcquisition.pageSingle', descriptionKey: 'pages.captureAcquisition.pageSingleDescription' },
  { value: 'selected_area', labelKey: 'pages.captureAcquisition.pageSelectedArea', descriptionKey: 'pages.captureAcquisition.pageSelectedAreaDescription' },
  { value: 'curved_book', labelKey: 'pages.captureAcquisition.pageCurved', descriptionKey: 'pages.captureAcquisition.pageCurvedDescription' },
];

const colorModeOptions = [
  { value: 'auto_optimize', labelKey: 'pages.captureAcquisition.colorAutoOptimize' },
  { value: 'color', labelKey: 'pages.captureAcquisition.colorFullColor' },
  { value: 'black_white', labelKey: 'pages.captureAcquisition.colorBlackWhite' },
  { value: 'white_paper_seal', labelKey: 'pages.captureAcquisition.colorWhitePaperSeal' },
  { value: 'grayscale', labelKey: 'pages.captureAcquisition.colorGrayscale' },
  { value: 'certificate', labelKey: 'pages.captureAcquisition.colorCertificate' },
  { value: 'ancient', labelKey: 'pages.captureAcquisition.colorAncient' },
  { value: 'no_optimize', labelKey: 'pages.captureAcquisition.colorNoOptimize' },
];

const outputFormats = ['jpg', 'png', 'tiff'];
const pointKeys: PointKey[] = ['left_top', 'right_top', 'right_down', 'left_down'];

const form = reactive({
  pageProcessing: 'single_page' as PageProcessing,
  colorMode: 'auto_optimize',
  outputFormat: 'jpg',
  cropBorder: false,
  cropBorderWidth: 0,
  cropBorderHeight: 0,
  idCardRoundCorner: false,
  autoRotate: false,
  smartBlackEdgeOptimize: true,
  multiTargetPaging: false,
  selectedArea: emptyArea(),
  removeFinger: true,
  fingerType: 'with_sleeve',
  smartPaging: true,
  curvedCropBorder: false,
  curvedCropBorderWidth: 0,
  curvedCropBorderHeight: 0,
  curvedAutoComplete: false,
});

const selectedFile = ref<File | null>(null);
const selectedFileName = ref('');
const inputPreviewUrl = ref('');
const uploadId = ref('');
const uploadRunning = ref(false);
const running = ref(false);
const lastResponse = ref<Record<string, unknown> | null>(null);
const lastError = ref('');
const inputImageRef = ref<HTMLImageElement | null>(null);
const inputStageRef = ref<HTMLElement | null>(null);
const naturalSize = reactive({ width: 0, height: 0 });
const displaySize = reactive({ width: 0, height: 0 });
const areaMode = ref<AreaMode>('rectangle');
const areaDraftStart = ref<{ x: number; y: number } | null>(null);
const areaDraftEnd = ref<{ x: number; y: number } | null>(null);
const selectedPointCount = ref(0);
const outputPreviews = ref<OutputPreview[]>([]);
const activeOutputAssetId = ref('');

const canProcess = computed(() => Boolean(selectedFile.value && !uploadRunning.value && !running.value));
const outputs = computed<ProcessOutput[]>(() => {
  const value = lastResponse.value?.data;
  if (!value || typeof value !== 'object' || !Array.isArray((value as { outputs?: unknown }).outputs)) {
    return [];
  }
  return (value as { outputs: ProcessOutput[] }).outputs;
});
const activeOutputPreview = computed(() => outputPreviews.value.find((item) => item.assetId === activeOutputAssetId.value) ?? outputPreviews.value[0]);
const resultStatusLabel = computed(() => {
  if (running.value) {
    return t('status.running');
  }
  if (lastError.value) {
    return t('status.error');
  }
  if (outputPreviews.value.length > 0) {
    return t('status.success');
  }
  return t('status.idle');
});
const resultStatusTone = computed<Tone>(() => {
  if (running.value) {
    return 'warning';
  }
  if (lastError.value) {
    return 'danger';
  }
  if (outputPreviews.value.length > 0) {
    return 'success';
  }
  return 'neutral';
});
const processedPlaceholder = computed(() => {
  if (running.value) {
    return t('pages.imageProcessing.processing');
  }
  if (activeOutputPreview.value?.state === 'unsupported') {
    return t('pages.imageProcessing.previewUnsupported');
  }
  return t('pages.imageProcessing.processedPlaceholder');
});
const runtimeItems = computed(() => [
  { label: t('pages.imageProcessing.commandState'), value: t(`status.${authSessionState.commandState}`) },
  { label: t('pages.imageProcessing.session'), value: authSessionState.sessionToken ? t('pages.imageProcessing.bound') : t('pages.imageProcessing.notBound') },
  { label: t('pages.imageProcessing.uploadId'), value: uploadId.value || t('common.notSet'), monospace: true },
]);
const resultItems = computed(() => {
  const data = lastResponse.value?.data as Record<string, unknown> | undefined;
  return [
    { label: t('labels.status'), value: lastError.value || (lastResponse.value ? String(lastResponse.value.message) : t('status.idle')) },
    { label: t('labels.code'), value: lastResponse.value ? String(lastResponse.value.code) : '-' },
    { label: t('pages.imageProcessing.taskId'), value: typeof data?.task_id === 'string' ? data.task_id : '-', monospace: true },
    { label: t('labels.outputPath'), value: typeof data?.output_path === 'string' ? data.output_path : '-', monospace: true },
    { label: t('pages.imageProcessing.outputs'), value: String(outputs.value.length) },
  ];
});
const selectedAreaHint = computed(() => {
  if (!inputPreviewUrl.value) {
    return t('pages.imageProcessing.selectImageBeforeArea');
  }
  if (areaMode.value === 'points') {
    return t('pages.captureAcquisition.selectedAreaPointProgress', { count: selectedPointCount.value });
  }
  return hasSelectedArea() ? t('pages.captureAcquisition.selectedAreaReady') : t('pages.captureAcquisition.selectedAreaPending');
});
const overlayPoints = computed(() => {
  const keys = areaMode.value === 'points' ? pointKeys.slice(0, selectedPointCount.value) : pointKeys;
  return keys.map((key) => naturalToDisplay(form.selectedArea[key]));
});
const areaOverlayPolygon = computed(() => {
  if (areaMode.value === 'points' && selectedPointCount.value < pointKeys.length) {
    return '';
  }
  if (!hasSelectedArea()) {
    return '';
  }
  return pointKeys.map((key) => naturalToDisplay(form.selectedArea[key])).map((point) => `${point.x},${point.y}`).join(' ');
});
const areaOverlayRect = computed(() => {
  if (areaMode.value !== 'rectangle' || !hasSelectedArea()) {
    return null;
  }
  const points = pointKeys.map((key) => naturalToDisplay(form.selectedArea[key]));
  const xs = points.map((point) => point.x);
  const ys = points.map((point) => point.y);
  const minX = Math.min(...xs);
  const minY = Math.min(...ys);
  return {
    x: minX,
    y: minY,
    width: Math.max(...xs) - minX,
    height: Math.max(...ys) - minY,
  };
});
const commandPayload = computed(() => ({
  input_upload_id: uploadId.value || '<upload_id>',
  page_processing: form.pageProcessing,
  color_mode: form.colorMode,
  output_format: form.outputFormat,
  single_page: {
    crop_border: {
      enabled: form.cropBorder,
      width: form.cropBorderWidth,
      height: form.cropBorderHeight,
    },
    id_card_round_corner: form.idCardRoundCorner,
    auto_rotate: form.autoRotate,
    smart_black_edge_optimize: form.smartBlackEdgeOptimize,
    multi_target_paging: form.multiTargetPaging,
  },
  curved_book: {
    remove_finger: {
      enabled: form.removeFinger,
      finger_type: form.fingerType,
    },
    smart_paging: form.smartPaging,
    crop_border: {
      enabled: form.curvedCropBorder,
      width: form.curvedCropBorderWidth,
      height: form.curvedCropBorderHeight,
    },
    auto_complete: form.curvedAutoComplete,
  },
  selected_area:
    form.pageProcessing === 'selected_area' && hasSelectedArea()
      ? {
          source: {
            width: naturalSize.width,
            height: naturalSize.height,
          },
          points: form.selectedArea,
        }
      : undefined,
}));
const requestPreview = computed(() => JSON.stringify({ method: 'image.process', params: commandPayload.value }, null, 2));
const responsePreview = computed(() => JSON.stringify(lastResponse.value ?? { error: lastError.value || t('pages.imageProcessing.noResponse') }, null, 2));

function handleFileChange(event: Event): void {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0] ?? null;
  resetImageState();
  selectedFile.value = file;
  selectedFileName.value = file?.name ?? '';
  if (file) {
    inputPreviewUrl.value = URL.createObjectURL(file);
  }
  input.value = '';
}

function handleInputImageLoad(): void {
  const image = inputImageRef.value;
  if (!image) {
    return;
  }
  naturalSize.width = image.naturalWidth;
  naturalSize.height = image.naturalHeight;
  updateDisplaySize();
}

async function uploadSelectedImage(): Promise<string> {
  if (uploadId.value) {
    return uploadId.value;
  }
  if (!selectedFile.value) {
    throw new Error(t('pages.imageProcessing.noImageSelected'));
  }
  if (!authSessionState.sessionToken) {
    throw new Error(t('pages.imageProcessing.sessionMissing'));
  }

  uploadRunning.value = true;
  lastError.value = '';
  try {
    const body = new FormData();
    body.append('file', selectedFile.value);
    const response = await fetch(buildUploadUrl(), {
      method: 'POST',
      headers: {
        Authorization: `Bearer ${authSessionState.sessionToken}`,
      },
      body,
    });
    const payload = (await response.json()) as Record<string, unknown>;
    if (!response.ok) {
      throw new Error(asString(payload.message) || `${response.status} ${response.statusText}`.trim());
    }
    uploadId.value = asString(payload.upload_id);
    if (!uploadId.value) {
      throw new Error(t('pages.imageProcessing.uploadIdMissing'));
    }
    return uploadId.value;
  } finally {
    uploadRunning.value = false;
  }
}

async function handleUploadClick(): Promise<void> {
  try {
    await uploadSelectedImage();
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : t('pages.imageProcessing.unknownError');
  }
}

async function runImageProcess(): Promise<void> {
  if (!canProcess.value) {
    return;
  }
  running.value = true;
  lastError.value = '';
  clearOutputPreviews();
  try {
    const inputUploadId = await uploadSelectedImage();
    const response = await sendBoundCommand('image.process', {
      params: {
        ...commandPayload.value,
        input_upload_id: inputUploadId,
      },
    });
    lastResponse.value = response;
    if (!isOkResponse(response)) {
      throw new Error(response.message);
    }
    await buildOutputPreviews(outputs.value);
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : t('pages.imageProcessing.unknownError');
    lastResponse.value = null;
  } finally {
    running.value = false;
  }
}

async function buildOutputPreviews(items: ProcessOutput[]): Promise<void> {
  outputPreviews.value = items.map((item, index) => ({
    assetId: asString(item.asset_id) || `output-${index}`,
    role: asString(item.role) || asString(item.output_id) || `#${index + 1}`,
    url: asString(item.url),
    downloadUrl: asString(item.download_url),
    contentType: asString(item.content_type),
    objectUrl: '',
    state: isBrowserRenderable(asString(item.content_type)) ? 'idle' : 'unsupported',
    error: '',
  }));
  activeOutputAssetId.value = outputPreviews.value[0]?.assetId ?? '';
  await Promise.all(outputPreviews.value.map((preview) => loadOutputPreview(preview.assetId)));
}

async function loadOutputPreview(assetId: string): Promise<void> {
  const preview = outputPreviews.value.find((item) => item.assetId === assetId);
  if (!preview || !preview.url || preview.state === 'unsupported') {
    return;
  }
  preview.state = 'loading';
  try {
    const response = await fetch(resolveAssetUrl(preview.url), {
      headers: {
        Authorization: `Bearer ${authSessionState.sessionToken}`,
      },
    });
    if (!response.ok) {
      throw new Error(`${response.status} ${response.statusText}`.trim());
    }
    const blob = await response.blob();
    preview.objectUrl = URL.createObjectURL(blob);
    preview.state = 'ready';
  } catch (error) {
    preview.state = 'error';
    preview.error = error instanceof Error ? error.message : t('pages.imageProcessing.previewLoadFailed');
  }
}

function setPageProcessing(value: PageProcessing): void {
  form.pageProcessing = value;
  if (value !== 'selected_area') {
    clearSelectedArea();
  }
}

function setAreaMode(value: AreaMode): void {
  areaMode.value = value;
  clearSelectedArea();
}

function handleAreaPointerDown(event: PointerEvent): void {
  if (!inputStageRef.value || !naturalSize.width || !naturalSize.height) {
    return;
  }
  updateDisplaySize();
  const point = eventPoint(event);
  if (areaMode.value === 'points') {
    if (selectedPointCount.value >= pointKeys.length) {
      clearSelectedArea();
    }
    const key = pointKeys[Math.min(selectedPointCount.value, pointKeys.length - 1)];
    form.selectedArea[key] = displayToNatural(point);
    selectedPointCount.value = Math.min(selectedPointCount.value + 1, pointKeys.length);
    if (selectedPointCount.value === pointKeys.length) {
      normalizeSelectedAreaPoints();
      event.preventDefault();
    }
    return;
  }
  areaDraftStart.value = point;
  areaDraftEnd.value = point;
  updateRectangleArea(point, point);
  (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
}

function handleAreaPointerMove(event: PointerEvent): void {
  if (areaMode.value !== 'rectangle' || !areaDraftStart.value) {
    return;
  }
  const point = eventPoint(event);
  areaDraftEnd.value = point;
  updateRectangleArea(areaDraftStart.value, point);
}

function handleAreaPointerUp(event: PointerEvent): void {
  if (areaMode.value !== 'rectangle' || !areaDraftStart.value) {
    return;
  }
  const point = eventPoint(event);
  updateRectangleArea(areaDraftStart.value, point);
  areaDraftStart.value = null;
  areaDraftEnd.value = null;
  (event.currentTarget as HTMLElement).releasePointerCapture(event.pointerId);
}

function handleAreaPointerCancel(): void {
  areaDraftStart.value = null;
  areaDraftEnd.value = null;
}

function updateRectangleArea(start: { x: number; y: number }, end: { x: number; y: number }): void {
  const left = Math.min(start.x, end.x);
  const right = Math.max(start.x, end.x);
  const top = Math.min(start.y, end.y);
  const bottom = Math.max(start.y, end.y);
  form.selectedArea.left_top = displayToNatural({ x: left, y: top });
  form.selectedArea.right_top = displayToNatural({ x: right, y: top });
  form.selectedArea.right_down = displayToNatural({ x: right, y: bottom });
  form.selectedArea.left_down = displayToNatural({ x: left, y: bottom });
  selectedPointCount.value = 4;
}

function eventPoint(event: PointerEvent): { x: number; y: number } {
  const rect = inputStageRef.value?.getBoundingClientRect();
  if (!rect) {
    return { x: 0, y: 0 };
  }
  return {
    x: clamp(event.clientX - rect.left, 0, rect.width),
    y: clamp(event.clientY - rect.top, 0, rect.height),
  };
}

function displayToNatural(point: { x: number; y: number }): { x: number; y: number } {
  updateDisplaySize();
  const sx = naturalSize.width > 0 && displaySize.width > 0 ? naturalSize.width / displaySize.width : 1;
  const sy = naturalSize.height > 0 && displaySize.height > 0 ? naturalSize.height / displaySize.height : 1;
  return {
    x: Math.round(point.x * sx),
    y: Math.round(point.y * sy),
  };
}

function naturalToDisplay(point: { x: number; y: number }): { x: number; y: number } {
  updateDisplaySize();
  const sx = naturalSize.width > 0 ? displaySize.width / naturalSize.width : 1;
  const sy = naturalSize.height > 0 ? displaySize.height / naturalSize.height : 1;
  return {
    x: point.x * sx,
    y: point.y * sy,
  };
}

function updateDisplaySize(): void {
  const rect = inputStageRef.value?.getBoundingClientRect();
  displaySize.width = rect?.width ?? 0;
  displaySize.height = rect?.height ?? 0;
}

function clearSelectedArea(): void {
  form.selectedArea = emptyArea();
  selectedPointCount.value = 0;
  areaDraftStart.value = null;
  areaDraftEnd.value = null;
}

function hasSelectedArea(): boolean {
  const points = pointKeys.map((key) => form.selectedArea[key]);
  const xs = points.map((point) => point.x);
  const ys = points.map((point) => point.y);
  return Math.max(...xs) - Math.min(...xs) >= 8 && Math.max(...ys) - Math.min(...ys) >= 8;
}

function normalizeSelectedAreaPoints(): void {
  const points = pointKeys.map((key) => form.selectedArea[key]);
  const center = points.reduce((acc, point) => ({ x: acc.x + point.x / points.length, y: acc.y + point.y / points.length }), { x: 0, y: 0 });
  const sorted = [...points].sort((a, b) => Math.atan2(a.y - center.y, a.x - center.x) - Math.atan2(b.y - center.y, b.x - center.x));
  const top = sorted.slice().sort((a, b) => a.y - b.y).slice(0, 2).sort((a, b) => a.x - b.x);
  const bottom = sorted.slice().sort((a, b) => b.y - a.y).slice(0, 2).sort((a, b) => a.x - b.x);
  form.selectedArea.left_top = top[0];
  form.selectedArea.right_top = top[1];
  form.selectedArea.right_down = bottom[1];
  form.selectedArea.left_down = bottom[0];
}

function emptyArea(): Record<PointKey, { x: number; y: number }> {
  return {
    left_top: { x: 0, y: 0 },
    right_top: { x: 0, y: 0 },
    right_down: { x: 0, y: 0 },
    left_down: { x: 0, y: 0 },
  };
}

function resetImageState(): void {
  if (inputPreviewUrl.value) {
    URL.revokeObjectURL(inputPreviewUrl.value);
  }
  clearOutputPreviews();
  uploadId.value = '';
  lastResponse.value = null;
  lastError.value = '';
  naturalSize.width = 0;
  naturalSize.height = 0;
  clearSelectedArea();
}

function clearOutputPreviews(): void {
  for (const preview of outputPreviews.value) {
    if (preview.objectUrl) {
      URL.revokeObjectURL(preview.objectUrl);
    }
  }
  outputPreviews.value = [];
  activeOutputAssetId.value = '';
}

function buildUploadUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'https' : 'http';
  return `${protocol}://${resolveRuntimeHost()}:17082/api/uploads/images`;
}

function resolveAssetUrl(path: string): string {
  if (/^https?:\/\//i.test(path)) {
    return path;
  }
  const protocol = window.location.protocol === 'https:' ? 'https:' : 'http:';
  return `${protocol}//${window.location.hostname || '127.0.0.1'}:17082${path.startsWith('/') ? path : `/${path}`}`;
}

function isBrowserRenderable(contentType: string): boolean {
  const value = contentType.toLowerCase();
  return ['image/jpeg', 'image/jpg', 'image/png', 'image/webp', 'image/gif', 'image/bmp'].includes(value);
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}

onBeforeUnmount(() => {
  if (inputPreviewUrl.value) {
    URL.revokeObjectURL(inputPreviewUrl.value);
  }
  clearOutputPreviews();
});

void nextTick(() => updateDisplaySize());
</script>

<style scoped>
.preview-card {
  overflow: hidden;
  border: 1px solid rgb(226 232 240);
  border-radius: 1rem;
  background: rgb(255 255 255 / 0.9);
  box-shadow: 0 1px 2px rgb(15 23 42 / 0.06);
}

.preview-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 0.75rem;
  border-bottom: 1px solid rgb(241 245 249);
  padding: 0.75rem 1rem;
  color: rgb(15 23 42);
  font-size: 0.875rem;
  font-weight: 700;
}

.preview-surface {
  display: flex;
  min-height: 420px;
  align-items: center;
  justify-content: center;
  background: rgb(15 23 42);
  padding: 1rem;
}

.field-label {
  display: block;
  color: rgb(100 116 139);
  font-size: 0.75rem;
  font-weight: 700;
  letter-spacing: 0.04em;
  text-transform: uppercase;
}

.field-input {
  margin-top: 0.25rem;
  width: 100%;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.75rem;
  background: rgb(255 255 255);
  padding: 0.5rem 0.75rem;
  color: rgb(30 41 59);
  font-size: 0.875rem;
  font-weight: 600;
  outline: none;
}

.field-input:focus {
  border-color: rgb(34 211 238);
  box-shadow: 0 0 0 2px rgb(207 250 254);
}

.option-heading {
  margin-bottom: 0.5rem;
  color: rgb(100 116 139);
  font-size: 0.75rem;
  font-weight: 700;
  letter-spacing: 0.04em;
  text-transform: uppercase;
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

.format-button,
.primary-button,
.secondary-button {
  display: inline-flex;
  cursor: pointer;
  align-items: center;
  justify-content: center;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.75rem;
  background: rgb(255 255 255);
  padding: 0.5rem 0.75rem;
  color: rgb(51 65 85);
  font-size: 0.875rem;
  font-weight: 700;
}

.format-button-active,
.primary-button {
  border-color: rgb(8 145 178);
  background: rgb(8 145 178);
  color: rgb(255 255 255);
}

.secondary-button:hover,
.format-button:hover {
  border-color: rgb(103 232 249);
  color: rgb(14 116 144);
}

.primary-button:disabled,
.secondary-button:disabled {
  cursor: not-allowed;
  opacity: 0.5;
}

.result-thumb {
  border: 1px solid rgb(226 232 240);
  border-radius: 0.875rem;
  background: rgb(255 255 255);
  padding: 0.5rem;
  text-align: left;
}

.result-thumb-active {
  border-color: rgb(34 211 238);
  box-shadow: 0 0 0 2px rgb(207 250 254);
}
</style>
