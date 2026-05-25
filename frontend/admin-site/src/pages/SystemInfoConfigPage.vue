<template>
  <div class="space-y-6">
    <div v-if="errorMessage" class="rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700">{{ errorMessage }}</div>

    <section class="grid gap-4 md:grid-cols-3">
      <article v-for="metric in metrics" :key="metric.label" class="rounded-2xl border border-slate-200 bg-white p-5 shadow-sm">
        <div class="text-xs font-medium uppercase tracking-[0.18em] text-slate-400">{{ metric.hint }}</div>
        <div class="mt-3 text-2xl font-semibold tracking-tight text-slate-950">{{ metric.value }}</div>
        <div class="mt-3 text-sm font-semibold text-slate-700">{{ metric.label }}</div>
      </article>
    </section>

    <div class="grid gap-6 2xl:grid-cols-2">
      <SectionPanel title-key="sections.systemProfiles" subtitle-key="sections.systemProfilesHint">
        <dl class="grid gap-3">
          <div v-for="row in profileRows" :key="row.label" class="rounded-2xl bg-slate-50 px-4 py-4">
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ row.label }}</dt>
            <dd class="mt-2 break-all font-mono text-sm text-slate-900">{{ row.value }}</dd>
          </div>
        </dl>
      </SectionPanel>

      <SectionPanel title-key="sections.systemFlags" subtitle-key="sections.systemFlagsHint">
        <dl class="grid gap-3">
          <div v-for="row in hardwareRows" :key="row.label" class="rounded-2xl bg-slate-50 px-4 py-4">
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ row.label }}</dt>
            <dd class="mt-2 break-all font-mono text-sm text-slate-900">{{ row.value }}</dd>
          </div>
        </dl>
      </SectionPanel>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';

import SectionPanel from '../components/blocks/SectionPanel.vue';
import type { AdminSystem } from '../services/admin-api';
import { fetchSystem } from '../services/admin-api';

const system = ref<AdminSystem | null>(null);
const errorMessage = ref('');

function formatBytes(value: unknown): string {
  const bytes = Number(value ?? 0);
  if (!Number.isFinite(bytes) || bytes <= 0) return '-';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let next = bytes;
  let index = 0;
  while (next >= 1024 && index < units.length - 1) {
    next /= 1024;
    index += 1;
  }
  return `${next.toFixed(index === 0 ? 0 : 1)} ${units[index]}`;
}

const metrics = computed(() => [
  { label: 'Version', value: system.value?.software?.version ?? '-', hint: system.value?.software?.protocolVersion ?? 'protocol' },
  { label: 'Process', value: String(system.value?.process?.pid ?? '-'), hint: `${system.value?.process?.uptimeSec ?? 0}s uptime` },
  { label: 'Host', value: system.value?.os?.nodename ?? '-', hint: system.value?.os?.machine ?? '-' },
]);

const profileRows = computed(() => [
  { label: 'Executable', value: String(system.value?.process?.executablePath ?? '-') },
  { label: 'Build', value: `${system.value?.software?.buildDate ?? '-'} ${system.value?.software?.buildTime ?? ''}` },
  { label: 'OS', value: `${system.value?.os?.sysname ?? '-'} ${system.value?.os?.release ?? ''}` },
  { label: 'Work directory', value: String(system.value?.runtime?.workDir ?? '-') },
  { label: 'Log directory', value: String(system.value?.runtime?.logDir ?? '-') },
]);

const hardwareRows = computed(() => [
  { label: 'CPU', value: `${system.value?.hardware?.cpuModel ?? '-'} · ${system.value?.hardware?.cpuCores ?? '-'} cores` },
  { label: 'Memory', value: `${formatBytes(system.value?.hardware?.memoryFreeBytes)} free / ${formatBytes(system.value?.hardware?.memoryTotalBytes)} total` },
  { label: 'Work disk', value: `${formatBytes(system.value?.hardware?.workDirDiskFreeBytes)} free / ${formatBytes(system.value?.hardware?.workDirDiskTotalBytes)} total` },
  { label: 'Ports', value: Object.entries(system.value?.ports ?? {}).map(([key, value]) => `${key}:${value}`).join(' · ') || '-' },
  { label: 'Web root', value: String(system.value?.runtime?.webRoot ?? '-') },
]);

async function load(): Promise<void> {
  try {
    errorMessage.value = '';
    system.value = await fetchSystem();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  }
}

onMounted(load);
</script>
