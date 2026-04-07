<template>
  <article class="rounded-[28px] border border-slate-200/80 bg-white p-5 shadow-[var(--demo-shadow)]">
    <div class="flex items-start justify-between gap-4">
      <div>
        <p class="text-xs font-semibold uppercase tracking-[0.22em] text-slate-500">{{ label }}</p>
        <h3 class="mt-3 text-3xl font-semibold tracking-tight text-slate-950">{{ value }}</h3>
      </div>
      <StatusPill v-if="trend" :label="trend" :tone="tone" />
    </div>
    <p class="mt-4 text-sm leading-6 text-slate-600">{{ detail }}</p>
  </article>
</template>

<script setup lang="ts">
import type { Tone } from '../../types/demo';
import StatusPill from './StatusPill.vue';

withDefaults(
  defineProps<{
    label: string;
    value: string;
    detail: string;
    trend?: string;
    tone?: Tone;
  }>(),
  {
    trend: undefined,
    tone: 'neutral',
  },
);
</script>
