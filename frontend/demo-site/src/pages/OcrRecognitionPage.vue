<template>
  <div class="space-y-6">
    <SectionPanel :title="t('pages.ocrRecognition.title')" :description="t('pages.ocrRecognition.subtitle')">
      <div class="grid gap-5 xl:grid-cols-[minmax(0,1.2fr)_minmax(340px,0.8fr)]">
        <div class="space-y-4">
          <div class="rounded-2xl border border-slate-200 bg-white/80 p-4 shadow-sm">
            <div class="flex flex-wrap items-center justify-between gap-3">
              <div>
                <p class="text-sm font-semibold text-slate-900">{{ t('pages.ocrRecognition.inputImage') }}</p>
                <p class="mt-1 text-xs text-slate-500">{{ selectedFileSummary }}</p>
              </div>
              <label class="primary-button">
                {{ t('pages.ocrRecognition.chooseImages') }}
                <input type="file" accept="image/*" multiple class="sr-only" @change="handleFileChange" />
              </label>
            </div>
          </div>

          <article class="preview-card">
            <div class="preview-header">
              <span>{{ t('pages.ocrRecognition.preview') }}</span>
              <StatusPill :label="inputPreviewUrl ? t('pages.ocrRecognition.localReady') : t('status.idle')" :tone="inputPreviewUrl ? 'success' : 'neutral'" />
            </div>
            <div class="preview-surface">
              <img v-if="inputPreviewUrl" :src="inputPreviewUrl" alt="" class="max-h-[420px] max-w-full object-contain" />
              <p v-else class="px-6 text-center text-sm text-slate-500">{{ t('pages.ocrRecognition.pickImageHint') }}</p>
            </div>
            <div v-if="selectedImages.length > 1" class="grid gap-2 border-t border-slate-100 p-3 sm:grid-cols-2">
              <button
                v-for="(image, index) in selectedImages"
                :key="image.previewUrl"
                type="button"
                class="thumbnail-button"
                :class="activeImageIndex === index ? 'thumbnail-button-active' : ''"
                @click="activeImageIndex = index"
              >
                <img :src="image.previewUrl" alt="" class="h-12 w-12 rounded-lg bg-slate-100 object-cover" />
                <span class="min-w-0 flex-1 truncate text-left">{{ image.file.name }}</span>
              </button>
            </div>
          </article>
        </div>

        <div class="space-y-4">
          <KeyValueGrid :items="runtimeItems" />
          <div class="grid grid-cols-2 gap-2">
            <button type="button" class="secondary-button" :disabled="selectedImages.length === 0 || uploadRunning" @click="handleUploadClick">
              {{ uploadRunning ? t('pages.ocrRecognition.uploading') : t('pages.ocrRecognition.uploadAll') }}
            </button>
            <button type="button" class="primary-button justify-center" :disabled="!canRunActive" @click="runExtractText">
              {{ runningAction === 'extract' ? t('pages.ocrRecognition.running') : t('pages.ocrRecognition.extractText') }}
            </button>
          </div>
          <div class="grid grid-cols-2 gap-2">
            <button type="button" class="secondary-button" :disabled="!canRunActive" @click="runBarcodeDetect">
              {{ runningAction === 'barcode' ? t('pages.ocrRecognition.running') : t('pages.ocrRecognition.detectBarcode') }}
            </button>
            <button type="button" class="secondary-button" :disabled="!lastTaskId || Boolean(runningAction)" @click="queryOcrTask">
              {{ t('pages.ocrRecognition.queryTask') }}
            </button>
          </div>
          <button type="button" class="secondary-button w-full justify-center" :disabled="!lastTaskId || Boolean(runningAction)" @click="cancelOcrTask">
            {{ t('pages.ocrRecognition.cancelTask') }}
          </button>
          <p class="rounded-xl border border-cyan-100 bg-cyan-50 px-3 py-2 text-xs font-medium text-cyan-800">
            {{ t('pages.ocrRecognition.activeOnlyNotice') }}
          </p>
          <div v-if="lastResponse || lastError" class="result-summary">
            <div class="flex items-center justify-between gap-3">
              <p class="text-sm font-semibold text-slate-900">{{ t('pages.ocrRecognition.recentResult') }}</p>
              <StatusPill :label="lastError ? t('status.error') : t('status.success')" :tone="lastError ? 'danger' : 'success'" />
            </div>
            <KeyValueGrid class="mt-3" :items="summaryItems" />
            <p class="mt-3 text-xs font-medium text-slate-500">{{ t('pages.ocrRecognition.fullResultBelow') }}</p>
          </div>
          <p v-if="lastError" class="rounded-xl border border-rose-200 bg-rose-50 px-3 py-2 text-sm text-rose-600">{{ lastError }}</p>
        </div>
      </div>
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-[0.9fr_1.1fr]">
      <SectionPanel :title="t('pages.ocrRecognition.ocrOptions')" :description="t('pages.ocrRecognition.ocrOptionsDescription')">
        <div class="space-y-4">
          <label class="field-label">
            {{ t('pages.ocrRecognition.outputFormat') }}
            <select v-model="ocrFormat" class="field-input">
              <option v-for="format in ocrFormats" :key="format" :value="format">{{ format }}</option>
            </select>
          </label>
          <label class="field-label">
            {{ t('pages.ocrRecognition.exportType') }}
            <select v-model="exportType" class="field-input">
              <option value="multi-page">{{ t('pages.ocrRecognition.multiPage') }}</option>
              <option value="single-page">{{ t('pages.ocrRecognition.singlePage') }}</option>
            </select>
          </label>
          <label class="field-label">
            {{ exportType === 'single-page' ? t('pages.ocrRecognition.outputDir') : t('pages.ocrRecognition.outputPath') }}
            <input v-if="exportType === 'single-page'" v-model="outputDir" class="field-input" />
            <input v-else v-model="outputPath" class="field-input" />
          </label>
          <div class="grid grid-cols-2 gap-2">
            <label class="field-label">
              {{ t('pages.ocrRecognition.encoding') }}
              <input v-model="ocrParams.encoding" class="field-input" />
            </label>
            <label class="field-label">
              {{ t('pages.ocrRecognition.quality') }}
              <input v-model.number="ocrParams.quality" type="number" min="1" max="100" class="field-input" />
            </label>
          </div>
          <button type="button" class="primary-button w-full justify-center" :disabled="!canRun" @click="runOcrRecognize">
            {{ runningAction === 'recognize' ? t('pages.ocrRecognition.running') : t('pages.ocrRecognition.submitRecognize') }}
          </button>
          <p class="rounded-xl border border-slate-200 bg-slate-50 px-3 py-2 text-xs font-medium text-slate-600">
            {{ t('pages.ocrRecognition.recognizeNotice') }}
          </p>
        </div>
      </SectionPanel>

      <div class="space-y-6">
        <SectionPanel :title="t('common.results')" :description="t('pages.ocrRecognition.resultsDescription')">
          <KeyValueGrid :items="resultItems" />
          <DataTableCard v-if="textRows.length > 0" class="mt-4" :columns="textColumns" :rows="textRows" />
          <DataTableCard v-if="barcodeRows.length > 0" class="mt-4" :columns="barcodeColumns" :rows="barcodeRows" />
        </SectionPanel>

        <JsonPanel :title="t('pages.ocrRecognition.requestJson')" :caption="t('common.request')" :payload="requestPreview" />
        <JsonPanel :title="t('pages.ocrRecognition.responseJson')" :caption="t('common.response')" :payload="responsePreview" />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, reactive, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import DataTableCard from '../components/blocks/DataTableCard.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import KeyValueGrid from '../components/blocks/KeyValueGrid.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState, sendBoundCommand } from '../services/auth-session';
import { isOkResponse, resolveRuntimeHost } from '../services/protocol';

type RunningAction = '' | 'extract' | 'recognize' | 'barcode' | 'get' | 'cancel';

interface TextBlock {
  text?: string;
  x?: number;
  y?: number;
  width?: number;
  height?: number;
  confidence?: number;
}

interface BarcodeResult {
  format_name?: string;
  text?: string;
  points?: Array<{ x?: number; y?: number }>;
}

interface SelectedImage {
  file: File;
  previewUrl: string;
  uploadId: string;
}

const { t } = useI18n();

const ocrFormats = ['txt', 'pdf', 'docx', 'xlsx', 'ofd', 'json'];
const selectedImages = ref<SelectedImage[]>([]);
const activeImageIndex = ref(0);
const uploadId = ref('');
const uploadRunning = ref(false);
const runningAction = ref<RunningAction>('');
const lastResponse = ref<Record<string, unknown> | null>(null);
const lastRequest = ref<Record<string, unknown> | null>(null);
const lastError = ref('');
const lastTaskId = ref('');
const ocrFormat = ref('txt');
const exportType = ref<'multi-page' | 'single-page'>('multi-page');
const outputPath = ref(defaultOutputPath('txt'));
const outputDir = ref(defaultOutputDir());
const ocrParams = reactive({
  encoding: 'utf-8',
  quality: 90,
});

const activeImage = computed(() => selectedImages.value[activeImageIndex.value] ?? null);
const inputPreviewUrl = computed(() => activeImage.value?.previewUrl ?? '');
const selectedFileSummary = computed(() => {
  if (selectedImages.value.length === 0) {
    return t('pages.ocrRecognition.noImageSelected');
  }
  if (selectedImages.value.length === 1) {
    return selectedImages.value[0].file.name;
  }
  return t('pages.ocrRecognition.selectedImageCount', { count: selectedImages.value.length });
});
const uploadedCount = computed(() => selectedImages.value.filter((image) => image.uploadId).length);
const uploadIds = computed(() => selectedImages.value.map((image) => image.uploadId).filter(Boolean));
const canRun = computed(() => Boolean(selectedImages.value.length > 0 && !uploadRunning.value && !runningAction.value));
const canRunActive = computed(() => Boolean(activeImage.value && !uploadRunning.value && !runningAction.value));
const runtimeItems = computed(() => [
  { label: t('pages.imageProcessing.commandState'), value: t(`status.${authSessionState.commandState}`) },
  { label: t('pages.imageProcessing.session'), value: authSessionState.sessionToken ? t('pages.imageProcessing.bound') : t('pages.imageProcessing.notBound') },
  { label: t('pages.ocrRecognition.images'), value: selectedImages.value.length ? `${selectedImages.value.length}` : t('common.notSet') },
  { label: t('pages.imageProcessing.uploadId'), value: uploadStatusLabel.value, monospace: true },
  { label: t('pages.imageProcessing.taskId'), value: lastTaskId.value || t('common.notSet'), monospace: true },
]);
const uploadStatusLabel = computed(() => {
  if (selectedImages.value.length === 0) {
    return t('common.notSet');
  }
  if (selectedImages.value.length === 1) {
    return selectedImages.value[0].uploadId || t('common.notSet');
  }
  return `${uploadedCount.value}/${selectedImages.value.length}`;
});
const resultItems = computed(() => {
  const data = lastResponse.value?.data as Record<string, unknown> | undefined;
  const task = data?.task && typeof data.task === 'object' ? (data.task as Record<string, unknown>) : null;
  const outputPaths = collectOutputPaths(data, task);
  return [
    { label: t('labels.status'), value: lastError.value || (lastResponse.value ? String(lastResponse.value.message) : t('status.idle')) },
    { label: t('labels.code'), value: lastResponse.value ? String(lastResponse.value.code) : '-' },
    { label: t('pages.ocrRecognition.taskStatus'), value: task ? asString(task.status) : '-' },
    { label: t('pages.ocrRecognition.progress'), value: task ? String(task.progress ?? '-') : '-' },
    { label: t('labels.outputPath'), value: task ? asString(task.output_path) : asString(data?.output_path), monospace: true },
    { label: t('pages.ocrRecognition.outputCount'), value: outputPaths.length ? String(outputPaths.length) : '-' },
  ];
});
const summaryItems = computed(() => {
  const data = lastResponse.value?.data as Record<string, unknown> | undefined;
  const task = data?.task && typeof data.task === 'object' ? (data.task as Record<string, unknown>) : null;
  const outputPaths = collectOutputPaths(data, task);
  return [
    { label: t('labels.status'), value: lastError.value || (task ? asString(task.status) : asString(lastResponse.value?.message) || t('status.idle')) },
    { label: t('pages.imageProcessing.taskId'), value: lastTaskId.value || '-', monospace: true },
    { label: t('pages.ocrRecognition.progress'), value: task ? `${String(task.progress ?? '-')}%` : '-' },
    { label: t('pages.ocrRecognition.outputCount'), value: outputPaths.length ? String(outputPaths.length) : '-' },
    { label: t('labels.outputPath'), value: outputPaths[0] || asString(task?.output_path) || asString(data?.output_path) || '-', monospace: true },
  ];
});
const textBlocks = computed<TextBlock[]>(() => {
  const data = lastResponse.value?.data as Record<string, unknown> | undefined;
  return Array.isArray(data?.blocks) ? (data.blocks as TextBlock[]) : [];
});
const barcodes = computed<BarcodeResult[]>(() => {
  const data = lastResponse.value?.data as Record<string, unknown> | undefined;
  return Array.isArray(data?.barcodes) ? (data.barcodes as BarcodeResult[]) : [];
});
const textRows = computed(() =>
  textBlocks.value.map((block, index) => ({
    index: String(index + 1),
    text: asString(block.text),
    box: `${Math.round(block.x ?? 0)}, ${Math.round(block.y ?? 0)}, ${Math.round(block.width ?? 0)} x ${Math.round(block.height ?? 0)}`,
    confidence: typeof block.confidence === 'number' ? block.confidence.toFixed(2) : '-',
  })),
);
const barcodeRows = computed(() =>
  barcodes.value.map((barcode, index) => ({
    index: String(index + 1),
    format: asString(barcode.format_name),
    text: asString(barcode.text),
    points: Array.isArray(barcode.points) ? String(barcode.points.length) : '0',
  })),
);
const textColumns = computed(() => [
  { key: 'index', label: '#', monospace: true },
  { key: 'text', label: t('labels.value') },
  { key: 'box', label: t('pages.ocrRecognition.box'), monospace: true },
  { key: 'confidence', label: t('labels.confidence'), align: 'right', monospace: true },
]);
const barcodeColumns = computed(() => [
  { key: 'index', label: '#', monospace: true },
  { key: 'format', label: t('labels.type') },
  { key: 'text', label: t('labels.value') },
  { key: 'points', label: t('pages.ocrRecognition.points'), align: 'right', monospace: true },
]);
const requestPreview = computed(() => JSON.stringify(lastRequest.value ?? buildRecognizeRequestPreview(), null, 2));
const responsePreview = computed(() => JSON.stringify(lastResponse.value ?? { error: lastError.value || t('pages.imageProcessing.noResponse') }, null, 2));

watch(ocrFormat, (format) => {
  outputPath.value = defaultOutputPath(format);
});

function handleFileChange(event: Event): void {
  const input = event.target as HTMLInputElement;
  const files = Array.from(input.files ?? []);
  resetImageState();
  selectedImages.value = files.map((file) => ({
    file,
    previewUrl: URL.createObjectURL(file),
    uploadId: '',
  }));
  activeImageIndex.value = 0;
  input.value = '';
}

async function uploadSelectedImages(): Promise<string[]> {
  if (selectedImages.value.length === 0) {
    throw new Error(t('pages.ocrRecognition.noImageSelected'));
  }
  if (!authSessionState.sessionToken) {
    throw new Error(t('pages.imageProcessing.sessionMissing'));
  }
  uploadRunning.value = true;
  lastError.value = '';
  try {
    for (const image of selectedImages.value) {
      if (!image.uploadId) {
        image.uploadId = await uploadImageFile(image.file);
      }
    }
    uploadId.value = selectedImages.value[0]?.uploadId ?? '';
    return selectedImages.value.map((image) => image.uploadId).filter(Boolean);
  } finally {
    uploadRunning.value = false;
  }
}

async function uploadActiveImage(): Promise<string> {
  const image = activeImage.value;
  if (!image) {
    throw new Error(t('pages.ocrRecognition.noImageSelected'));
  }
  if (!authSessionState.sessionToken) {
    throw new Error(t('pages.imageProcessing.sessionMissing'));
  }
  uploadRunning.value = true;
  lastError.value = '';
  try {
    if (!image.uploadId) {
      image.uploadId = await uploadImageFile(image.file);
    }
    uploadId.value = image.uploadId;
    return image.uploadId;
  } finally {
    uploadRunning.value = false;
  }
}

async function uploadImageFile(file: File): Promise<string> {
  const body = new FormData();
  body.append('file', file);
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
  const nextUploadId = asString(payload.upload_id);
  if (!nextUploadId) {
    throw new Error(t('pages.imageProcessing.uploadIdMissing'));
  }
  return nextUploadId;
}

async function handleUploadClick(): Promise<void> {
  try {
    await uploadSelectedImages();
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : t('pages.imageProcessing.unknownError');
  }
}

async function runExtractText(): Promise<void> {
  await runActiveCommandWithUpload('extract', 'ocr.extract_text', (inputUploadId) => ({ input_upload_id: inputUploadId }));
}

async function runOcrRecognize(): Promise<void> {
  await runCommandWithAllUploads('recognize', 'ocr.recognize', (inputUploadIds) => ({
    input_upload_ids: inputUploadIds,
    ...(exportType.value === 'single-page' ? { output_dir: outputDir.value } : { output_path: outputPath.value }),
    format: ocrFormat.value,
    exportType: exportType.value,
    encoding: ocrParams.encoding,
    quality: ocrParams.quality,
  }));
}

async function runBarcodeDetect(): Promise<void> {
  await runActiveCommandWithUpload('barcode', 'recognition.barcode_detect', (inputUploadId) => ({
    input_upload_id: inputUploadId,
    formats: ['qrcode', 'pdf417', 'code128', 'ean13', 'ean8'],
  }));
}

async function queryOcrTask(): Promise<void> {
  if (!lastTaskId.value) {
    return;
  }
  await runCommand('get', 'ocr.get', { task_id: lastTaskId.value });
}

async function cancelOcrTask(): Promise<void> {
  if (!lastTaskId.value) {
    return;
  }
  await runCommand('cancel', 'ocr.cancel', { task_id: lastTaskId.value });
}

async function runCommand(action: RunningAction, method: string, params: Record<string, unknown>): Promise<void> {
  runningAction.value = action;
  lastError.value = '';
  lastRequest.value = { method, params };
  try {
    const response = await sendBoundCommand(method, { params });
    lastResponse.value = response;
    if (!isOkResponse(response)) {
      throw new Error(response.message);
    }
    const data = response.data as Record<string, unknown>;
    const taskId = asString(data.task_id) || asString((data.task as Record<string, unknown> | undefined)?.task_id);
    if (taskId) {
      lastTaskId.value = taskId;
    }
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : t('pages.imageProcessing.unknownError');
  } finally {
    runningAction.value = '';
  }
}

async function runActiveCommandWithUpload(
  action: RunningAction,
  method: string,
  buildParams: (inputUploadId: string) => Record<string, unknown>,
): Promise<void> {
  try {
    const inputUploadId = await uploadActiveImage();
    await runCommand(action, method, buildParams(inputUploadId));
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : t('pages.imageProcessing.unknownError');
  }
}

async function runCommandWithAllUploads(
  action: RunningAction,
  method: string,
  buildParams: (inputUploadIds: string[]) => Record<string, unknown>,
): Promise<void> {
  try {
    const inputUploadIds = await uploadSelectedImages();
    await runCommand(action, method, buildParams(inputUploadIds));
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : t('pages.imageProcessing.unknownError');
  }
}

function buildRecognizeRequestPreview(): Record<string, unknown> {
  return {
    method: 'ocr.recognize',
    params: {
      input_upload_ids: uploadIds.value.length ? uploadIds.value : ['<upload_id>'],
      ...(exportType.value === 'single-page' ? { output_dir: outputDir.value } : { output_path: outputPath.value }),
      format: ocrFormat.value,
      exportType: exportType.value,
      encoding: ocrParams.encoding,
      quality: ocrParams.quality,
    },
  };
}

function buildUploadUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'https' : 'http';
  return `${protocol}://${resolveRuntimeHost()}:17082/api/uploads/images`;
}

function defaultOutputPath(format: string): string {
  return `/tmp/czur-sdk-ocr-demo-${Date.now().toString(36)}.${format}`;
}

function defaultOutputDir(): string {
  return `/tmp/czur-sdk-ocr-demo-${Date.now().toString(36)}`;
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function collectOutputPaths(data?: Record<string, unknown>, task?: Record<string, unknown> | null): string[] {
  const taskPaths = Array.isArray(task?.output_paths) ? (task?.output_paths as unknown[]).map(asString).filter(Boolean) : [];
  if (taskPaths.length > 0) {
    return taskPaths;
  }
  const dataPaths = Array.isArray(data?.output_paths) ? (data?.output_paths as unknown[]).map(asString).filter(Boolean) : [];
  if (dataPaths.length > 0) {
    return dataPaths;
  }
  const singlePath = asString(task?.output_path) || asString(data?.output_path);
  return singlePath ? [singlePath] : [];
}

function resetImageState(): void {
  selectedImages.value.forEach((image) => URL.revokeObjectURL(image.previewUrl));
  selectedImages.value = [];
  activeImageIndex.value = 0;
  uploadId.value = '';
  lastResponse.value = null;
  lastRequest.value = null;
  lastError.value = '';
  lastTaskId.value = '';
}

onBeforeUnmount(() => {
  selectedImages.value.forEach((image) => URL.revokeObjectURL(image.previewUrl));
});
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
  font-size: 0.875rem;
  color: rgb(30 41 59);
  font-weight: 600;
  outline: none;
}

.field-input:focus {
  border-color: rgb(34 211 238);
  box-shadow: 0 0 0 2px rgb(207 250 254);
}

.result-summary {
  border: 1px solid rgb(186 230 253);
  border-radius: 0.875rem;
  background: rgb(240 249 255);
  padding: 0.875rem;
  box-shadow: 0 1px 2px rgb(15 23 42 / 0.05);
}

.thumbnail-button {
  display: flex;
  min-width: 0;
  cursor: pointer;
  align-items: center;
  gap: 0.625rem;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.75rem;
  background: rgb(255 255 255);
  padding: 0.5rem;
  color: rgb(51 65 85);
  font-size: 0.75rem;
  font-weight: 700;
}

.thumbnail-button-active {
  border-color: rgb(34 211 238);
  background: rgb(236 254 255);
  color: rgb(22 78 99);
}

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

.primary-button {
  border-color: rgb(8 145 178);
  background: rgb(8 145 178);
  color: rgb(255 255 255);
}

.secondary-button:hover {
  border-color: rgb(103 232 249);
  color: rgb(14 116 144);
}

.primary-button:disabled,
.secondary-button:disabled {
  cursor: not-allowed;
  opacity: 0.5;
}
</style>
