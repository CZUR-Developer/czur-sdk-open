<template>
  <div class="space-y-6">
    <section class="grid gap-4 md:grid-cols-2 2xl:grid-cols-4">
      <article v-for="metric in metrics" :key="metric.label" class="rounded-2xl border border-slate-200 bg-white p-5 shadow-sm">
        <div class="text-xs font-medium uppercase tracking-[0.18em] text-slate-400">{{ metric.hint }}</div>
        <div class="mt-3 text-3xl font-semibold tracking-tight text-sky-700">{{ metric.value }}</div>
        <div class="mt-3 text-sm font-semibold text-slate-950">{{ metric.label }}</div>
      </article>
    </section>

    <div class="grid gap-6 2xl:grid-cols-[minmax(0,1.1fr)_minmax(0,0.9fr)]">
      <SectionPanel title-key="sections.dashboardRuntime" subtitle-key="sections.dashboardRuntimeHint">
        <div v-if="errorMessage" class="rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700">{{ errorMessage }}</div>
        <dl v-else class="grid gap-3">
          <div v-for="row in runtimeRows" :key="row.label" class="rounded-2xl bg-slate-50 px-4 py-4">
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ row.label }}</dt>
            <dd class="mt-2 break-all font-mono text-sm text-slate-900">{{ row.value }}</dd>
          </div>
        </dl>
      </SectionPanel>

      <SectionPanel title-key="sections.dashboardModules" subtitle-key="sections.dashboardModulesHint">
        <div class="grid gap-3">
          <RouterLink
            v-for="link in links"
            :key="link.to"
            :to="link.to"
            class="flex items-center justify-between rounded-2xl border border-slate-200 px-4 py-3 text-sm font-semibold text-slate-800 transition hover:bg-slate-50"
          >
            <span>{{ link.label }}</span>
            <span class="font-mono text-xs text-slate-500">{{ link.detail }}</span>
          </RouterLink>
        </div>
      </SectionPanel>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { RouterLink } from 'vue-router';

import SectionPanel from '../components/blocks/SectionPanel.vue';
import type { AdminStatus } from '../services/admin-api';
import { fetchStatus } from '../services/admin-api';

const status = ref<AdminStatus | null>(null);
const errorMessage = ref('');

const metrics = computed(() => {
  const command = status.value?.ws?.command ?? {};
  const video = status.value?.ws?.video ?? {};
  return [
    { label: 'Runtime', value: status.value?.running ? 'Running' : 'Stopped', hint: `${status.value?.uptimeSec ?? 0}s` },
    { label: 'Command WS', value: String(command.activeConnections ?? 0), hint: `${command.requestCount ?? 0} requests` },
    { label: 'Video WS', value: String(video.activeConnections ?? 0), hint: `${video.binaryFrames ?? 0} frames` },
    { label: 'Sessions', value: String(status.value?.application?.boundSessions ?? 0), hint: `${status.value?.application?.activeStreams ?? 0} streams` },
  ];
});

const runtimeRows = computed(() => [
  { label: 'Bind host', value: status.value?.bindHost ?? '-' },
  { label: 'Process', value: `${status.value?.process?.pid ?? '-'} · ${status.value?.process?.executablePath ?? '-'}` },
  { label: 'Admin/Demo/Asset', value: `${status.value?.ports?.adminHttp ?? '-'} / ${status.value?.ports?.demoHttp ?? '-'} / ${status.value?.ports?.assetHttp ?? '-'}` },
  { label: 'Command/Video WS', value: `${status.value?.ports?.commandWs ?? '-'} / ${status.value?.ports?.videoWs ?? '-'}` },
  { label: 'Asset base URL', value: status.value?.http?.assetBaseUrl ?? '-' },
]);

const links = [
  { label: 'WebSocket services', detail: 'Command + Video', to: '/websocket' },
  { label: 'System information', detail: 'Host + process', to: '/system' },
  { label: 'Authorization', detail: 'Sessions + capabilities', to: '/auth' },
  { label: 'Logs', detail: 'Runtime log files', to: '/log' },
  { label: 'Events & results', detail: 'Requests + errors', to: '/records' },
  { label: 'Configuration', detail: 'Gateway hosts', to: '/config' },
];

async function load(): Promise<void> {
  try {
    errorMessage.value = '';
    status.value = await fetchStatus();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  }
}

onMounted(() => {
  load();
  window.setInterval(load, 3000);
});
</script>
