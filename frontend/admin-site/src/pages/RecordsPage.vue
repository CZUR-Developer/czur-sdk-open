<template>
  <div class="space-y-6">
    <div v-if="errorMessage" class="rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700">{{ errorMessage }}</div>

    <section class="grid gap-4 md:grid-cols-3">
      <article v-for="metric in metrics" :key="metric.label" class="rounded-2xl border border-slate-200 bg-white p-5 shadow-sm">
        <div class="text-xs font-medium uppercase tracking-[0.18em] text-slate-400">{{ metric.label }}</div>
        <div class="mt-3 text-3xl font-semibold tracking-tight text-slate-950">{{ metric.value }}</div>
      </article>
    </section>

    <SectionPanel title-key="sections.recordsSurface" subtitle-key="sections.recordsSurfaceHint">
      <template #header>
        <button type="button" class="rounded-xl border border-slate-200 px-3 py-2 text-sm font-semibold text-slate-700 hover:bg-slate-50" @click="load">Refresh</button>
      </template>
      <div class="overflow-hidden rounded-2xl border border-slate-200">
        <table class="min-w-full divide-y divide-slate-200 text-sm">
          <thead class="bg-slate-50 text-left text-xs font-semibold uppercase tracking-[0.14em] text-slate-500">
            <tr>
              <th class="px-4 py-3">Time</th>
              <th class="px-4 py-3">Type</th>
              <th class="px-4 py-3">Source</th>
              <th class="px-4 py-3">Method</th>
              <th class="px-4 py-3">Code</th>
              <th class="px-4 py-3">Duration</th>
              <th class="px-4 py-3">Message</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-slate-100 bg-white">
            <tr v-for="record in records" :key="record.id">
              <td class="px-4 py-3 whitespace-nowrap font-mono text-xs text-slate-500">{{ formatTime(record.ts) }}</td>
              <td class="px-4 py-3">
                <span class="rounded-full px-2 py-1 text-xs font-semibold" :class="record.type === 'error' ? 'bg-rose-50 text-rose-700' : 'bg-slate-100 text-slate-700'">{{ record.type }}</span>
              </td>
              <td class="px-4 py-3 font-mono text-xs text-slate-500">{{ record.source }}</td>
              <td class="px-4 py-3 font-semibold text-slate-900">{{ record.method }}</td>
              <td class="px-4 py-3 font-mono">{{ record.code }}</td>
              <td class="px-4 py-3 font-mono">{{ record.durationMs }}ms</td>
              <td class="px-4 py-3 max-w-xl truncate text-slate-600" :title="record.payloadPreview">{{ record.message || record.payloadPreview }}</td>
            </tr>
            <tr v-if="!records.length">
              <td class="px-4 py-8 text-center text-slate-500" colspan="7">No runtime records retained yet.</td>
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
import type { AdminRecord } from '../services/admin-api';
import { fetchRecords } from '../services/admin-api';

const records = ref<AdminRecord[]>([]);
const capacity = ref(0);
const errorMessage = ref('');

const metrics = computed(() => [
  { label: 'Retained', value: String(records.value.length) },
  { label: 'Errors', value: String(records.value.filter((record) => record.type === 'error').length) },
  { label: 'Capacity', value: String(capacity.value || '-') },
]);

function formatTime(ts: number): string {
  return ts ? new Date(ts * 1000).toLocaleTimeString('en-GB', { hour12: false }) : '-';
}

async function load(): Promise<void> {
  try {
    errorMessage.value = '';
    const response = await fetchRecords();
    records.value = response.records ?? [];
    capacity.value = response.capacity ?? 0;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  }
}

onMounted(() => {
  load();
  window.setInterval(load, 3000);
});
</script>
