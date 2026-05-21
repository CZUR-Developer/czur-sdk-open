<template>
  <div class="space-y-6">
    <SectionPanel :title="t('pages.imageEnhancement.title')" :description="t('pages.imageEnhancement.subtitle')">
      <div class="grid gap-5 xl:grid-cols-[minmax(0,1.1fr)_minmax(340px,0.9fr)]">
        <div class="space-y-4">
          <div class="rounded-2xl border border-slate-200 bg-white/80 p-4 shadow-sm">
            <div class="flex flex-wrap items-center justify-between gap-3">
              <div>
                <p class="text-sm font-semibold text-slate-900">{{ t('pages.imageEnhancement.inputImages') }}</p>
                <p class="mt-1 text-xs text-slate-500">{{ selectedSummary }}</p>
              </div>
              <label class="primary-button justify-center">
                {{ t('pages.imageEnhancement.chooseImages') }}
                <input type="file" accept="image/*" multiple class="sr-only" @change="handleFileChange" />
              </label>
            </div>
          </div>

          <div class="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
            <article v-for="(image, index) in selectedImages" :key="image.previewUrl" class="rounded-xl border border-slate-200 bg-white p-3">
              <img
                :src="image.previewUrl"
                alt=""
                class="h-40 w-full cursor-zoom-in rounded-lg bg-slate-100 object-contain"
                @click="openSelectedImageViewer(image)"
              />
              <p class="mt-2 truncate text-xs font-medium text-slate-700">{{ image.file.name }}</p>
              <p class="mt-1 font-mono text-[11px] text-slate-500">{{ image.uploadId || t('common.notSet') }}</p>
              <button type="button" class="mt-2 text-xs font-semibold text-rose-600" @click="removeImage(index)">
                {{ t('actions.clear') }}
              </button>
            </article>
            <div v-if="selectedImages.length === 0" class="rounded-xl border border-dashed border-slate-300 bg-slate-50 px-4 py-16 text-center text-sm text-slate-500">
              {{ t('pages.imageEnhancement.pickImagesHint') }}
            </div>
          </div>
        </div>

        <div class="space-y-4">
          <KeyValueGrid :items="runtimeItems" />
          <div class="grid grid-cols-2 gap-2">
            <button type="button" class="secondary-button" :disabled="selectedImages.length === 0 || uploadRunning" @click="uploadSelectedImages">
              {{ uploadRunning ? t('pages.imageEnhancement.uploading') : t('pages.imageEnhancement.uploadAll') }}
            </button>
            <button type="button" class="primary-button justify-center" :disabled="!canRun" @click="runEnhance">
              {{ running ? t('pages.imageEnhancement.running') : t('pages.imageEnhancement.runWorkflow') }}
            </button>
          </div>
          <p v-if="lastError" class="rounded-xl border border-rose-200 bg-rose-50 px-3 py-2 text-sm text-rose-600">{{ lastError }}</p>
        </div>
      </div>
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-[0.95fr_1.05fr]">
      <SectionPanel :title="t('pages.imageEnhancement.workflow')" :description="t('pages.imageEnhancement.workflowDescription')">
        <div class="space-y-4">
          <div class="grid gap-3 sm:grid-cols-[1fr_auto_auto]">
            <select v-model="selectedWorkflowId" class="field-input" @change="applySelectedWorkflow">
              <option value="">{{ t('pages.imageEnhancement.newWorkflow') }}</option>
              <option v-for="workflow in workflows" :key="workflow.workflow_id" :value="workflow.workflow_id">
                {{ workflow.name }}
              </option>
            </select>
            <button type="button" class="secondary-button" @click="refreshWorkflows">{{ t('actions.refresh') }}</button>
            <button type="button" class="secondary-button" :disabled="!selectedWorkflowId" @click="deleteWorkflow">{{ t('pages.imageEnhancement.deleteWorkflow') }}</button>
          </div>

          <div class="grid gap-3 sm:grid-cols-[1fr_1fr_auto]">
            <label class="field-label">
              {{ t('pages.imageEnhancement.workflowName') }}
              <input v-model="workflowName" class="field-input" />
            </label>
            <label class="field-label">
              {{ t('pages.imageEnhancement.workflowDescriptionLabel') }}
              <input v-model="workflowDescription" class="field-input" />
            </label>
            <button type="button" class="primary-button self-end justify-center" @click="saveWorkflow">{{ t('actions.save') }}</button>
          </div>

          <div>
            <p class="option-heading">{{ t('pages.imageEnhancement.capabilityLibrary') }}</p>
            <div class="grid gap-2 sm:grid-cols-2">
              <article
                v-for="capability in capabilities"
                :key="capability.type"
                class="capability-button"
                :class="{ 'capability-button-disabled': !capability.available }"
              >
                <div class="flex w-full items-start justify-between gap-3">
                  <div class="min-w-0">
                    <p class="truncate font-semibold">{{ capabilityTitle(capability) }}</p>
                    <p class="mt-1 text-xs leading-5 text-slate-500">{{ capabilityDescription(capability) }}</p>
                  </div>
                  <StatusPill :label="capability.runtime" :tone="capability.available ? (capability.runtime === 'online' ? 'warning' : 'success') : 'danger'" />
                </div>
                <div class="mt-3 flex w-full items-center justify-between gap-3 text-xs">
                  <span class="truncate font-mono text-slate-500">{{ capability.available ? capability.requires_capability : capabilityUnavailableReason(capability) }}</span>
                  <button type="button" class="add-chip" :disabled="!capability.available" @click="addStep(capability)">
                    {{ t('pages.imageEnhancement.addCapability') }}
                  </button>
                </div>
              </article>
            </div>
          </div>

          <div>
            <p class="option-heading">{{ t('pages.imageEnhancement.steps') }}</p>
            <div class="space-y-3">
              <article v-for="(step, index) in pipeline.steps" :key="step.id" class="step-card">
                <div class="flex flex-wrap items-center justify-between gap-2">
                  <div class="flex min-w-0 items-center gap-3">
                    <span class="step-index">{{ index + 1 }}</span>
                    <div class="min-w-0">
                      <p class="truncate text-sm font-semibold text-slate-900">{{ stepTitle(step.type) }}</p>
                      <p class="font-mono text-[11px] text-slate-500">{{ step.id }}</p>
                    </div>
                  </div>
                  <div class="flex gap-1.5">
                    <button type="button" class="icon-button" :aria-label="t('pages.imageEnhancement.moveUp')" :title="t('pages.imageEnhancement.moveUp')" :disabled="index === 0" @click="moveStep(index, -1)">↑</button>
                    <button type="button" class="icon-button" :aria-label="t('pages.imageEnhancement.moveDown')" :title="t('pages.imageEnhancement.moveDown')" :disabled="index === pipeline.steps.length - 1" @click="moveStep(index, 1)">↓</button>
                    <button type="button" class="danger-button" @click="removeStep(index)">{{ t('actions.clear') }}</button>
                  </div>
                </div>
                <div class="mt-3 grid gap-3 sm:grid-cols-3">
                  <label class="toggle-row">
                    <span>{{ t('pages.imageEnhancement.enabled') }}</span>
                    <input v-model="step.enabled" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
                  </label>
                  <label class="field-label">
                    {{ t('pages.imageEnhancement.onError') }}
                    <select v-model="step.on_error" class="field-input">
                      <option value="fail">fail</option>
                      <option value="skip">skip</option>
                    </select>
                  </label>
                  <label class="field-label">
                    {{ t('pages.imageEnhancement.provider') }}
                    <input v-model="step.provider" class="field-input" />
                  </label>
                </div>
                <label class="field-label mt-3">
                  {{ t('pages.imageEnhancement.paramsJson') }}
                  <textarea v-model="step.paramText" class="field-input min-h-24 font-mono text-xs" @blur="syncStepParams(step)" />
                </label>
              </article>
              <p v-if="pipeline.steps.length === 0" class="rounded-xl border border-dashed border-slate-300 bg-slate-50 px-4 py-8 text-center text-sm text-slate-500">
                {{ t('pages.imageEnhancement.noSteps') }}
              </p>
            </div>
          </div>
        </div>
      </SectionPanel>

      <div class="space-y-6">
        <SectionPanel :title="t('common.results')" :description="taskStatusLabel">
          <div class="mb-4">
            <div class="h-2 overflow-hidden rounded-full bg-slate-100">
              <div class="h-full bg-cyan-500 transition-all" :style="{ width: `${taskProgress}%` }" />
            </div>
            <p class="mt-2 text-xs font-medium text-slate-500">{{ taskPhaseLabel }}</p>
          </div>
          <div v-if="taskStepItems.length" class="mb-4 space-y-2">
            <div v-for="step in taskStepItems" :key="step.id" class="flex items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm">
              <div class="min-w-0">
                <p class="truncate font-semibold text-slate-800">{{ step.type }}</p>
                <p class="truncate font-mono text-[11px] text-slate-500">{{ step.id }}</p>
              </div>
              <StatusPill :label="step.status || t('status.idle')" :tone="step.status === 'completed' ? 'success' : step.status === 'failed' ? 'danger' : 'warning'" />
            </div>
          </div>
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
                class="mt-3 h-52 w-full cursor-zoom-in rounded-lg bg-slate-100 object-contain"
                @click="openOutputImageViewer(preview)"
              />
              <p v-else class="mt-3 rounded-lg bg-slate-50 px-3 py-8 text-center text-sm text-slate-500">{{ preview.path }}</p>
            </article>
          </div>
          <div v-else class="rounded-xl border border-dashed border-slate-300 bg-slate-50 px-4 py-12 text-center text-sm text-slate-500">
            {{ t('pages.imageEnhancement.noResults') }}
          </div>
        </SectionPanel>

        <JsonPanel :title="t('pages.imageEnhancement.requestJson')" :caption="t('common.request')" :payload="requestPreview" />
        <JsonPanel :title="t('pages.imageEnhancement.responseJson')" :caption="t('common.response')" :payload="responsePreview" />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import JsonPanel from '../components/blocks/JsonPanel.vue';
import KeyValueGrid from '../components/blocks/KeyValueGrid.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState, sendBoundCommand } from '../services/auth-session';
import { openImageViewer } from '../services/image-viewer';
import {
  createDefaultPipeline,
  deleteEnhanceWorkflow,
  listEnhanceWorkflows,
  loadEnhanceCapabilities,
  saveEnhanceWorkflow,
  type EnhanceCapability,
  type EnhancePipeline,
  type EnhanceWorkflow,
} from '../services/image-enhance-workflows';
import { isOkResponse, resolveRuntimeHost, type CommandResponse } from '../services/protocol';

interface SelectedImage {
  file: File;
  previewUrl: string;
  uploadId: string;
}

interface EditableStep {
  id: string;
  type: string;
  provider: string;
  enabled: boolean;
  on_error: string;
  params: Record<string, unknown>;
  paramText: string;
}

interface EditablePipeline extends Omit<EnhancePipeline, 'steps'> {
  steps: EditableStep[];
}

interface OutputPreview {
  assetId: string;
  path: string;
  contentType: string;
  objectUrl: string;
}

const { t, locale } = useI18n();

const selectedImages = ref<SelectedImage[]>([]);
const uploadRunning = ref(false);
const running = ref(false);
const lastError = ref('');
const lastResponse = ref<CommandResponse<Record<string, unknown>> | null>(null);
const lastRequest = ref<Record<string, unknown> | null>(null);
const outputPreviews = ref<OutputPreview[]>([]);
const providers = ref<Array<{ capabilities?: EnhanceCapability[] }>>([]);
const workflows = ref<EnhanceWorkflow[]>([]);
const selectedWorkflowId = ref('');
const workflowName = ref('New enhancement workflow');
const workflowDescription = ref('');
const currentTask = ref<Record<string, unknown> | null>(null);

const pipeline = ref<EditablePipeline>({
  ...createDefaultPipeline(),
  steps: [],
});

const capabilities = computed(() =>
  providers.value.flatMap((provider) => provider.capabilities ?? []).sort((a, b) => (a.order_hint ?? 0) - (b.order_hint ?? 0)),
);
const capabilityByType = computed(() => new Map(capabilities.value.map((capability) => [capability.type, capability])));
const selectedSummary = computed(() => {
  if (selectedImages.value.length === 0) {
    return t('pages.imageEnhancement.noImagesSelected');
  }
  return selectedImages.value.length === 1
    ? selectedImages.value[0].file.name
    : t('pages.imageEnhancement.selectedImageCount', { count: selectedImages.value.length });
});
const uploadIds = computed(() => selectedImages.value.map((image) => image.uploadId).filter(Boolean));
const canRun = computed(() => selectedImages.value.length > 0 && pipeline.value.steps.length > 0 && !uploadRunning.value && !running.value);
const runtimeItems = computed(() => [
  { label: t('pages.imageProcessing.commandState'), value: t(`status.${authSessionState.commandState}`) },
  { label: t('pages.imageProcessing.session'), value: authSessionState.sessionToken ? t('pages.imageProcessing.bound') : t('pages.imageProcessing.notBound') },
  { label: t('pages.imageEnhancement.images'), value: selectedImages.value.length ? String(selectedImages.value.length) : t('common.notSet') },
  { label: t('pages.imageEnhancement.steps'), value: String(pipeline.value.steps.length) },
  { label: t('pages.imageEnhancement.uploads'), value: uploadIds.value.length ? `${uploadIds.value.length}/${selectedImages.value.length}` : t('common.notSet') },
]);
const requestPayload = computed(() => ({
  source: {
    type: uploadIds.value.length > 1 ? 'images' : 'image',
    input_upload_ids: uploadIds.value.length ? uploadIds.value : selectedImages.value.map((_, index) => `<upload_id_${index + 1}>`),
  },
  pipeline: exportPipeline(),
}));
const requestPreview = computed(() => JSON.stringify({ method: 'image.enhance', params: requestPayload.value }, null, 2));
const responsePreview = computed(() => JSON.stringify(lastResponse.value ?? { data: null }, null, 2));
const taskProgress = computed(() => asNumber(currentTask.value?.progress));
const taskPhaseLabel = computed(() => asString(currentTask.value?.phase) || t('status.idle'));
const taskStatusLabel = computed(() => asString(currentTask.value?.status) || t('status.idle'));
const taskStepItems = computed(() =>
  collectRecords(currentTask.value?.steps).map((step, index) => ({
    id: asString(step.id) || `step-${index + 1}`,
    type: asString(step.type) || `step-${index + 1}`,
    status: asString(step.status),
  })),
);

function localizedCapability(capability: EnhanceCapability): { title?: string; description?: string; unavailable_reason?: string } {
  const localized = capability.localized ?? {};
  const currentKey = locale.value.toLowerCase().startsWith('zh') ? 'zh-CN' : locale.value;
  return localized[currentKey] ?? localized.en ?? {};
}

function capabilityTitle(capability: EnhanceCapability): string {
  return localizedCapability(capability).title || capability.title || capability.type;
}

function capabilityDescription(capability: EnhanceCapability): string {
  return localizedCapability(capability).description || capability.description || capability.type;
}

function capabilityUnavailableReason(capability: EnhanceCapability): string {
  return localizedCapability(capability).unavailable_reason || capability.unavailable_reason || t('status.blocked');
}

function stepTitle(type: string): string {
  const capability = capabilityByType.value.get(type);
  return capability ? capabilityTitle(capability) : type;
}

onMounted(() => {
  void refreshCapabilities();
  void refreshWorkflows();
});

onBeforeUnmount(() => {
  selectedImages.value.forEach((image) => URL.revokeObjectURL(image.previewUrl));
  clearOutputPreviews();
});

async function refreshCapabilities(): Promise<void> {
  try {
    providers.value = await loadEnhanceCapabilities();
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : 'failed';
  }
}

async function refreshWorkflows(): Promise<void> {
  try {
    workflows.value = await listEnhanceWorkflows();
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : 'failed';
  }
}

function handleFileChange(event: Event): void {
  const input = event.target as HTMLInputElement;
  selectedImages.value.forEach((image) => URL.revokeObjectURL(image.previewUrl));
  selectedImages.value = Array.from(input.files ?? []).map((file) => ({
    file,
    previewUrl: URL.createObjectURL(file),
    uploadId: '',
  }));
  input.value = '';
  clearOutputPreviews();
}

function removeImage(index: number): void {
  const [image] = selectedImages.value.splice(index, 1);
  if (image?.previewUrl) {
    URL.revokeObjectURL(image.previewUrl);
  }
}

async function uploadSelectedImages(): Promise<string[]> {
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
    headers: { Authorization: `Bearer ${authSessionState.sessionToken}` },
    body,
  });
  const payload = await response.json() as Record<string, unknown>;
  if (!response.ok) {
    throw new Error(asString(payload.message) || `${response.status} ${response.statusText}`.trim());
  }
  const uploadId = asString(payload.upload_id);
  if (!uploadId) {
    throw new Error(t('pages.imageProcessing.uploadIdMissing'));
  }
  return uploadId;
}

function addStep(capability: EnhanceCapability): void {
  const params = capability.defaults ?? {};
  pipeline.value.steps.push({
    id: `${capability.type}-${Date.now().toString(36)}`,
    type: capability.type,
    provider: 'auto',
    enabled: true,
    on_error: 'fail',
    params,
    paramText: JSON.stringify(params, null, 2),
  });
}

function moveStep(index: number, direction: number): void {
  const target = index + direction;
  if (target < 0 || target >= pipeline.value.steps.length) {
    return;
  }
  const steps = [...pipeline.value.steps];
  const [step] = steps.splice(index, 1);
  steps.splice(target, 0, step);
  pipeline.value.steps = steps;
}

function removeStep(index: number): void {
  pipeline.value.steps.splice(index, 1);
}

function syncStepParams(step: EditableStep): void {
  try {
    const parsed = JSON.parse(step.paramText) as Record<string, unknown>;
    step.params = parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed : {};
    step.paramText = JSON.stringify(step.params, null, 2);
  } catch {
    step.paramText = JSON.stringify(step.params, null, 2);
  }
}

async function saveWorkflow(): Promise<void> {
  try {
    const saved = await saveEnhanceWorkflow({
      workflow_id: selectedWorkflowId.value,
      name: workflowName.value,
      description: workflowDescription.value,
      pipeline: exportPipeline(),
    });
    selectedWorkflowId.value = saved.workflow_id;
    await refreshWorkflows();
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : 'failed';
  }
}

async function deleteWorkflow(): Promise<void> {
  if (!selectedWorkflowId.value) {
    return;
  }
  try {
    await deleteEnhanceWorkflow(selectedWorkflowId.value);
    selectedWorkflowId.value = '';
    workflowName.value = 'New enhancement workflow';
    workflowDescription.value = '';
    pipeline.value = { ...createDefaultPipeline(), steps: [] };
    await refreshWorkflows();
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : 'failed';
  }
}

function applySelectedWorkflow(): void {
  const workflow = workflows.value.find((item) => item.workflow_id === selectedWorkflowId.value);
  if (!workflow) {
    return;
  }
  workflowName.value = workflow.name;
  workflowDescription.value = workflow.description;
  pipeline.value = importPipeline(workflow.pipeline);
}

async function runEnhance(): Promise<void> {
  running.value = true;
  lastError.value = '';
  clearOutputPreviews();
  try {
    await uploadSelectedImages();
    lastRequest.value = { method: 'image.enhance', params: requestPayload.value };
    const response = await sendBoundCommand('image.enhance', { params: requestPayload.value });
    lastResponse.value = response;
    if (!isOkResponse(response)) {
      throw new Error(response.message);
    }
    const taskId = asString(response.data.task_id);
    currentTask.value = asRecord(response.data.task);
    if (taskId) {
      await pollTask(taskId);
    }
  } catch (error) {
    lastError.value = error instanceof Error ? error.message : 'failed';
  } finally {
    running.value = false;
  }
}

async function pollTask(taskId: string): Promise<void> {
  for (let attempt = 0; attempt < 80; attempt += 1) {
    await delay(700);
    const response = await sendBoundCommand('image.enhance_get', { params: { task_id: taskId } });
    lastResponse.value = response;
    if (!isOkResponse(response)) {
      throw new Error(response.message);
    }
    const task = asRecord(response.data.task);
    currentTask.value = task;
    const status = asString(task.status);
    if (status === 'completed') {
      await buildOutputPreviews(task);
      return;
    }
    if (status === 'failed' || status === 'cancelled') {
      throw new Error(asString(task.error) || status);
    }
  }
}

async function buildOutputPreviews(task: Record<string, unknown>): Promise<void> {
  const assets = collectRecords(task.assets);
  outputPreviews.value = assets.map((asset, index) => ({
    assetId: asString(asset.asset_id) || `asset-${index + 1}`,
    path: asString(asset.path),
    contentType: asString(asset.content_type),
    objectUrl: '',
  }));
  await Promise.all(outputPreviews.value.map(async (preview, index) => {
    const asset = assets[index];
    const url = asString(asset.url);
    if (!url || !preview.contentType.toLowerCase().startsWith('image/')) {
      return;
    }
    const response = await fetch(resolveAssetUrl(url), {
      headers: { Authorization: `Bearer ${authSessionState.sessionToken}` },
    });
    if (!response.ok) {
      return;
    }
    preview.objectUrl = URL.createObjectURL(await response.blob());
  }));
}

function clearOutputPreviews(): void {
  outputPreviews.value.forEach((preview) => {
    if (preview.objectUrl) URL.revokeObjectURL(preview.objectUrl);
  });
  outputPreviews.value = [];
}

function openSelectedImageViewer(image: SelectedImage): void {
  openImageViewer({
    src: image.previewUrl,
    title: image.file.name,
    subtitle: image.uploadId || t('pages.imageEnhancement.inputImages'),
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

function exportPipeline(): EnhancePipeline {
  return {
    ...pipeline.value,
    steps: pipeline.value.steps.map((step) => ({
      id: step.id,
      type: step.type,
      provider: step.provider || 'auto',
      enabled: step.enabled,
      on_error: step.on_error || 'fail',
      params: step.params,
    })),
    target: {
      type: 'images',
      format: 'jpg',
      export_type: 'single-page',
    },
  };
}

function importPipeline(value: EnhancePipeline): EditablePipeline {
  return {
    ...createDefaultPipeline(),
    ...value,
    target: { type: 'images', format: 'jpg', export_type: 'single-page' },
    steps: (value.steps ?? []).map((step) => ({
      ...step,
      provider: step.provider || 'auto',
      enabled: step.enabled !== false,
      on_error: step.on_error || 'fail',
      params: step.params ?? {},
      paramText: JSON.stringify(step.params ?? {}, null, 2),
    })),
  };
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

function collectRecords(value: unknown): Array<Record<string, unknown>> {
  return Array.isArray(value) ? value.map(asRecord).filter((item) => Object.keys(item).length > 0) : [];
}

function asRecord(value: unknown): Record<string, unknown> {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : {};
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function asNumber(value: unknown): number {
  return typeof value === 'number' ? value : 0;
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}
</script>

<style scoped>
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

.primary-button,
.secondary-button {
  display: inline-flex;
  min-height: 2.5rem;
  cursor: pointer;
  align-items: center;
  justify-content: center;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.75rem;
  padding: 0.5rem 0.75rem;
  font-size: 0.875rem;
  font-weight: 700;
  transition:
    background-color 160ms ease,
    border-color 160ms ease,
    color 160ms ease,
    opacity 160ms ease;
}

.primary-button {
  border-color: rgb(8 145 178);
  background: rgb(8 145 178);
  color: rgb(255 255 255);
}

.secondary-button {
  background: rgb(255 255 255);
  color: rgb(51 65 85);
}

.secondary-button:hover:not(:disabled) {
  border-color: rgb(103 232 249);
  color: rgb(14 116 144);
}

.primary-button:disabled,
.secondary-button:disabled {
  cursor: not-allowed;
  opacity: 0.5;
}

.capability-button {
  display: flex;
  min-height: 8rem;
  flex-direction: column;
  align-items: flex-start;
  justify-content: space-between;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.875rem;
  background: rgb(255 255 255);
  padding: 0.75rem;
  color: rgb(51 65 85);
  text-align: left;
  font-size: 0.875rem;
  transition:
    background-color 160ms ease,
    border-color 160ms ease,
    box-shadow 160ms ease,
    transform 160ms ease;
}

.capability-button:hover:not(.capability-button-disabled) {
  border-color: rgb(103 232 249);
  background: rgb(236 254 255 / 0.5);
  box-shadow: 0 8px 20px rgb(15 23 42 / 0.08);
  transform: translateY(-1px);
}

.capability-button-disabled {
  background: rgb(248 250 252);
  opacity: 0.72;
}

.add-chip {
  cursor: pointer;
  flex: none;
  border: 1px solid rgb(8 145 178);
  border-radius: 9999px;
  background: rgb(8 145 178);
  padding: 0.25rem 0.6rem;
  color: white;
  font-weight: 800;
}

.add-chip:hover:not(:disabled) {
  border-color: rgb(14 116 144);
  background: rgb(14 116 144);
}

.add-chip:disabled {
  cursor: not-allowed;
  border-color: rgb(203 213 225);
  background: rgb(203 213 225);
  color: rgb(71 85 105);
}

.step-card {
  border: 1px solid rgb(226 232 240);
  border-radius: 0.875rem;
  background: rgb(255 255 255);
  padding: 0.875rem;
  box-shadow: 0 1px 2px rgb(15 23 42 / 0.04);
}

.step-index {
  display: inline-flex;
  height: 2rem;
  width: 2rem;
  flex: none;
  align-items: center;
  justify-content: center;
  border-radius: 9999px;
  background: rgb(236 254 255);
  color: rgb(14 116 144);
  font-size: 0.75rem;
  font-weight: 800;
}

.icon-button,
.danger-button {
  display: inline-flex;
  min-height: 2rem;
  align-items: center;
  justify-content: center;
  border-radius: 0.625rem;
  border: 1px solid rgb(226 232 240);
  background: white;
  padding: 0.35rem 0.65rem;
  font-size: 0.75rem;
  font-weight: 800;
  transition:
    border-color 160ms ease,
    color 160ms ease,
    background-color 160ms ease,
    opacity 160ms ease;
}

.icon-button {
  width: 2rem;
  color: rgb(51 65 85);
}

.icon-button:hover:not(:disabled) {
  border-color: rgb(103 232 249);
  color: rgb(14 116 144);
  background: rgb(236 254 255 / 0.45);
}

.danger-button {
  color: rgb(190 18 60);
}

.danger-button:hover:not(:disabled) {
  border-color: rgb(253 164 175);
  background: rgb(255 241 242);
}

.icon-button:disabled,
.danger-button:disabled {
  cursor: not-allowed;
  opacity: 0.45;
}

.toggle-row {
  display: flex;
  min-height: 2.5rem;
  align-items: center;
  justify-content: space-between;
  gap: 0.75rem;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.75rem;
  background: rgb(248 250 252);
  padding: 0.5rem 0.75rem;
  color: rgb(71 85 105);
  font-size: 0.75rem;
  font-weight: 800;
  letter-spacing: 0.04em;
  text-transform: uppercase;
}
</style>
