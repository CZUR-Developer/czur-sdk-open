<template>
  <div class="space-y-6">
    <div v-if="errorMessage" class="rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700">{{ errorMessage }}</div>

    <section class="grid gap-4 lg:grid-cols-2">
      <article v-for="channel in channels" :key="channel.name" class="rounded-3xl border border-slate-200 bg-white p-6 shadow-sm">
        <div class="flex items-start justify-between gap-4">
          <div>
            <p class="text-xs font-semibold uppercase tracking-[0.18em] text-slate-500">{{ channel.kind }}</p>
            <h3 class="mt-2 text-xl font-semibold text-slate-950">{{ channel.name }}</h3>
          </div>
          <span class="rounded-full bg-emerald-50 px-3 py-1 text-sm font-semibold text-emerald-700">listening</span>
        </div>
        <dl class="mt-5 grid gap-3 sm:grid-cols-2">
          <div v-for="item in channel.items" :key="item.label" class="rounded-2xl bg-slate-50 px-4 py-3">
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ item.label }}</dt>
            <dd class="mt-2 font-mono text-lg font-semibold text-slate-950">{{ item.value }}</dd>
          </div>
        </dl>
      </article>
    </section>

    <SectionPanel title-key="sections.websocketIncidents" subtitle-key="sections.websocketIncidentsHint">
      <div class="overflow-hidden rounded-2xl border border-slate-200">
        <table class="min-w-full divide-y divide-slate-200 text-sm">
          <thead class="bg-slate-50 text-left text-xs font-semibold uppercase tracking-[0.14em] text-slate-500">
            <tr>
              <th class="px-4 py-3">Source</th>
              <th class="px-4 py-3">Event</th>
              <th class="px-4 py-3">Code</th>
              <th class="px-4 py-3">Message</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-slate-100 bg-white">
            <tr v-for="record in wsRecords" :key="record.id">
              <td class="px-4 py-3 font-mono text-xs text-slate-500">{{ record.source }}</td>
              <td class="px-4 py-3 font-semibold text-slate-900">{{ record.method }}</td>
              <td class="px-4 py-3 font-mono">{{ record.code }}</td>
              <td class="px-4 py-3 text-slate-600">{{ record.message || '-' }}</td>
            </tr>
            <tr v-if="!wsRecords.length">
              <td class="px-4 py-6 text-slate-500" colspan="4">No websocket events retained yet.</td>
            </tr>
          </tbody>
        </table>
      </div>
    </SectionPanel>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';

import SectionPanel from '../components/blocks/SectionPanel.vue';
import type { AdminRecord, AdminRecords, AdminStatus } from '../services/admin-api';
import { fetchRecords, fetchStatus } from '../services/admin-api';

const status = ref<AdminStatus | null>(null);
const records = ref<AdminRecords | null>(null);
const errorMessage = ref('');

const channels = computed(() => {
  const command = status.value?.ws?.command ?? {};
  const video = status.value?.ws?.video ?? {};
  return [
    {
      kind: 'Command channel',
      name: `ws://${status.value?.bindHost ?? '127.0.0.1'}:${status.value?.ports?.commandWs ?? '-'}`,
      items: [
        { label: 'Active', value: command.activeConnections ?? 0 },
        { label: 'Requests', value: command.requestCount ?? 0 },
        { label: 'Auth failed', value: command.authFailed ?? 0 },
        { label: 'Bound sessions', value: status.value?.application?.boundSessions ?? 0 },
      ],
    },
    {
      kind: 'Video channel',
      name: `ws://${status.value?.bindHost ?? '127.0.0.1'}:${status.value?.ports?.videoWs ?? '-'}`,
      items: [
        { label: 'Active', value: video.activeConnections ?? 0 },
        { label: 'Frames', value: video.binaryFrames ?? 0 },
        { label: 'Bytes', value: video.binaryBytes ?? 0 },
        { label: 'Auth failed', value: video.authFailed ?? 0 },
      ],
    },
  ];
});

const wsRecords = computed<AdminRecord[]>(() =>
  (records.value?.records ?? []).filter((record) => record.source === 'command_ws' || record.source === 'video_ws').slice(0, 20),
);

async function load(): Promise<void> {
  try {
    errorMessage.value = '';
    const [nextStatus, nextRecords] = await Promise.all([fetchStatus(), fetchRecords()]);
    status.value = nextStatus;
    records.value = nextRecords;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  }
}

onMounted(() => {
  load();
  window.setInterval(load, 3000);
});
</script>
