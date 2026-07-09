<template>
  <div class="space-y-6">
    <p v-if="statusLoaded && !available" class="rounded-md border border-amber-200 bg-amber-50 px-3 py-2 text-sm text-amber-900">
      {{ statusReason || t('pages.twainScanning.windowsOnly') }}
    </p>

    <div class="grid gap-6 xl:grid-cols-[0.9fr_1.1fr]">
      <SectionPanel :title="t('sections.twainSources')" :description="t('pages.twainScanning.sourceDescription')">
        <div class="space-y-4">
          <div class="grid gap-3 md:grid-cols-3">
            <div class="rounded-md border border-slate-200 bg-slate-50 p-3 text-sm">
              <p class="text-xs font-semibold uppercase text-slate-500">{{ t('labels.status') }}</p>
              <StatusPill class="mt-2" :label="available ? 'available' : 'unavailable'" :tone="available ? 'success' : 'warning'" />
            </div>
            <div class="rounded-md border border-slate-200 bg-slate-50 p-3 text-sm">
              <p class="text-xs font-semibold uppercase text-slate-500">DSM</p>
              <p class="mt-2 font-semibold text-slate-900">{{ dsmLoaded ? 'loaded' : 'not loaded' }}</p>
            </div>
            <div class="rounded-md border border-slate-200 bg-slate-50 p-3 text-sm">
              <p class="text-xs font-semibold uppercase text-slate-500">TWAIN</p>
              <p class="mt-2 truncate font-semibold text-slate-900">{{ twainVersion || '-' }}</p>
            </div>
          </div>

          <div class="flex flex-wrap gap-2">
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning" @click="runStatus">
              {{ t('actions.refresh') }}
            </button>
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning" @click="refreshSources">
              {{ t('pages.twainScanning.refreshSources') }}
            </button>
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || watchEnabled" @click="startWatch">
              {{ t('pages.twainScanning.watchSources') }}
            </button>
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || !watchEnabled" @click="stopWatch">
              {{ t('pages.twainScanning.stopWatchSources') }}
            </button>
          </div>

          <div v-if="sources.length === 0" class="rounded-lg border border-dashed border-slate-300 p-5 text-sm text-slate-500">
            {{ t('pages.twainScanning.noSources') }}
          </div>
          <div v-else class="overflow-hidden rounded-lg border border-slate-200">
            <table class="min-w-full divide-y divide-slate-200 text-sm">
              <thead class="bg-slate-50 text-left text-xs uppercase text-slate-500">
                <tr>
                  <th class="px-3 py-2">{{ t('pages.twainScanning.source') }}</th>
                  <th class="px-3 py-2">{{ t('labels.model') }}</th>
                  <th class="px-3 py-2">{{ t('labels.status') }}</th>
                </tr>
              </thead>
              <tbody class="divide-y divide-slate-100 bg-white">
                <tr
                  v-for="source in sources"
                  :key="source.source_id"
                  class="cursor-pointer hover:bg-cyan-50"
                  :class="[selectedSourceId === source.source_id ? 'bg-cyan-50' : '', source.openable === false ? 'text-slate-500' : '']"
                  @click="selectedSourceId = source.source_id"
                >
                  <td class="px-3 py-2">
                    <p class="font-medium" :class="source.openable === false ? 'text-slate-600' : 'text-slate-900'">{{ source.source_name || source.source_id }}</p>
                    <p class="font-mono text-xs text-slate-500">{{ source.source_id }}</p>
                  </td>
                  <td class="px-3 py-2 text-slate-700">
                    <p>{{ source.manufacturer }} {{ source.product_name }}</p>
                    <p v-if="source.product_family" class="mt-1 text-xs text-slate-500">{{ source.product_family }}</p>
                  </td>
                  <td class="px-3 py-2">
                    <StatusPill :label="source.status || 'unknown'" :tone="source.status === 'online' ? 'success' : 'neutral'" />
                  </td>
                </tr>
              </tbody>
            </table>
          </div>

          <div class="grid gap-3 md:grid-cols-2">
            <label class="space-y-1 text-sm">
              <span class="font-medium text-slate-700">{{ t('pages.twainScanning.selectedSource') }}</span>
              <select v-model="selectedSourceId" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm">
                <option value="">{{ t('common.notSet') }}</option>
                <option v-for="source in sources" :key="source.source_id" :value="source.source_id">
                  {{ source.source_name || source.source_id }}
                </option>
              </select>
            </label>
            <label class="space-y-1 text-sm">
              <span class="font-medium text-slate-700">{{ t('pages.twainScanning.sessionId') }}</span>
              <input v-model="sessionId" class="w-full rounded-md border border-slate-300 px-3 py-2 font-mono text-xs" readonly>
            </label>
          </div>

          <div class="flex flex-wrap gap-2">
            <button class="rounded-md bg-cyan-700 px-3 py-2 text-sm font-semibold text-white disabled:opacity-50" :disabled="isRunning || scanBusy || !selectedSourceId || !selectedSourceOpenable || Boolean(sessionId)" @click="openSource">
              {{ t('pages.twainScanning.openSource') }}
            </button>
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || !sessionId" @click="closeSource">
              {{ t('pages.twainScanning.closeSource') }}
            </button>
          </div>
        </div>
      </SectionPanel>

      <SectionPanel :title="t('sections.twainCapabilities')" :description="t('pages.twainScanning.capabilitiesDescription')">
        <div class="space-y-4">
          <div class="flex flex-wrap gap-2">
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || !sessionId" @click="loadCapabilities">
              {{ t('pages.twainScanning.loadCapabilities') }}
            </button>
          </div>

          <div class="max-h-[560px] overflow-auto rounded-lg border border-slate-200 bg-white">
            <div v-if="capabilities.length === 0" class="p-5 text-sm text-slate-500">
              {{ t('pages.twainScanning.noCapabilities') }}
            </div>
            <div v-for="capability in capabilities" :key="capabilityKey(capability)" class="grid gap-3 border-b border-slate-100 p-4 last:border-b-0 md:grid-cols-[220px_1fr]">
              <div>
                <div class="flex items-center gap-2">
                  <p class="font-medium text-slate-800">{{ capability.title || capability.name || capability.cap }}</p>
                  <StatusPill v-if="capability.settable === false || capability.readonly" :label="t('pages.twainScanning.readonly')" tone="neutral" />
                </div>
                <p class="mt-1 font-mono text-xs text-slate-500">{{ capability.cap || capability.name }}</p>
              </div>
              <div class="space-y-2">
                <select
                  v-if="capabilityChoices(capability).length > 0"
                  v-model="capabilityDrafts[capabilityKey(capability)]"
                  class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm disabled:bg-slate-100"
                  :disabled="capabilityDisabled(capability)"
                  @change="applyCapability(capability)"
                >
                  <option v-for="choice in capabilityChoices(capability)" :key="formatValue(choice)" :value="choice">
                    {{ formatValue(choice) }}
                  </option>
                </select>
                <div v-else-if="capability.constraint?.type === 'range'" class="grid gap-2 md:grid-cols-[1fr_110px]">
                  <input
                    v-model.number="capabilityDrafts[capabilityKey(capability)]"
                    type="range"
                    class="w-full"
                    :min="capability.constraint?.min"
                    :max="capability.constraint?.max"
                    :step="rangeStep(capability)"
                    :disabled="capabilityDisabled(capability)"
                    @change="applyCapability(capability)"
                  >
                  <input
                    v-model.number="capabilityDrafts[capabilityKey(capability)]"
                    type="number"
                    class="rounded-md border border-slate-300 px-3 py-2 text-sm disabled:bg-slate-100"
                    :min="capability.constraint?.min"
                    :max="capability.constraint?.max"
                    :step="rangeStep(capability)"
                    :disabled="capabilityDisabled(capability)"
                    @change="applyCapability(capability)"
                  >
                </div>
                <textarea
                  v-else-if="capability.constraint?.type === 'array'"
                  class="min-h-24 w-full resize-y rounded-md border border-slate-300 px-3 py-2 font-mono text-xs disabled:bg-slate-100"
                  :value="formatValue(capability.value)"
                  disabled
                />
                <input
                  v-else
                  v-model="capabilityDrafts[capabilityKey(capability)]"
                  class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm disabled:bg-slate-100"
                  :disabled="capabilityDisabled(capability)"
                  @change="applyCapability(capability)"
                >
                <p class="text-xs text-slate-500">
                  {{ t('labels.type') }}: {{ capability.type || '-' }}
                  <span v-if="capability.constraint?.type"> / {{ capability.constraint.type }}</span>
                </p>
              </div>
            </div>
          </div>

          <div class="grid gap-3 md:grid-cols-[1fr_auto_auto]">
            <input v-model="profileName" class="rounded-md border border-slate-300 px-3 py-2 text-sm" :placeholder="t('pages.twainScanning.profileName')">
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning" @click="loadProfiles">
              {{ t('pages.twainScanning.loadProfiles') }}
            </button>
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || !profileName" @click="saveProfile">
              {{ t('pages.twainScanning.saveProfile') }}
            </button>
          </div>
          <div class="grid gap-3 md:grid-cols-[1fr_auto_auto]">
            <select v-model="selectedProfileId" class="rounded-md border border-slate-300 px-3 py-2 text-sm">
              <option value="">{{ t('pages.twainScanning.noProfileSelected') }}</option>
              <option v-for="profile in profiles" :key="profile.profile_id" :value="profile.profile_id">
                {{ profile.name || profile.profile_id }}
              </option>
            </select>
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || !selectedProfileId" @click="applyProfile">
              {{ t('pages.twainScanning.applyProfile') }}
            </button>
            <button class="rounded-md border border-rose-300 px-3 py-2 text-sm font-semibold text-rose-700 disabled:opacity-50" :disabled="isRunning || !selectedProfileId" @click="deleteProfile">
              {{ t('actions.clear') }}
            </button>
          </div>
        </div>
      </SectionPanel>
    </div>

    <div class="grid gap-6 xl:grid-cols-[0.9fr_1.1fr]">
      <SectionPanel :title="t('pages.twainScanning.scanTitle')" :description="t('pages.twainScanning.scanDescription')">
        <div class="grid gap-3 md:grid-cols-2">
          <label class="space-y-1 text-sm">
            <span class="font-medium text-slate-700">{{ t('pages.twainScanning.outputType') }}</span>
            <select v-model="outputType" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm" :disabled="scanBusy">
              <option value="images">images</option>
              <option value="pdf">pdf</option>
              <option value="ofd">ofd</option>
              <option value="tiff">tiff</option>
            </select>
          </label>
          <label class="space-y-1 text-sm">
            <span class="font-medium text-slate-700">{{ t('pages.twainScanning.exportType') }}</span>
            <select v-model="exportType" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm" :disabled="scanBusy || outputType === 'images'">
              <option value="multi-page">{{ t('pages.twainScanning.multiPage') }}</option>
              <option value="single-page">{{ t('pages.twainScanning.singlePage') }}</option>
            </select>
          </label>
          <label class="space-y-1 text-sm">
            <span class="font-medium text-slate-700">{{ t('pages.twainScanning.outputPath') }}</span>
            <input v-model="outputPath" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm" :disabled="scanBusy" :placeholder="t('pages.twainScanning.outputPlaceholder')">
          </label>
          <label class="space-y-1 text-sm text-slate-700">
            <span class="inline-flex items-center gap-2">
              <input v-model="showUi" type="checkbox" class="h-4 w-4 rounded border-slate-300" :disabled="scanBusy || forceShowUi">
              <span>{{ t('pages.twainScanning.showUi') }}</span>
            </span>
            <span class="block text-xs" :class="showUiCapabilityTone">
              {{ showUiCapabilityText }}
            </span>
          </label>
        </div>

        <div class="mt-4 flex flex-wrap gap-2">
          <button class="rounded-md bg-cyan-700 px-3 py-2 text-sm font-semibold text-white disabled:opacity-50" :disabled="isRunning || scanBusy || !sessionId" @click="scan">
            {{ t('pages.twainScanning.startScan') }}
          </button>
          <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || !lastTaskId" @click="loadScanTask">
            {{ t('pages.twainScanning.getScan') }}
          </button>
          <button class="rounded-md border border-rose-300 px-3 py-2 text-sm font-semibold text-rose-700 disabled:opacity-50" :disabled="isRunning || !lastTaskId || !scanBusy" @click="cancelScan">
            {{ t('pages.twainScanning.cancelScan') }}
          </button>
        </div>

        <div v-if="lastTask" class="mt-4 grid gap-3 rounded-lg border border-slate-200 bg-slate-50 p-4 text-sm md:grid-cols-4">
          <div>
            <p class="text-xs text-slate-500">{{ t('labels.status') }}</p>
            <p class="font-semibold text-slate-900">{{ lastTask.status }}</p>
          </div>
          <div>
            <p class="text-xs text-slate-500">{{ t('pages.twainScanning.phase') }}</p>
            <p class="font-semibold text-slate-900">{{ lastTask.phase || '-' }}</p>
          </div>
          <div>
            <p class="text-xs text-slate-500">{{ t('pages.twainScanning.progress') }}</p>
            <p class="font-semibold text-slate-900">{{ lastTask.progress ?? 0 }}%</p>
          </div>
          <div>
            <p class="text-xs text-slate-500">{{ t('labels.pageCount') }}</p>
            <p class="font-semibold text-slate-900">{{ lastTask.page_count ?? 0 }}</p>
          </div>
          <div class="md:col-span-2">
            <p class="text-xs text-slate-500">{{ t('pages.twainScanning.resultPath') }}</p>
            <p class="truncate font-mono text-xs text-slate-800">{{ lastTask.output_path || '-' }}</p>
          </div>
          <div class="md:col-span-2">
            <p class="text-xs text-slate-500">{{ t('pages.twainScanning.message') }}</p>
            <p class="truncate text-sm font-semibold text-slate-900">{{ lastTask.error || lastTask.message || '-' }}</p>
          </div>
        </div>

        <div v-if="lastTask?.output_paths?.length" class="mt-3 rounded-lg border border-slate-200 bg-white p-4 text-sm">
          <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.twainScanning.resultPaths') }}</p>
          <ul class="space-y-1">
            <li v-for="path in lastTask.output_paths" :key="path" class="truncate font-mono text-xs text-slate-800">
              {{ path }}
            </li>
          </ul>
        </div>

        <div v-if="outputPreviews.length" class="mt-3 grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
          <article v-for="preview in outputPreviews" :key="preview.assetId" class="rounded-lg border border-slate-200 bg-white p-3 text-sm">
            <div class="flex items-center justify-between gap-3 text-xs font-semibold text-slate-500">
              <span class="truncate">{{ preview.assetId }}</span>
              <span>{{ preview.contentType }}</span>
            </div>
            <img
              v-if="preview.objectUrl"
              :src="preview.objectUrl"
              alt=""
              class="mt-3 h-44 w-full cursor-zoom-in rounded-md bg-slate-100 object-contain"
              @click="openTwainOutputViewer(preview)"
            >
            <p v-else class="mt-3 rounded-md bg-slate-50 px-3 py-8 text-center text-sm text-slate-500">
              {{ preview.error || t('common.loading') }}
            </p>
          </article>
        </div>
      </SectionPanel>

      <div class="grid gap-6 lg:grid-cols-2">
        <JsonPanel :title="t('pages.twainScanning.requestJson')" :caption="lastMethod || t('common.request')" :payload="requestJson" />
        <JsonPanel :title="t('pages.twainScanning.responseJson')" :caption="lastStatusLabel" :payload="responseJson" />
      </div>
      <JsonPanel :title="twainEventTitle" :caption="twainEventCaption" :payload="twainEventJson" />
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import JsonPanel from '../components/blocks/JsonPanel.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState, onCommandEvent, sendBoundCommand } from '../services/auth-session';
import { openImageViewer } from '../services/image-viewer';
import { buildAssetApiUrl, buildCommandRequest, type CommandEvent, type CommandResponse } from '../services/protocol';

interface TwainSource {
  source_id: string;
  source_name?: string;
  manufacturer?: string;
  product_family?: string;
  product_name?: string;
  status?: string;
  openable?: boolean;
}

interface TwainCapability {
  cap: string;
  cap_id?: number;
  name?: string;
  title?: string;
  type?: string;
  value?: unknown;
  constraint?: {
    type?: string;
    min?: number;
    max?: number;
    quant?: number;
    values?: unknown[];
  };
  readonly?: boolean;
  settable?: boolean;
}

interface TwainProfile {
  profile_id: string;
  name?: string;
}

interface TwainCapabilitySetItem {
  cap?: string;
  name?: string;
  value: unknown;
}

interface TwainAsset {
  asset_id?: string;
  kind?: string;
  path?: string;
  url?: string;
  download_url?: string;
  content_type?: string;
}

interface TwainTask {
  task_id?: string;
  status?: string;
  phase?: string;
  progress?: number;
  page_count?: number;
  output_path?: string;
  output_paths?: string[];
  message?: string;
  error?: string;
  assets?: TwainAsset[];
}

interface TwainOutputPreview {
  assetId: string;
  kind: string;
  path: string;
  url: string;
  contentType: string;
  objectUrl: string;
  error: string;
}

const { t, locale } = useI18n();

const isRunning = ref(false);
const statusLoaded = ref(false);
const available = ref(false);
const dsmLoaded = ref(false);
const twainVersion = ref('');
const statusReason = ref('');
const watchEnabled = ref(false);
const sources = ref<TwainSource[]>([]);
const selectedSourceId = ref('');
const sessionId = ref('');
const capabilities = ref<TwainCapability[]>([]);
const capabilityDrafts = ref<Record<string, unknown>>({});
const appliedCapabilityHistory = ref<TwainCapabilitySetItem[]>([]);
const profiles = ref<TwainProfile[]>([]);
const profileName = ref('Default TWAIN scan');
const selectedProfileId = ref('');
const outputType = ref('images');
const exportType = ref('single-page');
const outputPath = ref('');
const showUi = ref(false);
const lastTaskId = ref('');
const lastTask = ref<TwainTask | null>(null);
const lastRequest = ref<Record<string, unknown> | null>(null);
const lastResponse = ref<CommandResponse<Record<string, unknown>> | null>(null);
const lastMethod = ref('');
const outputPreviews = ref<TwainOutputPreview[]>([]);
const twainEventHistory = ref<CommandEvent<Record<string, unknown>>[]>([]);
let commandEventUnsubscribe: (() => void) | null = null;
let outputPreviewGeneration = 0;

const requestJson = computed(() => JSON.stringify(lastRequest.value ?? {}, null, 2));
const responseJson = computed(() => JSON.stringify(lastResponse.value ?? {}, null, 2));
const twainEventJson = computed(() => JSON.stringify(twainEventHistory.value, null, 2));
const twainEventTitle = computed(() => (String(locale.value).startsWith('zh') ? 'TWAIN 事件' : 'TWAIN events'));
const twainEventCaption = computed(() => {
  const unit = String(locale.value).startsWith('zh') ? '条事件' : 'events';
  return `${twainEventHistory.value.length} ${unit}`;
});
const lastStatusLabel = computed(() => (lastResponse.value ? `${lastResponse.value.code} ${lastResponse.value.message}` : t('common.response')));
const scanBusy = computed(() => lastTask.value?.status === 'queued' || lastTask.value?.status === 'running');
const selectedSource = computed(() => sources.value.find((source) => source.source_id === selectedSourceId.value));
const selectedSourceOpenable = computed(() => selectedSource.value?.openable !== false);
const uiControllableCapability = computed(() => capabilityByKey('CAP_UICONTROLLABLE', 'ui_controllable'));
const enableDsUiOnlyCapability = computed(() => capabilityByKey('CAP_ENABLEDSUIONLY', 'enable_ds_ui_only'));
const uiControllableValue = computed(() => booleanCapabilityValue(uiControllableCapability.value));
const enableDsUiOnlyValue = computed(() => booleanCapabilityValue(enableDsUiOnlyCapability.value));
const forceShowUi = computed(() => uiControllableValue.value === false);
const showUiCapabilityTone = computed(() => {
  if (uiControllableValue.value === false) {
    return 'text-amber-700';
  }
  if (uiControllableValue.value === true) {
    return 'text-emerald-700';
  }
  return 'text-slate-500';
});
const showUiCapabilityText = computed(() => {
  if (uiControllableValue.value === false) {
    return t('pages.twainScanning.showUiRequired');
  }
  if (uiControllableValue.value === true && enableDsUiOnlyValue.value === true) {
    return t('pages.twainScanning.showUiControllableWithSettings');
  }
  if (uiControllableValue.value === true) {
    return t('pages.twainScanning.showUiControllable');
  }
  return t('pages.twainScanning.showUiUnknown');
});

onMounted(() => {
  void initializeTwainPage();
});

onUnmounted(() => {
  commandEventUnsubscribe?.();
  commandEventUnsubscribe = null;
  clearOutputPreviews();
  if (watchEnabled.value) {
    void stopWatch().catch(() => undefined);
  }
});

watch(lastTask, (task) => {
  void buildTwainOutputPreviews(task);
});

watch(outputType, (type) => {
  if (type === 'images') {
    exportType.value = 'single-page';
  }
});

async function initializeTwainPage(): Promise<void> {
  await runStatus();
  commandEventUnsubscribe?.();
  commandEventUnsubscribe = onCommandEvent(handleCommandEvent);
  await refreshSources();
  await startWatch();
}

async function runStatus(): Promise<void> {
  const response = await runCommand('twain.status');
  statusLoaded.value = true;
  available.value = response.data.available === true;
  dsmLoaded.value = response.data.dsm_loaded === true;
  twainVersion.value = asString(response.data.twain_version);
  statusReason.value = asString(response.data.reason) || response.message;
}

async function refreshSources(): Promise<void> {
  const response = await runCommand('twain.list', { refresh: true });
  updateSources(Array.isArray(response.data.sources) ? response.data.sources as TwainSource[] : []);
}

async function startWatch(): Promise<void> {
  const response = await runCommand('twain.watch_start');
  watchEnabled.value = response.data.watching === true;
}

async function stopWatch(): Promise<void> {
  const response = await runCommand('twain.watch_stop');
  watchEnabled.value = response.data.watching === true;
}

function handleCommandEvent(event: CommandEvent<Record<string, unknown>>): void {
  if (event.event === 'twain.scan_changed') {
    appendTwainScanEvent(event);
    const task = asTask(event.payload?.task);
    if (task) {
      lastTaskId.value = asString(task.task_id) || lastTaskId.value;
      lastTask.value = task;
    }
    return;
  }
  if (event.event !== 'twain.source_snapshot') {
    return;
  }
  const payload = event.payload ?? {};
  if (Array.isArray(payload.sources)) {
    updateSources(payload.sources as TwainSource[]);
  } else {
    void refreshSources();
  }
}

function updateSources(nextSources: TwainSource[]): void {
  const openableSources = nextSources.filter((source) => source.openable !== false);
  sources.value = nextSources;
  if (selectedSourceId.value && !nextSources.some((source) => source.source_id === selectedSourceId.value)) {
    selectedSourceId.value = '';
    sessionId.value = '';
    capabilities.value = [];
    appliedCapabilityHistory.value = [];
  }
  if (selectedSourceId.value && !openableSources.some((source) => source.source_id === selectedSourceId.value)) {
    sessionId.value = '';
    capabilities.value = [];
    appliedCapabilityHistory.value = [];
  }
  if (!selectedSourceId.value && openableSources.length > 0) {
    selectedSourceId.value = openableSources[0].source_id;
  }
}

async function openSource(): Promise<void> {
  const response = await runCommand('twain.open', { source_id: selectedSourceId.value });
  const openedSessionId = asString(response.data.session_id);
  if (openedSessionId) {
    sessionId.value = openedSessionId;
    appliedCapabilityHistory.value = [];
    await loadCapabilities();
  }
}

async function closeSource(): Promise<void> {
  await runCommand('twain.close', { session_id: sessionId.value });
  sessionId.value = '';
  capabilities.value = [];
  appliedCapabilityHistory.value = [];
  resetScanState();
}

async function loadCapabilities(): Promise<void> {
  const response = await runCommand('twain.get_capabilities', { session_id: sessionId.value });
  capabilities.value = Array.isArray(response.data.capabilities) ? response.data.capabilities as TwainCapability[] : [];
  capabilityDrafts.value = Object.fromEntries(capabilities.value.map((capability) => [capabilityKey(capability), capability.value ?? '']));
  syncShowUiWithCapabilities();
}

async function applyCapability(capability: TwainCapability): Promise<void> {
  if (!sessionId.value || capabilityDisabled(capability)) {
    return;
  }
  const item = capabilitySetItem(capability);
  await runCommand('twain.set_capabilities', {
    session_id: sessionId.value,
    capabilities: [item],
  });
  rememberAppliedCapability(item);
  // TWAIN Source 可能在设置一个 capability 后联动修改其它 capability，设置后需要重新读取当前驱动状态。
  await loadCapabilities();
}

async function loadProfiles(): Promise<void> {
  const response = await runCommand('twain.profile_list', { session_id: sessionId.value, source_id: selectedSourceId.value });
  profiles.value = Array.isArray(response.data.profiles) ? response.data.profiles as TwainProfile[] : [];
}

async function saveProfile(): Promise<void> {
  await runCommand('twain.profile_save', {
    session_id: sessionId.value,
    source_id: selectedSourceId.value,
    name: profileName.value,
    capabilities: currentCapabilityItems(),
  });
  await loadProfiles();
}

async function applyProfile(): Promise<void> {
  await runCommand('twain.profile_apply', {
    session_id: sessionId.value,
    profile_id: selectedProfileId.value,
  });
  if (sessionId.value) {
    await loadCapabilities();
  }
}

async function deleteProfile(): Promise<void> {
  await runCommand('twain.profile_delete', {
    session_id: sessionId.value,
    profile_id: selectedProfileId.value,
  });
  selectedProfileId.value = '';
  await loadProfiles();
}

async function scan(): Promise<void> {
  clearTwainEventHistory();
  const response = await runCommand('twain.scan', {
    session_id: sessionId.value,
    show_ui: showUi.value,
    output: {
      type: outputType.value,
      path: outputPath.value || undefined,
      export_type: outputType.value === 'images' ? 'single-page' : exportType.value,
    },
  });
  applyScanResponse(response);
}

async function loadScanTask(): Promise<void> {
  const response = await runCommand('twain.scan_get', { task_id: lastTaskId.value });
  lastTask.value = asTask(response.data.task);
}

function isPendingScanTask(task: TwainTask): boolean {
  return task.status === 'queued' || task.status === 'running';
}

async function cancelScan(): Promise<void> {
  clearTwainEventHistory();
  const response = await runCommand('twain.scan_cancel', { task_id: lastTaskId.value });
  const task = asTask(response.data.task);
  if (task) {
    lastTask.value = task;
    return;
  }
  if (lastTask.value) {
    lastTask.value = {
      ...lastTask.value,
      cancel_requested: response.data.cancel_requested === true,
      phase: asString(response.data.phase) || lastTask.value.phase,
      message: asString(response.data.message) || lastTask.value.message,
      status: asString(response.data.status) || lastTask.value.status,
    };
  }
}

function applyScanResponse(response: CommandResponse<Record<string, unknown>>): void {
  const accepted = response.data.accepted === true;
  const taskId = asString(response.data.task_id);
  const task = asTask(response.data.task);
  if (accepted && taskId && task) {
    lastTaskId.value = taskId;
    lastTask.value = task;
    return;
  }
  lastTaskId.value = '';
  lastTask.value = task && task.status && !isPendingScanTask(task) ? task : null;
}

function resetScanState(): void {
  lastTaskId.value = '';
  lastTask.value = null;
  clearTwainEventHistory();
  clearOutputPreviews();
}

function appendTwainScanEvent(event: CommandEvent<Record<string, unknown>>): void {
  twainEventHistory.value = [...twainEventHistory.value, event];
}

function clearTwainEventHistory(): void {
  twainEventHistory.value = [];
}

async function runCommand(method: string, params: Record<string, unknown> = {}): Promise<CommandResponse<Record<string, unknown>>> {
  const request = buildCommandRequest(method, { params });
  lastMethod.value = method;
  lastRequest.value = request;
  isRunning.value = true;
  try {
    const response = await sendBoundCommand(method, { params });
    lastResponse.value = response;
    return response;
  } finally {
    isRunning.value = false;
  }
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function asTask(value: unknown): TwainTask | null {
  return value && typeof value === 'object' ? value as TwainTask : null;
}

function capabilityKey(capability: TwainCapability): string {
  return capability.cap || capability.name || String(capability.cap_id ?? '');
}

function capabilityByKey(cap: string, name: string): TwainCapability | undefined {
  return capabilities.value.find((capability) => capability.cap === cap || capability.name === name);
}

function capabilityDisabled(capability: TwainCapability): boolean {
  return isRunning.value || scanBusy.value || !sessionId.value || capability.settable === false || capability.readonly === true;
}

function capabilityChoices(capability: TwainCapability): unknown[] {
  if (capability.constraint?.type !== 'list') {
    return [];
  }
  const values = capability.constraint?.values;
  return Array.isArray(values) ? values : [];
}

function rangeStep(capability: TwainCapability): number {
  const quant = capability.constraint?.quant;
  if (typeof quant === 'number' && quant > 0) {
    return quant;
  }
  return capability.type === 'fixed' ? 0.1 : 1;
}

function formatValue(value: unknown): string {
  if (typeof value === 'string') {
    return value;
  }
  return JSON.stringify(value) ?? '';
}

function booleanCapabilityValue(capability: TwainCapability | undefined): boolean | null {
  if (!capability) {
    return null;
  }
  const value = capability.value;
  if (typeof value === 'boolean') {
    return value;
  }
  if (typeof value === 'number') {
    return value !== 0;
  }
  if (typeof value === 'string') {
    const normalized = value.trim().toLowerCase();
    if (normalized === 'true' || normalized === '1' || normalized === 'yes') {
      return true;
    }
    if (normalized === 'false' || normalized === '0' || normalized === 'no') {
      return false;
    }
  }
  return null;
}

function syncShowUiWithCapabilities(): void {
  if (forceShowUi.value) {
    // CAP_UICONTROLLABLE=false 表示 Source 不支持无 UI 扫描，demo 需要强制走驱动 UI。
    showUi.value = true;
  }
}

function capabilitySetItem(capability: TwainCapability): TwainCapabilitySetItem {
  return {
    cap: capability.cap,
    name: capability.name,
    value: capabilityDrafts.value[capabilityKey(capability)],
  };
}

function rememberAppliedCapability(item: TwainCapabilitySetItem): void {
  const key = item.cap || item.name;
  if (!key) {
    return;
  }
  appliedCapabilityHistory.value = appliedCapabilityHistory.value.filter((existing) => (existing.cap || existing.name) !== key);
  appliedCapabilityHistory.value.push(item);
}

function currentCapabilityItems(): TwainCapabilitySetItem[] {
  if (appliedCapabilityHistory.value.length > 0) {
    // TWAIN capability 设置存在顺序和联动关系，保存 profile 时优先保留用户实际应用过的顺序。
    return appliedCapabilityHistory.value;
  }
  return capabilities.value
    .filter((capability) => (capability.cap || capability.name) && !capabilityDisabledForPatch(capability))
    .map((capability) => capabilitySetItem(capability));
}

function capabilityDisabledForPatch(capability: TwainCapability): boolean {
  return capability.settable === false || capability.readonly === true;
}

async function buildTwainOutputPreviews(task: TwainTask | null): Promise<void> {
  const generation = outputPreviewGeneration + 1;
  outputPreviewGeneration = generation;
  clearOutputPreviews();
  const assets = (task?.assets ?? []).filter((asset) => isBrowserRenderable(asset.content_type ?? '') && Boolean(asset.url || asset.download_url));
  outputPreviews.value = assets.map((asset, index) => ({
    assetId: asset.asset_id || `twain-output-${index + 1}`,
    kind: asset.kind || 'twain_scan_output',
    path: asset.path || '',
    url: asset.url || asset.download_url || '',
    contentType: asset.content_type || '',
    objectUrl: '',
    error: '',
  }));

  await Promise.all(outputPreviews.value.map(async (preview) => {
    if (!authSessionState.sessionToken || !preview.url) {
      preview.error = t('pages.captureAcquisition.thumbnailAuthMissing');
      return;
    }
    try {
      const response = await fetch(resolveAssetUrl(preview.url), {
        headers: { Authorization: `Bearer ${authSessionState.sessionToken}` },
      });
      if (!response.ok) {
        throw new Error(`${response.status} ${response.statusText}`.trim());
      }
      const objectUrl = URL.createObjectURL(await response.blob());
      if (outputPreviewGeneration !== generation) {
        URL.revokeObjectURL(objectUrl);
        return;
      }
      preview.objectUrl = objectUrl;
    } catch (error) {
      preview.error = error instanceof Error ? error.message : t('common.loadFailed');
    }
  }));
}

function clearOutputPreviews(): void {
  outputPreviews.value.forEach((preview) => {
    if (preview.objectUrl) {
      URL.revokeObjectURL(preview.objectUrl);
    }
  });
  outputPreviews.value = [];
}

function openTwainOutputViewer(preview: TwainOutputPreview): void {
  if (!preview.objectUrl) {
    return;
  }
  openImageViewer({
    src: preview.objectUrl,
    title: preview.assetId,
    subtitle: [preview.kind, preview.contentType, preview.path].filter(Boolean).join(' / '),
  });
}

function resolveAssetUrl(path: string): string {
  if (/^https?:\/\//i.test(path)) {
    return path;
  }
  return buildAssetApiUrl(path);
}

function isBrowserRenderable(contentType: string): boolean {
  const value = contentType.toLowerCase();
  return ['image/jpeg', 'image/jpg', 'image/png', 'image/webp', 'image/gif', 'image/bmp'].includes(value);
}
</script>
