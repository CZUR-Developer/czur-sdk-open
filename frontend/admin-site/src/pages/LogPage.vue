<template>
  <div class="space-y-6">
    <div v-if="errorMessage" class="rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700">{{ errorMessage }}</div>

    <div class="grid gap-6 2xl:grid-cols-[360px_minmax(0,1fr)]">
      <SectionPanel title-key="sections.logSurface" subtitle-key="sections.logSurfaceHint">
        <div class="space-y-3">
          <button
            v-for="log in logs"
            :key="log.id"
            type="button"
            class="w-full rounded-2xl border px-4 py-3 text-left transition"
            :class="selectedId === log.id ? 'border-sky-300 bg-sky-50' : 'border-slate-200 bg-white hover:bg-slate-50'"
            @click="selectLog(log.id)"
          >
            <div class="flex items-center justify-between gap-3">
              <span class="font-semibold text-slate-950">{{ log.label }}</span>
              <span class="rounded-full px-2 py-1 text-xs font-semibold" :class="log.exists ? 'bg-emerald-50 text-emerald-700' : 'bg-slate-100 text-slate-500'">
                {{ log.exists ? 'exists' : 'missing' }}
              </span>
            </div>
            <div class="mt-2 break-all font-mono text-xs text-slate-500">{{ log.path }}</div>
            <div class="mt-2 text-xs text-slate-500">{{ formatBytes(log.size) }} · {{ formatTime(log.mtime) }}</div>
          </button>
        </div>
      </SectionPanel>

      <SectionPanel title-key="sections.logTimeline" subtitle-key="sections.logTimelineHint">
        <template #header>
          <div class="flex gap-2">
            <button type="button" class="rounded-xl border border-slate-200 px-3 py-2 text-sm font-semibold text-slate-700 hover:bg-slate-50" @click="reloadSelected">Refresh</button>
            <button type="button" class="rounded-xl bg-slate-950 px-3 py-2 text-sm font-semibold text-white hover:bg-slate-800" @click="downloadSelected">Download tail</button>
          </div>
        </template>
        <pre class="max-h-[640px] overflow-auto rounded-2xl bg-slate-950 p-4 text-xs leading-6 text-slate-100">{{ content || 'Select a log file.' }}</pre>
      </SectionPanel>
    </div>
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue';

import SectionPanel from '../components/blocks/SectionPanel.vue';
import type { AdminLogFile } from '../services/admin-api';
import { fetchLogContent, fetchLogs } from '../services/admin-api';

const logs = ref<AdminLogFile[]>([]);
const selectedId = ref('');
const content = ref('');
const errorMessage = ref('');

function formatBytes(bytes: number): string {
  if (!bytes) return '-';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

function formatTime(ts: number): string {
  return ts ? new Date(ts * 1000).toLocaleString() : '-';
}

async function loadLogs(): Promise<void> {
  const response = await fetchLogs();
  logs.value = response.logs ?? [];
  if (!selectedId.value && logs.value.length) {
    selectedId.value = logs.value[0].id;
    await reloadSelected();
  }
}

async function selectLog(id: string): Promise<void> {
  selectedId.value = id;
  await reloadSelected();
}

async function reloadSelected(): Promise<void> {
  if (!selectedId.value) return;
  try {
    errorMessage.value = '';
    const response = await fetchLogContent(selectedId.value);
    content.value = response.content;
  } catch (error) {
    content.value = '';
    errorMessage.value = error instanceof Error ? error.message : String(error);
  }
}

function downloadSelected(): void {
  const blob = new Blob([content.value], { type: 'text/plain;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = `${selectedId.value || 'sdk-log'}-tail.log`;
  link.click();
  URL.revokeObjectURL(url);
}

onMounted(() => {
  loadLogs().catch((error) => {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  });
});
</script>
