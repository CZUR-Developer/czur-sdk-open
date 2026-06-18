<template>
  <div class="space-y-6">
    <p class="rounded-md border border-amber-200 bg-amber-50 px-3 py-2 text-sm text-amber-900">
      {{ t('pages.saneScanning.linuxOnly') }}
    </p>

    <div class="grid gap-6 xl:grid-cols-[0.85fr_1.15fr]">
      <SectionPanel :title="t('sections.deviceInventory')" :description="t('pages.saneScanning.deviceDescription')">
        <div class="space-y-4">
          <div v-if="devices.length === 0" class="rounded-lg border border-dashed border-slate-300 p-5 text-sm text-slate-500">
            {{ t('pages.saneScanning.noSaneDevices') }}
          </div>
          <div v-else class="overflow-hidden rounded-lg border border-slate-200">
            <table class="min-w-full divide-y divide-slate-200 text-sm">
              <thead class="bg-slate-50 text-left text-xs uppercase text-slate-500">
                <tr>
                  <th class="px-3 py-2">{{ t('labels.device') }}</th>
                  <th class="px-3 py-2">{{ t('labels.model') }}</th>
                  <th class="px-3 py-2">{{ t('labels.type') }}</th>
                  <th class="px-3 py-2">{{ t('labels.status') }}</th>
                </tr>
              </thead>
              <tbody class="divide-y divide-slate-100 bg-white">
                <tr
                  v-for="device in devices"
                  :key="device.device_id"
                  class="cursor-pointer hover:bg-cyan-50"
                  :class="selectedDeviceId === device.device_id ? 'bg-cyan-50' : ''"
                  @click="selectedDeviceId = device.device_id"
                >
                  <td class="px-3 py-2 font-mono text-xs text-slate-800">{{ device.device_name || device.device_id }}</td>
                  <td class="px-3 py-2 text-slate-700">{{ device.vendor }} {{ device.model }}</td>
                  <td class="px-3 py-2 text-slate-700">{{ device.type || '-' }}</td>
                  <td class="px-3 py-2">
                    <StatusPill :label="device.status || 'unknown'" :tone="device.status === 'online' ? 'success' : 'neutral'" />
                  </td>
                </tr>
              </tbody>
            </table>
          </div>

          <div class="grid gap-3 md:grid-cols-2">
            <label class="space-y-1 text-sm">
              <span class="font-medium text-slate-700">{{ t('pages.saneScanning.selectedDevice') }}</span>
              <select v-model="selectedDeviceId" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm">
                <option value="">{{ t('common.notSet') }}</option>
                <option v-for="device in devices" :key="device.device_id" :value="device.device_id">
                  {{ device.device_name || device.device_id }}
                </option>
              </select>
            </label>
            <label class="space-y-1 text-sm">
              <span class="font-medium text-slate-700">{{ t('pages.saneScanning.sessionId') }}</span>
              <input v-model="sessionId" class="w-full rounded-md border border-slate-300 px-3 py-2 font-mono text-xs" readonly>
            </label>
          </div>

          <div class="flex flex-wrap gap-2">
            <button class="rounded-md bg-cyan-700 px-3 py-2 text-sm font-semibold text-white disabled:opacity-50" :disabled="isRunning || scanBusy || !selectedDeviceId" @click="openDevice">
              {{ t('pages.saneScanning.openDevice') }}
            </button>
            <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || scanBusy || !sessionId" @click="closeDevice">
              {{ t('pages.saneScanning.closeDevice') }}
            </button>
          </div>
        </div>
      </SectionPanel>

      <SectionPanel :title="t('sections.saneOptions')" :description="t('pages.saneScanning.optionsDescription')">
        <div class="grid gap-4 lg:grid-cols-[1.2fr_0.8fr]">
          <div class="space-y-4">
            <div class="flex flex-wrap gap-2">
              <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || !sessionId" @click="loadOptions">
                {{ t('pages.saneScanning.loadOptions') }}
              </button>
            </div>

            <div class="flex rounded-md border border-slate-200 bg-slate-50 p-1 text-sm">
              <button class="flex-1 rounded px-3 py-2 font-semibold" :class="activeOptionTab === 'basic' ? 'bg-white text-slate-900 shadow-sm' : 'text-slate-600'" @click="activeOptionTab = 'basic'">
                {{ t('pages.saneScanning.basicOptions') }}
              </button>
              <button class="flex-1 rounded px-3 py-2 font-semibold" :class="activeOptionTab === 'advanced' ? 'bg-white text-slate-900 shadow-sm' : 'text-slate-600'" @click="activeOptionTab = 'advanced'">
                {{ t('pages.saneScanning.advancedOptions') }}
              </button>
            </div>

            <div class="max-h-[520px] overflow-auto rounded-lg border border-slate-200 bg-white">
              <div v-if="displayedOptions.length === 0" class="p-5 text-sm text-slate-500">
                {{ t('pages.saneScanning.noOptions') }}
              </div>
              <div v-for="option in displayedOptions" :key="`${option.index}-${option.name}`" class="grid gap-3 border-b border-slate-100 p-4 last:border-b-0 md:grid-cols-[220px_1fr]">
                <div>
                  <div class="flex items-center gap-2">
                    <p class="font-medium text-slate-800">{{ saneText(option.title || option.name) }}</p>
                    <StatusPill v-if="option.inactive" :label="t('pages.saneScanning.inactive')" tone="neutral" />
                    <StatusPill v-else-if="option.settable === false || option.readonly" :label="t('pages.saneScanning.readonly')" tone="neutral" />
                  </div>
                  <p class="mt-1 font-mono text-xs text-slate-500">{{ option.name || option.index }}</p>
                  <p v-if="option.group" class="mt-1 text-xs text-slate-500">{{ saneText(option.group) }}</p>
                </div>

                <div class="space-y-2">
                  <label v-if="option.type === 'bool'" class="inline-flex items-center gap-2 text-sm text-slate-700">
                    <input
                      v-model="optionDrafts[optionKey(option)]"
                      type="checkbox"
                      class="h-4 w-4 rounded border-slate-300"
                      :disabled="optionDisabled(option)"
                      @change="applyOption(option)"
                    >
                    <span>{{ saneText(Boolean(optionDrafts[optionKey(option)])) }}</span>
                  </label>

                  <select
                    v-else-if="hasChoices(option)"
                    v-model="optionDrafts[optionKey(option)]"
                    class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm disabled:bg-slate-100"
                    :disabled="optionDisabled(option)"
                    @change="applyOption(option)"
                  >
                    <option v-for="choice in optionChoices(option)" :key="formatOptionValue(choice)" :value="choice">
                      {{ saneText(choice) }}
                    </option>
                  </select>

                  <div v-else-if="option.constraint?.type === 'range'" class="grid gap-2 md:grid-cols-[1fr_110px]">
                    <input
                      v-model.number="optionDrafts[optionKey(option)]"
                      type="range"
                      class="w-full"
                      :min="option.constraint?.min"
                      :max="option.constraint?.max"
                      :step="rangeStep(option)"
                      :disabled="optionDisabled(option)"
                      @change="applyOption(option)"
                    >
                    <input
                      v-model.number="optionDrafts[optionKey(option)]"
                      type="number"
                      class="rounded-md border border-slate-300 px-3 py-2 text-sm disabled:bg-slate-100"
                      :min="option.constraint?.min"
                      :max="option.constraint?.max"
                      :step="rangeStep(option)"
                      :disabled="optionDisabled(option)"
                      @change="applyOption(option)"
                    >
                  </div>

                  <input
                    v-else-if="option.type === 'int' || option.type === 'fixed'"
                    v-model.number="optionDrafts[optionKey(option)]"
                    type="number"
                    class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm disabled:bg-slate-100"
                    :step="rangeStep(option)"
                    :disabled="optionDisabled(option)"
                    @change="applyOption(option)"
                  >

                  <input
                    v-else
                    v-model="optionDrafts[optionKey(option)]"
                    class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm disabled:bg-slate-100"
                    :disabled="optionDisabled(option)"
                    @change="applyOption(option)"
                  >
                  <p class="text-xs text-slate-500">
                    {{ t('labels.type') }}: {{ option.type || '-' }}
                    <span v-if="option.constraint?.type"> · {{ option.constraint.type }}</span>
                  </p>
                </div>
              </div>
            </div>
          </div>

          <div class="space-y-3">
            <label class="space-y-1 text-sm">
              <span class="font-medium text-slate-700">{{ t('pages.saneScanning.profileName') }}</span>
              <input v-model="profileName" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm">
            </label>
            <div class="flex flex-wrap gap-2">
              <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || scanBusy" @click="loadProfiles">
                {{ t('pages.saneScanning.loadProfiles') }}
              </button>
              <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || scanBusy || !profileName" @click="saveProfile">
                {{ t('pages.saneScanning.saveProfile') }}
              </button>
              <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || scanBusy || !selectedProfileId" @click="applyProfile">
                {{ t('pages.saneScanning.applyProfile') }}
              </button>
            </div>
            <select v-model="selectedProfileId" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm">
              <option value="">{{ t('pages.saneScanning.noProfileSelected') }}</option>
              <option v-for="profile in profiles" :key="profile.profile_id" :value="profile.profile_id">
                {{ profile.name || profile.profile_id }}
              </option>
            </select>
          </div>
        </div>
      </SectionPanel>
    </div>

    <div class="grid gap-6 xl:grid-cols-[0.9fr_1.1fr]">
      <SectionPanel :title="t('pages.saneScanning.scanTitle')" :description="t('pages.saneScanning.scanDescription')">
        <div class="grid gap-3 md:grid-cols-2">
          <label class="space-y-1 text-sm">
            <span class="font-medium text-slate-700">{{ t('pages.saneScanning.outputType') }}</span>
            <select v-model="outputType" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm" :disabled="scanBusy">
              <option value="images">images</option>
              <option value="pdf">pdf</option>
              <option value="ofd">ofd</option>
              <option value="tiff">tiff</option>
            </select>
          </label>
          <label class="space-y-1 text-sm">
            <span class="font-medium text-slate-700">{{ t('pages.saneScanning.exportType') }}</span>
            <select v-model="exportType" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm" :disabled="scanBusy">
              <option value="multi-page">{{ t('pages.saneScanning.multiPage') }}</option>
              <option value="single-page">{{ t('pages.saneScanning.singlePage') }}</option>
            </select>
          </label>
          <label class="space-y-1 text-sm">
            <span class="font-medium text-slate-700">{{ t('pages.saneScanning.outputPath') }}</span>
            <input v-model="outputPath" class="w-full rounded-md border border-slate-300 px-3 py-2 text-sm" :disabled="scanBusy" :placeholder="t('pages.saneScanning.outputPlaceholder')">
          </label>
        </div>
        <div class="mt-4">
          <ImageEnhanceWorkflowPicker
            v-model="selectedEnhanceWorkflowId"
            :title="t('pages.saneScanning.enhanceWorkflow')"
            :empty-label="t('pages.saneScanning.enhanceWorkflowDescription')"
            :none-label="t('pages.saneScanning.noEnhanceWorkflow')"
            :clear-label="t('pages.saneScanning.clearEnhanceWorkflow')"
            :refresh-label="t('pages.saneScanning.refreshEnhanceWorkflows')"
            :loading-label="t('pages.saneScanning.loadingEnhanceWorkflows')"
            @selected="handleEnhanceWorkflowSelected"
          />
        </div>
        <div class="mt-4 flex flex-wrap gap-2">
          <button class="rounded-md bg-cyan-700 px-3 py-2 text-sm font-semibold text-white disabled:opacity-50" :disabled="isRunning || scanBusy || !sessionId" @click="scan">
            {{ t('pages.saneScanning.startScan') }}
          </button>
          <button class="rounded-md border border-slate-300 px-3 py-2 text-sm font-semibold text-slate-700 disabled:opacity-50" :disabled="isRunning || !lastTaskId" @click="loadScanTask">
            {{ t('pages.saneScanning.getScan') }}
          </button>
          <button class="rounded-md border border-rose-300 px-3 py-2 text-sm font-semibold text-rose-700 disabled:opacity-50" :disabled="isRunning || !lastTaskId || !scanBusy" @click="cancelScan">
            {{ t('pages.saneScanning.cancelScan') }}
          </button>
        </div>
        <div v-if="lastTask" class="mt-4 grid gap-3 rounded-lg border border-slate-200 bg-slate-50 p-4 text-sm md:grid-cols-4">
          <div>
            <p class="text-xs text-slate-500">{{ t('labels.status') }}</p>
            <p class="font-semibold text-slate-900">{{ lastTask.status }}</p>
          </div>
          <div>
            <p class="text-xs text-slate-500">{{ t('pages.saneScanning.phase') }}</p>
            <p class="font-semibold text-slate-900">{{ lastTask.phase || '-' }}</p>
          </div>
          <div>
            <p class="text-xs text-slate-500">{{ t('pages.saneScanning.progress') }}</p>
            <p class="font-semibold text-slate-900">{{ lastTask.progress ?? 0 }}%</p>
          </div>
          <div>
            <p class="text-xs text-slate-500">{{ t('labels.pageCount') }}</p>
            <p class="font-semibold text-slate-900">{{ lastTask.page_count ?? 0 }}</p>
          </div>
          <div>
            <p class="text-xs text-slate-500">{{ t('pages.saneScanning.resultPath') }}</p>
            <p class="truncate font-mono text-xs text-slate-800">{{ lastTask.output_path || '-' }}</p>
          </div>
          <div>
            <p class="text-xs text-slate-500">{{ t('pages.saneScanning.currentPage') }}</p>
            <p class="font-semibold text-slate-900">{{ lastTask.current_page ?? 0 }}</p>
          </div>
          <div>
            <p class="text-xs text-slate-500">{{ t('pages.saneScanning.lastPagePath') }}</p>
            <p class="truncate font-mono text-xs text-slate-800">{{ lastTask.last_page_path || '-' }}</p>
          </div>
          <div class="md:col-span-2">
            <p class="text-xs text-slate-500">{{ t('pages.saneScanning.message') }}</p>
            <p class="truncate text-sm font-semibold text-slate-900">{{ lastTask.error || lastTask.message || '-' }}</p>
          </div>
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
              @click="openSaneOutputViewer(preview)"
            >
            <p v-else class="mt-3 rounded-md bg-slate-50 px-3 py-8 text-center text-sm text-slate-500">
              {{ preview.error || t('common.loading') }}
            </p>
          </article>
        </div>
        <div v-if="lastTask?.output_paths?.length" class="mt-3 rounded-lg border border-slate-200 bg-white p-4 text-sm">
          <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.saneScanning.resultPaths') }}</p>
          <ul class="space-y-1">
            <li v-for="path in lastTask.output_paths" :key="path" class="truncate font-mono text-xs text-slate-800">
              {{ path }}
            </li>
          </ul>
        </div>
      </SectionPanel>

      <div class="grid gap-6 lg:grid-cols-2">
        <JsonPanel :title="t('pages.saneScanning.requestJson')" :caption="lastMethod || t('common.request')" :payload="requestJson" />
        <JsonPanel :title="t('pages.saneScanning.responseJson')" :caption="lastStatusLabel" :payload="responseJson" />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import ImageEnhanceWorkflowPicker from '../components/blocks/ImageEnhanceWorkflowPicker.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState, onCommandEvent, sendBoundCommand } from '../services/auth-session';
import type { EnhancePipeline, EnhanceWorkflow } from '../services/image-enhance-workflows';
import { openImageViewer } from '../services/image-viewer';
import { buildAssetApiUrl, buildCommandRequest, isOkResponse, type CommandEvent, type CommandResponse } from '../services/protocol';
import saneI18nZh from '../data/sane_i18n_zh.json';

interface SaneDevice {
  device_id: string;
  device_name?: string;
  vendor?: string;
  model?: string;
  type?: string;
  backend?: string;
  status?: string;
  discovery_source?: string;
  openable?: boolean;
}

interface SaneOption {
  index: number;
  name: string;
  title?: string;
  description?: string;
  group?: string;
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
  inactive?: boolean;
  advanced?: boolean;
}

interface SaneProfile {
  profile_id: string;
  name?: string;
}

interface SaneTask {
  task_id?: string;
  status?: string;
  phase?: string;
  progress?: number;
  page_count?: number;
  current_page?: number;
  output_path?: string;
  output_paths?: string[];
  last_page_path?: string;
  message?: string;
  error?: string;
  assets?: SaneAsset[];
}

interface SaneAsset {
  asset_id?: string;
  kind?: string;
  path?: string;
  url?: string;
  download_url?: string;
  content_type?: string;
  width?: number;
  height?: number;
  size?: number;
}

interface SaneOutputPreview {
  assetId: string;
  kind: string;
  path: string;
  url: string;
  contentType: string;
  objectUrl: string;
  error: string;
}

type SaneI18nDict = Record<string, string>;

const { t, locale } = useI18n();
const saneI18nZhMap = saneI18nZh as SaneI18nDict;
const saneI18nZhLowerMap = Object.fromEntries(
  Object.entries(saneI18nZhMap).map(([key, value]) => [key.toLowerCase(), value]),
);

const isRunning = ref(false);
const watchEnabled = ref(false);
const available = ref(false);
const devices = ref<SaneDevice[]>([]);
const selectedDeviceId = ref('');
const generation = ref(0);
const sessionId = ref('');
const options = ref<SaneOption[]>([]);
const optionDrafts = ref<Record<string, unknown>>({});
const activeOptionTab = ref<'basic' | 'advanced'>('basic');
const profiles = ref<SaneProfile[]>([]);
const profileName = ref('Default scan');
const selectedProfileId = ref('');
const outputType = ref('images');
const exportType = ref('multi-page');
const outputPath = ref('');
const selectedEnhanceWorkflowId = ref('');
const selectedEnhanceWorkflow = ref<EnhanceWorkflow | null>(null);
const lastTaskId = ref('');
const lastTask = ref<SaneTask | null>(null);
const lastRequest = ref<Record<string, unknown> | null>(null);
const lastResponse = ref<CommandResponse<Record<string, unknown>> | null>(null);
const lastMethod = ref('');
const outputPreviews = ref<SaneOutputPreview[]>([]);

const BASIC_OPTION_NAMES = [
  'source',
  'brightness',
  'resolution',
  'scan source',
  'scan resolution',
  'geometry',
  'scan mode',
  'imageprocess',
  'binarization-brightness',
];
const editableOptions = computed(() => options.value.filter((option) => option.type !== 'group' && option.type !== 'button' && option.type !== 'unknown'));
const basicOptions = computed(() => editableOptions.value.filter(isBasicOption));
const advancedOptions = computed(() => editableOptions.value.filter((option) => !isBasicOption(option)));
const displayedOptions = computed(() => activeOptionTab.value === 'basic' ? basicOptions.value : advancedOptions.value);
const requestJson = computed(() => JSON.stringify(lastRequest.value ?? {}, null, 2));
const responseJson = computed(() => JSON.stringify(lastResponse.value ?? {}, null, 2));
const lastStatusLabel = computed(() => (lastResponse.value ? `${lastResponse.value.code} ${lastResponse.value.message}` : t('common.response')));
const scanBusy = computed(() => lastTask.value?.status === 'queued' || lastTask.value?.status === 'running' || lastTask.value?.status === 'converting');
let commandEventUnsubscribe: (() => void) | null = null;
let outputPreviewGeneration = 0;

onMounted(() => {
  void initializeSanePage();
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
  void buildSaneOutputPreviews(task);
});

async function initializeSanePage(): Promise<void> {
  await runStatus();
  commandEventUnsubscribe?.();
  commandEventUnsubscribe = onCommandEvent(handleCommandEvent);
  if (available.value) {
    await refreshDevices();
    await startWatch();
  }
}

async function runStatus(): Promise<void> {
  const response = await runCommand('sane.status');
  const data = response.data;
  available.value = data.available === true;
}

async function refreshDevices(): Promise<void> {
  const response = await runCommand('sane.list', { refresh: true });
  generation.value = typeof response.data.generation === 'number' ? response.data.generation : generation.value;
  updateDevices(Array.isArray(response.data.devices) ? response.data.devices as SaneDevice[] : []);
}

async function startWatch(): Promise<void> {
  const response = await runCommand('sane.watch_start');
  watchEnabled.value = response.data.watching === true;
  generation.value = typeof response.data.generation === 'number' ? response.data.generation : generation.value;
}

async function stopWatch(): Promise<void> {
  const response = await runCommand('sane.watch_stop');
  watchEnabled.value = response.data.watching === true;
  generation.value = typeof response.data.generation === 'number' ? response.data.generation : generation.value;
}

function handleCommandEvent(event: CommandEvent<Record<string, unknown>>): void {
  if (event.event === 'sane.scan_changed') {
    const task = asTask(event.payload?.task);
    if (task) {
      lastTaskId.value = asString(task.task_id) || lastTaskId.value;
      lastTask.value = task;
    }
    return;
  }
  if (!event.event.startsWith('sane.device_')) {
    return;
  }
  const payload = event.payload ?? {};
  generation.value = typeof payload.generation === 'number' ? payload.generation : generation.value;
  if (Array.isArray(payload.devices)) {
    updateDevices(payload.devices as SaneDevice[]);
  } else {
    void refreshDevices();
  }
}

function updateDevices(nextDevices: SaneDevice[]): void {
  const openableDevices = nextDevices.filter((device) => device.openable !== false);
  devices.value = openableDevices;
  if (selectedDeviceId.value && !openableDevices.some((device) => device.device_id === selectedDeviceId.value)) {
    selectedDeviceId.value = '';
    sessionId.value = '';
    options.value = [];
  }
  if (!selectedDeviceId.value && openableDevices.length > 0) {
    selectedDeviceId.value = openableDevices[0].device_id;
  }
}

async function openDevice(): Promise<void> {
  const response = await runCommand('sane.open', { device_id: selectedDeviceId.value });
  sessionId.value = asString(response.data.session_id);
  if (isOkResponse(response)) {
    await loadOptions();
  }
}

async function closeDevice(): Promise<void> {
  await runCommand('sane.close', { session_id: sessionId.value });
  sessionId.value = '';
  options.value = [];
}

async function loadOptions(): Promise<void> {
  const response = await runCommand('sane.get_options', { session_id: sessionId.value });
  options.value = Array.isArray(response.data.options) ? response.data.options as SaneOption[] : [];
  optionDrafts.value = Object.fromEntries(options.value.map((option) => [optionKey(option), normalizeOptionValue(option.value)]));
}

async function applyOption(option: SaneOption): Promise<void> {
  if (!sessionId.value || optionDisabled(option)) {
    return;
  }
  await runCommand('sane.set_options', {
    session_id: sessionId.value,
    options: {
      [option.name || String(option.index)]: normalizeOptionValue(optionDrafts.value[optionKey(option)]),
    },
  });
  await loadOptions();
}

async function loadProfiles(): Promise<void> {
  const response = await runCommand('sane.profile_list', { session_id: sessionId.value, device_id: selectedDeviceId.value });
  profiles.value = Array.isArray(response.data.profiles) ? response.data.profiles as SaneProfile[] : [];
}

async function saveProfile(): Promise<void> {
  await runCommand('sane.profile_save', {
    session_id: sessionId.value,
    device_id: selectedDeviceId.value,
    name: profileName.value,
    options: currentOptionPatch(),
  });
  await loadProfiles();
}

async function applyProfile(): Promise<void> {
  await runCommand('sane.profile_apply', {
    session_id: sessionId.value,
    profile_id: selectedProfileId.value,
  });
  await loadOptions();
}

function handleEnhanceWorkflowSelected(workflow: EnhanceWorkflow | null): void {
  selectedEnhanceWorkflow.value = workflow;
}

function buildSaneEnhancePipeline(): EnhancePipeline | undefined {
  if (!selectedEnhanceWorkflow.value?.pipeline?.steps?.length) {
    return undefined;
  }
  return {
    ...selectedEnhanceWorkflow.value.pipeline,
    target: {
      type: outputType.value,
      format: outputType.value === 'images' ? 'jpg' : outputType.value,
      export_type: exportType.value,
    },
  };
}

async function scan(): Promise<void> {
  const enhancePipeline = buildSaneEnhancePipeline();
  const response = await runCommand('sane.scan', {
    session_id: sessionId.value,
    output: {
      type: outputType.value,
      path: outputPath.value || undefined,
      export_type: exportType.value,
    },
    ...(enhancePipeline ? { pipeline: enhancePipeline } : {}),
  });
  lastTaskId.value = asString(response.data.task_id);
  lastTask.value = asTask(response.data.task);
}

async function loadScanTask(): Promise<void> {
  const response = await runCommand('sane.scan_get', { task_id: lastTaskId.value });
  lastTask.value = asTask(response.data.task);
}

async function cancelScan(): Promise<void> {
  const response = await runCommand('sane.scan_cancel', { task_id: lastTaskId.value });
  lastTask.value = asTask(response.data.task);
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

function asTask(value: unknown): SaneTask | null {
  return value && typeof value === 'object' ? value as SaneTask : null;
}

async function buildSaneOutputPreviews(task: SaneTask | null): Promise<void> {
  const generation = outputPreviewGeneration + 1;
  outputPreviewGeneration = generation;
  clearOutputPreviews();
  const assets = (task?.assets ?? []).filter((asset) => isBrowserRenderable(asset.content_type ?? '') && Boolean(asset.url || asset.download_url));
  outputPreviews.value = assets.map((asset, index) => ({
    assetId: asset.asset_id || `sane-output-${index + 1}`,
    kind: asset.kind || 'sane_scan_output',
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

function openSaneOutputViewer(preview: SaneOutputPreview): void {
  if (!preview.objectUrl) {
    return;
  }
  openImageViewer({
    src: preview.objectUrl,
    title: preview.assetId,
    subtitle: [preview.kind, preview.contentType, preview.path].filter(Boolean).join(' · '),
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

function formatOptionValue(value: unknown): string {
  if (typeof value === 'string') {
    return value;
  }
  return JSON.stringify(value) ?? '';
}

function saneText(value: unknown): string {
  let raw = '';
  if (typeof value === 'boolean') {
    raw = value ? 'True' : 'False';
  } else {
    raw = formatOptionValue(value);
  }
  if (!locale.value.toLowerCase().startsWith('zh')) {
    return raw;
  }
  return saneI18nZhLowerMap[raw.toLowerCase()] || raw;
}

function optionKey(option: SaneOption): string {
  return option.name || String(option.index);
}

function isBasicOption(option: SaneOption): boolean {
  const key = optionKey(option).toLowerCase();
  const title = (option.title || '').toLowerCase();
  return BASIC_OPTION_NAMES.some((name) => key === name || title === name || key.includes(name));
}

function optionDisabled(option: SaneOption): boolean {
  return isRunning.value || scanBusy.value || !sessionId.value || option.settable === false || option.readonly === true || option.inactive === true;
}

function normalizeOptionValue(value: unknown): unknown {
  return value === undefined ? '' : value;
}

function currentOptionPatch(): Record<string, unknown> {
  const patch: Record<string, unknown> = {};
  for (const option of editableOptions.value) {
    patch[optionKey(option)] = normalizeOptionValue(optionDrafts.value[optionKey(option)]);
  }
  return patch;
}

function optionChoices(option: SaneOption): unknown[] {
  const values = option.constraint?.values;
  return Array.isArray(values) ? values : [];
}

function hasChoices(option: SaneOption): boolean {
  const type = option.constraint?.type;
  return (type === 'list' || type === 'string_list') && optionChoices(option).length > 0;
}

function rangeStep(option: SaneOption): number {
  const quant = option.constraint?.quant;
  if (typeof quant === 'number' && quant > 0) {
    return quant;
  }
  return option.type === 'fixed' ? 0.1 : 1;
}

</script>
