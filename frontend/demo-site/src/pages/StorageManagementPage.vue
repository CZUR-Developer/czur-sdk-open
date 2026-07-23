<template>
  <div class="storage-page min-w-0 w-full max-w-full space-y-6 overflow-hidden">
    <SectionPanel
      class="storage-panel min-w-0 w-full max-w-full overflow-hidden"
      :eyebrow="t('pages.storageManagement.maintenanceEyebrow')"
      :title="t('pages.storageManagement.maintenanceTitle')"
      :description="t('pages.storageManagement.maintenanceDescription')"
    >
      <div class="grid min-w-0 max-w-full gap-5 lg:grid-cols-[minmax(0,1fr)_auto] lg:items-end">
        <div class="grid min-w-0 gap-3 sm:grid-cols-3">
          <div class="metric-cell">
            <span>{{ t('pages.storageManagement.commandState') }}</span>
            <strong>{{ t(`status.${authSessionState.commandState}`) }}</strong>
          </div>
          <div class="metric-cell">
            <span>{{ t('pages.storageManagement.lastCleanup') }}</span>
            <strong>{{ lastCleanupAt || t('common.notSet') }}</strong>
          </div>
          <div class="metric-cell">
            <span>{{ t('pages.storageManagement.clearedTasks') }}</span>
            <strong>{{ lastClearedTaskCount }}</strong>
          </div>
        </div>

        <button
          type="button"
          class="danger-button w-full sm:w-auto"
          :disabled="manualCleanupRunning || scenarioRunning || !sessionReady"
          @click="confirmManualCleanup"
        >
          {{ manualCleanupRunning ? t('pages.storageManagement.cleaning') : t('pages.storageManagement.cleanupNow') }}
        </button>
      </div>

      <p class="mt-4 rounded-md border border-amber-200 bg-amber-50 px-4 py-3 text-sm leading-6 text-amber-800">
        {{ t('pages.storageManagement.cleanupNotice') }}
      </p>

      <div v-if="lastCleanupRequest || lastCleanupResponse" class="mt-5 grid min-w-0 gap-4 xl:grid-cols-2">
        <JsonPanel
          v-if="lastCleanupRequest"
          :title="t('pages.storageManagement.lastCleanupRequest')"
          :payload="formatJson(lastCleanupRequest)"
        />
        <JsonPanel
          v-if="lastCleanupResponse"
          :title="t('pages.storageManagement.lastCleanupResponse')"
          :payload="formatJson(lastCleanupResponse)"
        />
      </div>
    </SectionPanel>

    <SectionPanel
      class="storage-panel min-w-0 w-full max-w-full overflow-hidden"
      :eyebrow="t('pages.storageManagement.scenarioEyebrow')"
      :title="t('pages.storageManagement.scenarioTitle')"
      :description="t('pages.storageManagement.scenarioDescription')"
    >
      <div class="grid min-w-0 max-w-full gap-6 xl:grid-cols-[minmax(280px,0.7fr)_minmax(0,1.3fr)]">
        <div class="min-w-0 space-y-4">
          <label class="field-label">
            {{ t('pages.storageManagement.testImage') }}
            <input
              type="file"
              accept="image/*"
              class="field-input file:mr-3 file:rounded-md file:border-0 file:bg-cyan-50 file:px-3 file:py-1.5 file:font-semibold file:text-cyan-700"
              :disabled="scenarioRunning"
              @change="selectTestImage"
            />
          </label>
          <p class="truncate text-sm font-medium text-slate-700">
            {{ testImage?.name || t('pages.storageManagement.noImageSelected') }}
          </p>

          <label class="field-label">
            {{ t('pages.storageManagement.initialBatchSize') }}
            <select v-model.number="initialBatchSize" class="field-input" :disabled="scenarioRunning">
              <option v-for="size in batchSizes" :key="size" :value="size">
                {{ t('pages.storageManagement.pagesCount', { count: size }) }}
              </option>
            </select>
          </label>

          <div class="grid gap-2 sm:grid-cols-2">
            <button
              type="button"
              class="primary-button"
              :disabled="!canRunScenario"
              @click="runOcrCleanupScenario"
            >
              {{ scenarioRunning ? t('pages.storageManagement.verifying') : t('pages.storageManagement.runFullScenario') }}
            </button>
            <button type="button" class="secondary-button" :disabled="!scenarioRunning" @click="stopScenario">
              {{ t('pages.storageManagement.stopPolling') }}
            </button>
          </div>

          <p class="rounded-md border border-cyan-100 bg-cyan-50 px-4 py-3 text-sm leading-6 text-cyan-800">
            {{ t('pages.storageManagement.ocrStrategyNotice') }}
          </p>
        </div>

        <div class="min-w-0 space-y-4">
          <div class="grid min-w-0 gap-3 sm:grid-cols-3">
            <div class="metric-cell">
              <span>{{ t('pages.storageManagement.scenarioResult') }}</span>
              <StatusPill :label="scenarioStatusLabel" :tone="scenarioStatusTone" />
            </div>
            <div class="metric-cell">
              <span>{{ t('pages.storageManagement.currentTaskId') }}</span>
              <strong class="truncate font-mono text-xs" :title="currentTaskId">{{ currentTaskId || '-' }}</strong>
            </div>
            <div class="metric-cell">
              <span>{{ t('pages.storageManagement.currentTaskStatus') }}</span>
              <strong>{{ currentTaskStatus || '-' }}</strong>
            </div>
          </div>

          <div v-if="scenarioSteps.length" class="divide-y divide-slate-200 border-y border-slate-200">
            <article v-for="step in scenarioSteps" :key="step.id" class="py-4">
              <div class="flex flex-wrap items-start justify-between gap-3">
                <div class="min-w-0">
                  <p class="font-semibold text-slate-900">{{ step.title }}</p>
                  <p class="mt-1 font-mono text-xs text-slate-500">{{ step.method }} · {{ step.time }}</p>
                </div>
                <StatusPill :label="stepStateLabel(step.state)" :tone="stepStateTone(step.state)" />
              </div>
              <p v-if="step.detail" class="mt-2 text-sm leading-6 text-slate-600">{{ step.detail }}</p>
              <div v-if="step.request || step.response" class="mt-3 grid min-w-0 gap-3 xl:grid-cols-2">
                <details v-if="step.request">
                  <summary class="cursor-pointer text-xs font-semibold text-cyan-700">
                    {{ t('pages.storageManagement.inspectRequest') }}
                  </summary>
                  <pre class="mt-2 max-h-72 overflow-auto rounded-md bg-slate-950 p-3 text-xs leading-5 text-slate-200"><code>{{ formatJson(step.request) }}</code></pre>
                </details>
                <details v-if="step.response">
                  <summary class="cursor-pointer text-xs font-semibold text-cyan-700">
                    {{ t('pages.storageManagement.inspectResponse') }}
                  </summary>
                  <pre class="mt-2 max-h-72 overflow-auto rounded-md bg-slate-950 p-3 text-xs leading-5 text-slate-200"><code>{{ formatJson(step.response) }}</code></pre>
                </details>
              </div>
            </article>
          </div>

          <div v-else class="border-y border-dashed border-slate-200 py-8 text-center text-sm text-slate-500">
            {{ t('pages.storageManagement.noScenarioRecords') }}
          </div>
        </div>
      </div>
    </SectionPanel>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import JsonPanel from '../components/blocks/JsonPanel.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState, sendBoundCommand } from '../services/auth-session';
import { buildAssetApiUrl, type CommandRequest, type CommandResponse } from '../services/protocol';
import type { Tone } from '../types/demo';
import {
  classifyBusyCleanup,
  classifyIdempotentCleanup,
  classifyPostCleanupQuery,
  isOcrTerminalStatus,
  nextOcrBatchSize,
  snapshotCommandRequest,
} from '../utils/storage-cleanup-scenario.js';

type ScenarioState = 'idle' | 'running' | 'passed' | 'warning' | 'failed';
type StepState = 'running' | 'passed' | 'warning' | 'failed';

interface ScenarioStep {
  id: number;
  title: string;
  method: string;
  state: StepState;
  detail: string;
  time: string;
  request?: CommandRequest;
  response?: CommandResponse<Record<string, unknown>>;
}

interface CleanupCommandResult {
  request: CommandRequest;
  response: CommandResponse<Record<string, unknown>>;
}

class ScenarioStoppedError extends Error {}

const { t } = useI18n();
const batchSizes = [4, 8, 16, 30];
const testImage = ref<File | null>(null);
const initialBatchSize = ref(8);
const manualCleanupRunning = ref(false);
const lastCleanupAt = ref('');
const lastCleanupRequest = ref<CommandRequest | null>(null);
const lastCleanupResponse = ref<CommandResponse<Record<string, unknown>> | null>(null);
const scenarioRunning = ref(false);
const scenarioState = ref<ScenarioState>('idle');
const scenarioSteps = ref<ScenarioStep[]>([]);
const currentTaskId = ref('');
const currentTaskStatus = ref('');
let scenarioToken = 0;
let stepCounter = 0;

const sessionReady = computed(() => authSessionState.commandState === 'success' && Boolean(authSessionState.sessionToken));
const canRunScenario = computed(() => sessionReady.value && Boolean(testImage.value) && !scenarioRunning.value && !manualCleanupRunning.value);
const lastClearedTaskCount = computed(() => String(lastCleanupResponse.value?.data?.cleared_task_count ?? 0));
const scenarioStatusLabel = computed(() => t(`pages.storageManagement.scenarioStates.${scenarioState.value}`));
const scenarioStatusTone = computed<Tone>(() => {
  if (scenarioState.value === 'passed') return 'success';
  if (scenarioState.value === 'failed') return 'danger';
  if (scenarioState.value === 'warning' || scenarioState.value === 'running') return 'warning';
  return 'neutral';
});

function selectTestImage(event: Event): void {
  const input = event.target as HTMLInputElement;
  testImage.value = input.files?.[0] ?? null;
  input.value = '';
}

async function confirmManualCleanup(): Promise<void> {
  if (!window.confirm(t('pages.storageManagement.cleanupConfirm'))) {
    return;
  }
  manualCleanupRunning.value = true;
  try {
    await sendCleanupCommand();
  } finally {
    manualCleanupRunning.value = false;
  }
}

async function runOcrCleanupScenario(): Promise<void> {
  if (!testImage.value || !canRunScenario.value) {
    return;
  }

  const token = ++scenarioToken;
  scenarioRunning.value = true;
  scenarioState.value = 'running';
  scenarioSteps.value = [];
  currentTaskId.value = '';
  currentTaskStatus.value = '';
  stepCounter = 0;

  try {
    // 先清除历史终态任务，并阻止在已有其他活跃任务时继续，避免把别的任务误判为本次 OCR busy。
    const baselineCleanup = await sendCleanupCommand();
    const baseline = baselineCleanup.response;
    addResponseStep(
      t('pages.storageManagement.steps.baselineCleanup'),
      'storage.cleanup_temp',
      baseline.code === 0 ? 'passed' : 'failed',
      baseline.code === 0
        ? t('pages.storageManagement.details.baselineReady')
        : t('pages.storageManagement.details.baselineBusy'),
      baseline,
      baselineCleanup.request,
    );
    if (baseline.code !== 0) {
      throw new Error(t('pages.storageManagement.errors.existingActiveTasks'));
    }

    let batchSize: number | null = initialBatchSize.value;
    while (batchSize !== null) {
      ensureScenarioActive(token);
      const uploadId = await uploadScenarioImage(testImage.value, token);
      const inputUploadIds = Array.from({ length: batchSize }, () => uploadId);
      // 同一上传资源重复作为批量输入，可以延长真实 OCR 处理窗口，又不增加网络上传和本地文件占用。
      const submit = await sendBoundCommand('ocr.recognize', {
        params: {
          input_upload_ids: inputUploadIds,
          format: 'txt',
          export_type: 'multi-page',
          encoding: 'utf-8',
        },
      });
      const taskId = extractTaskId(submit);
      currentTaskId.value = taskId;
      currentTaskStatus.value = extractTaskStatus(submit);
      addResponseStep(
        t('pages.storageManagement.steps.submitOcr', { count: batchSize }),
        'ocr.recognize',
        submit.code === 0 && Boolean(taskId) ? 'passed' : 'failed',
        taskId || submit.message,
        submit,
      );
      if (submit.code !== 0 || !taskId) {
        throw new Error(submit.message || t('pages.storageManagement.errors.taskIdMissing'));
      }

      ensureScenarioActive(token);
      const busyCleanupResult = await sendCleanupCommand();
      const busyCleanup = busyCleanupResult.response;
      const busyResult = classifyBusyCleanup(busyCleanup);
      if (busyResult.outcome === 'missed-window') {
        addResponseStep(
          t('pages.storageManagement.steps.activeCleanup'),
          'storage.cleanup_temp',
          'warning',
          t('pages.storageManagement.details.missedWindow', { count: batchSize }),
          busyCleanup,
          busyCleanupResult.request,
        );
        // 清理已经成功，旧 upload_id 也随资产索引失效；下一轮必须重新上传后再扩大批量页数。
        batchSize = nextOcrBatchSize(batchSize);
        if (batchSize === null) {
          scenarioState.value = 'warning';
          throw new Error(t('pages.storageManagement.errors.activeWindowNotReached'));
        }
        continue;
      }
      addResponseStep(
        t('pages.storageManagement.steps.activeCleanup'),
        'storage.cleanup_temp',
        busyResult.outcome === 'passed' ? 'passed' : 'failed',
        busyResult.outcome === 'passed'
          ? t('pages.storageManagement.details.busyVerified')
          : t('pages.storageManagement.details.unexpectedBusy'),
        busyCleanup,
        busyCleanupResult.request,
      );
      if (busyResult.outcome !== 'passed') {
        throw new Error(t('pages.storageManagement.errors.busyAssertionFailed'));
      }

      const aliveQuery = await queryOcrTask(taskId);
      currentTaskStatus.value = extractTaskStatus(aliveQuery);
      addResponseStep(
        t('pages.storageManagement.steps.queryAfterBusy'),
        'ocr.get',
        aliveQuery.code === 0 ? 'passed' : 'failed',
        aliveQuery.code === 0
          ? t('pages.storageManagement.details.taskPreserved')
          : aliveQuery.message,
        aliveQuery,
      );
      if (aliveQuery.code !== 0) {
        throw new Error(t('pages.storageManagement.errors.taskWasRemovedWhileBusy'));
      }

      const terminalQuery = await waitForOcrTerminal(taskId, token);
      addResponseStep(
        t('pages.storageManagement.steps.waitTerminal'),
        'ocr.get',
        'passed',
        t('pages.storageManagement.details.terminalReached', { status: currentTaskStatus.value }),
        terminalQuery,
      );

      const finalCleanupResult = await sendCleanupCommand();
      const finalCleanup = finalCleanupResult.response;
      addResponseStep(
        t('pages.storageManagement.steps.terminalCleanup'),
        'storage.cleanup_temp',
        finalCleanup.code === 0 ? 'passed' : 'failed',
        finalCleanup.code === 0
          ? t('pages.storageManagement.details.terminalCleanupPassed')
          : finalCleanup.message,
        finalCleanup,
        finalCleanupResult.request,
      );
      if (finalCleanup.code !== 0) {
        throw new Error(t('pages.storageManagement.errors.terminalCleanupFailed'));
      }

      const removedQuery = await queryOcrTask(taskId);
      const removed = classifyPostCleanupQuery(removedQuery);
      addResponseStep(
        t('pages.storageManagement.steps.queryAfterCleanup'),
        'ocr.get',
        removed ? 'passed' : 'failed',
        removed ? t('pages.storageManagement.details.taskRemoved') : removedQuery.message,
        removedQuery,
      );
      if (!removed) {
        throw new Error(t('pages.storageManagement.errors.taskStillQueryable'));
      }

      const repeatedCleanupResult = await sendCleanupCommand();
      const repeatedCleanup = repeatedCleanupResult.response;
      const idempotent = classifyIdempotentCleanup(repeatedCleanup);
      addResponseStep(
        t('pages.storageManagement.steps.repeatCleanup'),
        'storage.cleanup_temp',
        idempotent ? 'passed' : 'failed',
        idempotent ? t('pages.storageManagement.details.idempotentPassed') : t('pages.storageManagement.details.idempotentFailed'),
        repeatedCleanup,
        repeatedCleanupResult.request,
      );
      if (!idempotent) {
        throw new Error(t('pages.storageManagement.errors.idempotentAssertionFailed'));
      }

      scenarioState.value = 'passed';
      return;
    }
  } catch (error) {
    if (error instanceof ScenarioStoppedError) {
      scenarioState.value = 'warning';
      addLocalStep(t('pages.storageManagement.steps.stopped'), 'client', 'warning', t('pages.storageManagement.details.stoppedNotice'));
    } else {
      if (scenarioState.value !== 'warning') {
        scenarioState.value = 'failed';
      }
      addLocalStep(
        t('pages.storageManagement.steps.scenarioFailed'),
        'client',
        scenarioState.value === 'warning' ? 'warning' : 'failed',
        error instanceof Error ? error.message : String(error),
      );
    }
  } finally {
    if (token === scenarioToken) {
      scenarioRunning.value = false;
    }
  }
}

function stopScenario(): void {
  // 停止只中断浏览器轮询，不隐式取消后端 OCR；剩余活跃任务仍受 storage.cleanup_temp 的 busy 保护。
  scenarioToken += 1;
  scenarioRunning.value = false;
  scenarioState.value = 'warning';
}

async function uploadScenarioImage(file: File, token: number): Promise<string> {
  ensureScenarioActive(token);
  const body = new FormData();
  body.append('file', file);
  const response = await fetch(buildAssetApiUrl('/api/uploads/images'), {
    method: 'POST',
    headers: { Authorization: `Bearer ${authSessionState.sessionToken}` },
    body,
  });
  const payload = (await response.json()) as Record<string, unknown>;
  const uploadId = asString(payload.upload_id);
  addLocalStep(
    t('pages.storageManagement.steps.uploadImage'),
    'POST /api/uploads/images',
    response.ok && Boolean(uploadId) ? 'passed' : 'failed',
    uploadId || asString(payload.message) || `${response.status} ${response.statusText}`.trim(),
  );
  if (!response.ok || !uploadId) {
    throw new Error(asString(payload.message) || t('pages.storageManagement.errors.uploadFailed'));
  }
  return uploadId;
}

async function waitForOcrTerminal(taskId: string, token: number): Promise<CommandResponse<Record<string, unknown>>> {
  for (let attempt = 0; attempt < 240; attempt += 1) {
    ensureScenarioActive(token);
    const response = await queryOcrTask(taskId);
    if (response.code !== 0) {
      throw new Error(response.message || t('pages.storageManagement.errors.queryFailed'));
    }
    currentTaskStatus.value = extractTaskStatus(response);
    if (isOcrTerminalStatus(currentTaskStatus.value)) {
      return response;
    }
    await delay(500);
  }
  throw new Error(t('pages.storageManagement.errors.terminalTimeout'));
}

function queryOcrTask(taskId: string): Promise<CommandResponse<Record<string, unknown>>> {
  return sendBoundCommand('ocr.get', { params: { task_id: taskId } });
}

function rememberCleanup(response: CommandResponse<Record<string, unknown>>): void {
  lastCleanupResponse.value = response;
  lastCleanupAt.value = new Date().toLocaleTimeString();
}

async function sendCleanupCommand(): Promise<CleanupCommandResult> {
  let request: CommandRequest | null = null;
  const response = await sendBoundCommand('storage.cleanup_temp', {
    onRequest(sentRequest) {
      request = snapshotCommandRequest(sentRequest) as CommandRequest;
      lastCleanupRequest.value = request;
    },
  });
  if (!request) {
    throw new Error('storage.cleanup_temp request was not captured');
  }
  rememberCleanup(response);
  return { request, response };
}

function ensureScenarioActive(token: number): void {
  if (token !== scenarioToken) {
    throw new ScenarioStoppedError('scenario stopped');
  }
}

function extractTaskId(response: CommandResponse<Record<string, unknown>>): string {
  const task = asRecord(response.data?.task);
  return asString(response.data?.task_id) || asString(task.task_id);
}

function extractTaskStatus(response: CommandResponse<Record<string, unknown>>): string {
  return asString(asRecord(response.data?.task).status);
}

function addResponseStep(
  title: string,
  method: string,
  state: StepState,
  detail: string,
  response: CommandResponse<Record<string, unknown>>,
  request?: CommandRequest,
): void {
  scenarioSteps.value.push({ id: ++stepCounter, title, method, state, detail, time: new Date().toLocaleTimeString(), request, response });
}

function addLocalStep(title: string, method: string, state: StepState, detail: string): void {
  scenarioSteps.value.push({ id: ++stepCounter, title, method, state, detail, time: new Date().toLocaleTimeString() });
}

function stepStateLabel(state: StepState): string {
  return t(`pages.storageManagement.stepStates.${state}`);
}

function stepStateTone(state: StepState): Tone {
  if (state === 'passed') return 'success';
  if (state === 'failed') return 'danger';
  return 'warning';
}

function asRecord(value: unknown): Record<string, unknown> {
  return value && typeof value === 'object' ? value as Record<string, unknown> : {};
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function formatJson(value: unknown): string {
  return JSON.stringify(value, null, 2);
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

onBeforeUnmount(() => {
  scenarioToken += 1;
});
</script>

<style scoped>
.storage-page,
.storage-panel {
  width: 100%;
  max-width: 100%;
}

.storage-panel :deep(*) {
  min-width: 0;
  overflow-wrap: anywhere;
}

@media (max-width: 639px) {
  .storage-page,
  .storage-panel {
    width: calc(100vw - 2rem);
    max-width: calc(100vw - 2rem);
  }
}

.metric-cell {
  display: flex;
  min-width: 0;
  flex-direction: column;
  gap: 0.4rem;
  border-left: 2px solid rgb(207 250 254);
  padding-left: 0.875rem;
}

.metric-cell > span,
.field-label {
  color: rgb(100 116 139);
  font-size: 0.75rem;
  font-weight: 700;
  text-transform: uppercase;
}

.metric-cell > strong {
  color: rgb(15 23 42);
  font-size: 0.875rem;
}

.field-input {
  margin-top: 0.35rem;
  min-width: 0;
  max-width: 100%;
  width: 100%;
  border: 1px solid rgb(203 213 225);
  border-radius: 0.375rem;
  background: white;
  padding: 0.6rem 0.75rem;
  color: rgb(30 41 59);
  font-size: 0.875rem;
  outline: none;
}

.field-input:focus {
  border-color: rgb(8 145 178);
  box-shadow: 0 0 0 3px rgb(207 250 254);
}

.primary-button,
.secondary-button,
.danger-button {
  display: inline-flex;
  min-height: 2.6rem;
  align-items: center;
  justify-content: center;
  border: 1px solid rgb(203 213 225);
  border-radius: 0.375rem;
  padding: 0.6rem 1rem;
  font-size: 0.875rem;
  font-weight: 700;
}

.primary-button {
  border-color: rgb(8 145 178);
  background: rgb(8 145 178);
  color: white;
}

.secondary-button {
  background: white;
  color: rgb(51 65 85);
}

.danger-button {
  border-color: rgb(225 29 72);
  background: rgb(225 29 72);
  color: white;
}

.primary-button:disabled,
.secondary-button:disabled,
.danger-button:disabled,
.field-input:disabled {
  cursor: not-allowed;
  opacity: 0.5;
}
</style>
