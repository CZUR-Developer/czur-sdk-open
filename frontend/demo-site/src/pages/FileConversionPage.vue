<template>
  <div class="space-y-6">
    <SectionPanel :title="t('pages.fileConversion.title')" :description="t('pages.fileConversion.subtitle')">
      <div class="grid gap-5 xl:grid-cols-[minmax(0,1.15fr)_minmax(340px,0.85fr)]">
        <div class="space-y-4">
          <div class="rounded-2xl border border-slate-200 bg-white/80 p-4 shadow-sm">
            <div class="grid gap-3 lg:grid-cols-[minmax(0,1fr)_160px_140px_auto] lg:items-end">
              <div>
                <p class="text-sm font-semibold text-slate-900">{{ t('pages.fileConversion.inputSource') }}</p>
                <p class="mt-1 text-xs text-slate-500">{{ selectedFileSummary }}</p>
              </div>
              <label class="field-label min-w-[160px]">
                {{ t('pages.fileConversion.sourceType') }}
                <select v-model="sourceKind" class="field-input">
                  <option value="images">{{ t('pages.fileConversion.inputImages') }}</option>
                  <option value="document">{{ t('pages.fileConversion.inputDocument') }}</option>
                </select>
              </label>
              <label class="field-label">
                {{ t('pages.fileConversion.pages') }}
                <input v-model="pages" :disabled="sourceKind === 'images'" class="field-input" />
              </label>
              <label class="primary-button justify-center">
                {{ sourceKind === 'images' ? t('pages.fileConversion.chooseImages') : t('pages.fileConversion.chooseDocument') }}
                <input type="file" :accept="sourceAccept" :multiple="sourceKind === 'images'" class="sr-only" @change="handleFileChange" />
              </label>
            </div>
          </div>

          <article class="preview-card">
            <div class="preview-header">
              <span>{{ t('pages.fileConversion.preview') }}</span>
              <StatusPill :label="selectedImages.length ? t('pages.fileConversion.localReady') : t('status.idle')" :tone="selectedImages.length ? 'success' : 'neutral'" />
            </div>
            <div class="preview-surface">
              <img
                v-if="activePreviewUrl"
                :src="activePreviewUrl"
                alt=""
                class="max-h-[420px] max-w-full cursor-zoom-in object-contain"
                @click="openActiveImageViewer"
              />
              <div v-else-if="selectedImages.length" class="px-6 text-center text-sm text-slate-500">
                <p class="font-mono">{{ selectedImages[0].file.name }}</p>
                <p class="mt-2">{{ t('pages.fileConversion.documentPreviewHint') }}</p>
              </div>
              <p v-else class="px-6 text-center text-sm text-slate-500">{{ sourceKind === 'images' ? t('pages.fileConversion.pickImageHint') : t('pages.fileConversion.pickDocumentHint') }}</p>
            </div>
            <div v-if="sourceKind === 'images' && selectedImages.length > 1" class="grid gap-2 border-t border-slate-100 p-3 sm:grid-cols-2">
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
              {{ uploadRunning ? t('pages.fileConversion.uploading') : t('pages.fileConversion.uploadAll') }}
            </button>
            <button type="button" class="primary-button justify-center" :disabled="!canConvert" @click="runConvert">
              {{ running ? t('pages.fileConversion.converting') : t('pages.fileConversion.convert') }}
            </button>
          </div>
          <p v-if="lastError" class="rounded-xl border border-rose-200 bg-rose-50 px-3 py-2 text-sm text-rose-600">{{ lastError }}</p>
          <div v-if="lastResponse" class="result-summary">
            <div class="flex items-center justify-between gap-3">
              <p class="text-sm font-semibold text-slate-900">{{ t('pages.fileConversion.recentResult') }}</p>
              <StatusPill :label="lastError ? t('status.error') : t('status.success')" :tone="lastError ? 'danger' : 'success'" />
            </div>
            <KeyValueGrid class="mt-3" :items="resultItems" />
          </div>
        </div>
      </div>
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-[0.85fr_1.15fr]">
      <SectionPanel :title="t('pages.fileConversion.options')" :description="t('common.parameters')">
        <div class="space-y-4">
          <div class="grid gap-3 sm:grid-cols-2">
            <label class="field-label">
              {{ t('pages.fileConversion.targetType') }}
              <select v-model="targetType" class="field-input">
                <option value="png">PNG</option>
                <option value="jpg">JPG</option>
                <option value="tiff">TIFF</option>
                <option value="pdf">PDF</option>
                <option value="ofd">OFD</option>
              </select>
            </label>
            <label class="field-label">
              {{ t('pages.fileConversion.exportType') }}
              <select v-model="exportType" class="field-input">
                <option value="multi-page" :disabled="targetType === 'png' || targetType === 'jpg'">{{ t('pages.fileConversion.multiPage') }}</option>
                <option value="single-page">{{ t('pages.fileConversion.singlePage') }}</option>
              </select>
            </label>
          </div>

          <label class="field-label">
            {{ exportType === 'single-page' ? t('pages.fileConversion.outputDir') : t('pages.fileConversion.outputPath') }}
            <input v-if="exportType === 'single-page'" v-model="outputDir" class="field-input" />
            <input v-else v-model="outputPath" class="field-input" />
          </label>

          <div class="grid gap-3 sm:grid-cols-3">
            <label class="field-label">
              {{ t('pages.fileConversion.quality') }}
              <input v-model.number="quality" type="number" min="1" max="100" class="field-input" />
            </label>
            <label class="field-label">
              {{ t('pages.fileConversion.tiffColor') }}
              <select v-model="tiffColor" class="field-input">
                <option value="color">{{ t('pages.fileConversion.color') }}</option>
                <option value="grayscale">{{ t('pages.fileConversion.grayscale') }}</option>
              </select>
            </label>
            <label class="field-label">
              {{ t('pages.fileConversion.tiffCompression') }}
              <select v-model="tiffCompression" class="field-input">
                <option value="lzw">LZW</option>
                <option value="none">{{ t('pages.fileConversion.none') }}</option>
                <option value="jpeg">JPEG</option>
                <option value="group4">Group4</option>
              </select>
            </label>
          </div>

          <p v-if="multiPageImageFormatBlocked" class="rounded-xl border border-amber-200 bg-amber-50 px-3 py-2 text-xs font-medium text-amber-800">
            {{ t('pages.fileConversion.multiPageImageFormatBlocked') }}
          </p>
        </div>
      </SectionPanel>

      <div class="space-y-6">
        <SectionPanel :title="t('common.results')" :description="t('common.response')">
          <div v-if="outputPreviews.length" class="grid gap-3 sm:grid-cols-2">
            <article v-for="preview in outputPreviews" :key="preview.assetId" class="rounded-xl border border-slate-200 bg-white p-3">
              <div class="flex items-center justify-between gap-3 text-xs font-semibold text-slate-500">
                <span class="truncate">{{ preview.assetId }}</span>
                <span>{{ preview.contentType }}</span>
              </div>
              <img
                v-if="preview.objectUrl"
                :src="preview.objectUrl"
                alt=""
                class="mt-3 h-48 w-full cursor-zoom-in rounded-lg bg-slate-100 object-contain"
                @click="openOutputImageViewer(preview)"
              />
              <p v-else class="mt-3 rounded-lg bg-slate-50 px-3 py-8 text-center text-sm text-slate-500">{{ preview.path }}</p>
            </article>
          </div>
          <KeyValueGrid v-else :items="resultItems" />
        </SectionPanel>

        <JsonPanel :title="t('pages.fileConversion.requestJson')" :caption="t('common.request')" :payload="requestPreview" />
        <JsonPanel :title="t('pages.fileConversion.responseJson')" :caption="t('common.response')" :payload="responsePreview" />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import JsonPanel from '../components/blocks/JsonPanel.vue';
import KeyValueGrid from '../components/blocks/KeyValueGrid.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState, sendBoundCommand } from '../services/auth-session';
import { openImageViewer } from '../services/image-viewer';
import { isOkResponse, resolveRuntimeHost } from '../services/protocol';

type TargetType = 'png' | 'jpg' | 'tiff' | 'pdf' | 'ofd';
type ExportType = 'multi-page' | 'single-page';
type SourceKind = 'images' | 'document';
type DocumentType = 'pdf' | 'ofd' | 'tiff';

interface SelectedImage {
  file: File;
  previewUrl: string;
  uploadId: string;
}

interface OutputPreview {
  assetId: string;
  path: string;
  contentType: string;
  objectUrl: string;
}

const { t } = useI18n();

const selectedImages = ref<SelectedImage[]>([]);
const activeImageIndex = ref(0);
const uploadRunning = ref(false);
const running = ref(false);
const lastError = ref('');
const lastResponse = ref<Record<string, unknown> | null>(null);
const lastRequest = ref<Record<string, unknown> | null>(null);
const outputPreviews = ref<OutputPreview[]>([]);

const sourceKind = ref<SourceKind>('images');
const documentType = ref<DocumentType>('pdf');
const pages = ref('all');
const targetType = ref<TargetType>('pdf');
const exportType = ref<ExportType>('multi-page');
const outputPath = ref(defaultOutputPath(targetType.value));
const outputDir = ref(defaultOutputDir());
const quality = ref(90);
const tiffColor = ref('color');
const tiffCompression = ref('lzw');

const activePreviewUrl = computed(() => selectedImages.value[activeImageIndex.value]?.previewUrl ?? '');
const uploadIds = computed(() => selectedImages.value.map((image) => image.uploadId).filter(Boolean));
const uploadedCount = computed(() => uploadIds.value.length);
const sourceAccept = computed(() => (sourceKind.value === 'images' ? 'image/*' : '.pdf,.ofd,.tif,.tiff,application/pdf,application/vnd.ofd,image/tiff'));
const multiPageImageFormatBlocked = computed(() => (targetType.value === 'png' || targetType.value === 'jpg') && exportType.value === 'multi-page');
const canConvert = computed(() => {
  if (uploadRunning.value || running.value || multiPageImageFormatBlocked.value) {
    return false;
  }
  return selectedImages.value.length > 0;
});

const selectedFileSummary = computed(() => {
  if (selectedImages.value.length === 0) {
    return sourceKind.value === 'images' ? t('pages.fileConversion.noImageSelected') : t('pages.fileConversion.noDocumentSelected');
  }
  if (selectedImages.value.length === 1) {
    return selectedImages.value[0].file.name;
  }
  return t('pages.fileConversion.selectedImageCount', { count: selectedImages.value.length });
});

const uploadStatusLabel = computed(() => {
  if (selectedImages.value.length === 0) {
    return t('common.notSet');
  }
  if (selectedImages.value.length === 1) {
    return selectedImages.value[0].uploadId || t('common.notSet');
  }
  return `${uploadedCount.value}/${selectedImages.value.length}`;
});

const runtimeItems = computed(() => [
  { label: t('pages.imageProcessing.commandState'), value: t(`status.${authSessionState.commandState}`) },
  { label: t('pages.imageProcessing.session'), value: authSessionState.sessionToken ? t('pages.imageProcessing.bound') : t('pages.imageProcessing.notBound') },
  { label: t('pages.fileConversion.sourceType'), value: sourceKind.value === 'document' ? documentType.value.toUpperCase() : t('pages.fileConversion.inputImages') },
  { label: t('pages.fileConversion.images'), value: sourceKind.value === 'images' && selectedImages.value.length ? String(selectedImages.value.length) : t('common.notSet') },
  { label: t('pages.imageProcessing.uploadId'), value: uploadStatusLabel.value, monospace: true },
]);

const requestPayload = computed(() => {
  const ids = uploadIds.value.length ? uploadIds.value : selectedImages.value.map((_, index) => `<upload_id_${index + 1}>`);
  if (sourceKind.value === 'document') {
    return {
      source: {
        type: documentType.value,
        input_upload_id: ids[0],
        pages: pages.value || 'all',
      },
      target: {
        type: targetType.value,
        ...(exportType.value === 'single-page' ? { dir: outputDir.value } : { path: outputPath.value }),
      },
      options: {
        export_type: exportType.value,
        quality: quality.value,
        tiff_color: tiffColor.value,
        tiff_compression: tiffCompression.value,
      },
    };
  }
  const params: Record<string, unknown> = {
    source: {
      type: ids.length > 1 ? 'images' : 'image',
      input_upload_ids: ids,
    },
    target: {
      type: targetType.value,
      ...(exportType.value === 'single-page' ? { dir: outputDir.value } : { path: outputPath.value }),
    },
    options: {
      export_type: exportType.value,
      quality: quality.value,
      tiff_color: tiffColor.value,
      tiff_compression: tiffCompression.value,
    },
  };
  return params;
});

const requestPreview = computed(() => JSON.stringify({ method: 'file.convert', params: requestPayload.value }, null, 2));
const responsePreview = computed(() => JSON.stringify(lastResponse.value ?? { data: null }, null, 2));

const resultItems = computed(() => {
  const data = asRecord(lastResponse.value?.data);
  const outputPaths = collectStrings(data.output_paths);
  return [
    { label: t('labels.status'), value: lastError.value || (lastResponse.value ? asString(lastResponse.value.message) : t('status.idle')) },
    { label: t('labels.code'), value: lastResponse.value ? String(lastResponse.value.code) : '-' },
    { label: 'output_format', value: asString(data.output_format) || targetType.value },
    { label: t('labels.outputPath'), value: asString(data.output_path) || '-', monospace: true },
    { label: 'output_paths', value: outputPaths.length ? outputPaths.join(', ') : '-', monospace: true },
  ];
});

watch(targetType, (next) => {
  outputPath.value = defaultOutputPath(next);
  if (next === 'png' || next === 'jpg') {
    exportType.value = 'single-page';
  }
});

watch(sourceKind, () => {
  clearSelectedImages();
  activeImageIndex.value = 0;
  pages.value = 'all';
  lastError.value = '';
  lastResponse.value = null;
  lastRequest.value = null;
  clearOutputPreviews();
});

function handleFileChange(event: Event): void {
  const input = event.target as HTMLInputElement;
  clearSelectedImages();
  const files = Array.from(input.files ?? []);
  const selected = sourceKind.value === 'document' ? files.slice(0, 1) : files;
  selectedImages.value = selected.map((file) => ({
    file,
    previewUrl: sourceKind.value === 'images' && file.type.startsWith('image/') ? URL.createObjectURL(file) : '',
    uploadId: '',
  }));
  if (sourceKind.value === 'document' && selected[0]) {
    documentType.value = inferDocumentType(selected[0]);
  }
  activeImageIndex.value = 0;
  lastError.value = '';
  lastResponse.value = null;
  lastRequest.value = null;
  clearOutputPreviews();
  input.value = '';
}

async function handleUploadClick(): Promise<void> {
  try {
    await uploadSelectedImages();
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : t('pages.imageProcessing.unknownError');
  }
}

async function runConvert(): Promise<void> {
  if (!canConvert.value) {
    return;
  }
  running.value = true;
  lastError.value = '';
  clearOutputPreviews();
  try {
    await uploadSelectedImages();
    lastRequest.value = { method: 'file.convert', params: requestPayload.value };
    const response = await sendBoundCommand('file.convert', { params: requestPayload.value });
    lastResponse.value = response;
    if (!isOkResponse(response)) {
      throw new Error(response.message);
    }
    await buildOutputPreviews(response.data);
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : t('pages.imageProcessing.unknownError');
  } finally {
    running.value = false;
  }
}

async function uploadSelectedImages(): Promise<string[]> {
  if (selectedImages.value.length === 0) {
    throw new Error(sourceKind.value === 'images' ? t('pages.fileConversion.noImageSelected') : t('pages.fileConversion.noDocumentSelected'));
  }
  if (!authSessionState.sessionToken) {
    throw new Error(t('pages.imageProcessing.sessionMissing'));
  }
  uploadRunning.value = true;
  try {
    for (const image of selectedImages.value) {
      if (!image.uploadId) {
        image.uploadId = await uploadImageFile(image.file);
      }
    }
    return uploadIds.value;
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
  const uploadId = asString(payload.upload_id);
  if (!uploadId) {
    throw new Error(t('pages.imageProcessing.uploadIdMissing'));
  }
  return uploadId;
}

async function buildOutputPreviews(data: Record<string, unknown>): Promise<void> {
  const assets = collectRecords(data.assets);
  outputPreviews.value = assets.map((asset, index) => ({
    assetId: asString(asset.asset_id) || `asset-${index + 1}`,
    path: asString(asset.path),
    contentType: asString(asset.content_type),
    objectUrl: '',
  }));

  await Promise.all(
    outputPreviews.value.map(async (preview, index) => {
      if (!isBrowserRenderable(preview.contentType)) {
        return;
      }
      const asset = assets[index];
      const url = asString(asset.url);
      if (!url) {
        return;
      }
      const response = await fetch(resolveAssetUrl(url), {
        headers: {
          Authorization: `Bearer ${authSessionState.sessionToken}`,
        },
      });
      if (!response.ok) {
        return;
      }
      const blob = await response.blob();
      preview.objectUrl = URL.createObjectURL(blob);
    }),
  );
}

function clearSelectedImages(): void {
  selectedImages.value.forEach((image) => {
    if (image.previewUrl) {
      URL.revokeObjectURL(image.previewUrl);
    }
  });
  selectedImages.value = [];
}

function clearOutputPreviews(): void {
  outputPreviews.value.forEach((preview) => {
    if (preview.objectUrl) {
      URL.revokeObjectURL(preview.objectUrl);
    }
  });
  outputPreviews.value = [];
}

function openActiveImageViewer(): void {
  const image = selectedImages.value[activeImageIndex.value];
  if (!image?.previewUrl) {
    return;
  }
  openImageViewer({
    src: image.previewUrl,
    title: image.file.name,
    subtitle: image.uploadId || t('pages.fileConversion.preview'),
  });
}

function openOutputImageViewer(preview: OutputPreview): void {
  if (!preview.objectUrl) {
    return;
  }
  openImageViewer({
    src: preview.objectUrl,
    title: preview.assetId,
    subtitle: preview.contentType || preview.path,
  });
}

function buildUploadUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'https' : 'http';
  return `${protocol}://${resolveRuntimeHost()}:17082/api/uploads/files`;
}

function inferDocumentType(file: File): DocumentType {
  const name = file.name.toLowerCase();
  if (name.endsWith('.ofd')) {
    return 'ofd';
  }
  if (name.endsWith('.tif') || name.endsWith('.tiff') || file.type.toLowerCase().includes('tiff')) {
    return 'tiff';
  }
  return 'pdf';
}

function resolveAssetUrl(path: string): string {
  if (/^https?:\/\//i.test(path)) {
    return path;
  }
  const protocol = window.location.protocol === 'https:' ? 'https:' : 'http:';
  return `${protocol}//${window.location.hostname || '127.0.0.1'}:17082${path.startsWith('/') ? path : `/${path}`}`;
}

function isBrowserRenderable(contentType: string): boolean {
  return ['image/jpeg', 'image/jpg', 'image/png', 'image/webp', 'image/gif', 'image/bmp'].includes(contentType.toLowerCase());
}

function collectRecords(value: unknown): Array<Record<string, unknown>> {
  return Array.isArray(value) ? value.map(asRecord).filter((item) => Object.keys(item).length > 0) : [];
}

function collectStrings(value: unknown): string[] {
  return Array.isArray(value) ? value.map(asString).filter(Boolean) : [];
}

function asRecord(value: unknown): Record<string, unknown> {
  return value && typeof value === 'object' && !Array.isArray(value) ? (value as Record<string, unknown>) : {};
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function defaultOutputPath(format: string): string {
  return `/tmp/czur-sdk-file-convert-${Date.now().toString(36)}.${format === 'jpg' ? 'jpg' : format}`;
}

function defaultOutputDir(): string {
  return `/tmp/czur-sdk-file-convert-${Date.now().toString(36)}`;
}

onBeforeUnmount(() => {
  clearSelectedImages();
  clearOutputPreviews();
});
</script>

<style scoped>
.preview-card,
.result-summary {
  border: 1px solid rgb(226 232 240);
  border-radius: 1rem;
  background: rgba(255, 255, 255, 0.86);
  box-shadow: 0 10px 24px rgba(15, 23, 42, 0.06);
  overflow: hidden;
}

.preview-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 0.75rem;
  border-bottom: 1px solid rgb(226 232 240);
  padding: 0.75rem 1rem;
  font-size: 0.875rem;
  font-weight: 700;
  color: rgb(15 23 42);
}

.preview-surface {
  display: flex;
  min-height: 24rem;
  align-items: center;
  justify-content: center;
  background: rgb(248 250 252);
}

.field-label {
  display: grid;
  gap: 0.45rem;
  font-size: 0.78rem;
  font-weight: 700;
  color: rgb(71 85 105);
}

.field-input {
  width: 100%;
  border-radius: 0.75rem;
  border: 1px solid rgb(203 213 225);
  background: white;
  padding: 0.65rem 0.8rem;
  font-size: 0.875rem;
  color: rgb(15 23 42);
  outline: none;
}

.field-input:focus {
  border-color: rgb(14 116 144);
  box-shadow: 0 0 0 3px rgba(14, 116, 144, 0.12);
}

.primary-button,
.secondary-button {
  display: inline-flex;
  min-height: 2.5rem;
  align-items: center;
  justify-content: center;
  border-radius: 0.75rem;
  padding: 0.65rem 1rem;
  font-size: 0.82rem;
  font-weight: 800;
  transition: all 0.2s ease;
}

.primary-button {
  background: rgb(14 116 144);
  color: white;
}

.secondary-button {
  border: 1px solid rgb(203 213 225);
  background: white;
  color: rgb(15 23 42);
}

.primary-button:disabled,
.secondary-button:disabled {
  cursor: not-allowed;
  opacity: 0.5;
}

.thumbnail-button {
  display: flex;
  min-width: 0;
  align-items: center;
  gap: 0.65rem;
  border-radius: 0.75rem;
  border: 1px solid rgb(226 232 240);
  background: white;
  padding: 0.55rem;
  font-size: 0.78rem;
  font-weight: 700;
  color: rgb(51 65 85);
}

.thumbnail-button-active {
  border-color: rgb(14 116 144);
  background: rgb(236 254 255);
  color: rgb(21 94 117);
}
</style>
