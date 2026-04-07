<template>
  <article
    class="rounded-[28px] border border-slate-200/80 bg-white p-5 shadow-[var(--demo-shadow)] transition-colors duration-150 hover:border-cyan-200/80"
  >
    <div class="flex items-start justify-between gap-4">
      <div class="min-w-0">
        <p v-if="eyebrow" class="text-xs font-semibold uppercase tracking-[0.2em] text-slate-500">
          {{ eyebrow }}
        </p>
        <h3 class="mt-2 text-lg font-semibold tracking-tight text-slate-950">{{ title }}</h3>
      </div>
      <StatusPill v-if="badgeLabel" :label="badgeLabel" :tone="badgeTone" />
    </div>

    <p class="mt-3 text-sm leading-6 text-slate-600">{{ description }}</p>
    <p v-if="meta" class="mt-3 text-xs font-medium uppercase tracking-[0.18em] text-slate-400">{{ meta }}</p>

    <div v-if="$slots.default" class="mt-4">
      <slot />
    </div>

    <div v-if="$slots.footer" class="mt-4 flex flex-wrap gap-2">
      <slot name="footer" />
    </div>
  </article>
</template>

<script setup lang="ts">
import type { Tone } from '../../types/demo';
import StatusPill from './StatusPill.vue';

withDefaults(
  defineProps<{
    eyebrow?: string;
    title: string;
    description: string;
    meta?: string;
    badgeLabel?: string;
    badgeTone?: Tone;
  }>(),
  {
    eyebrow: undefined,
    meta: undefined,
    badgeLabel: undefined,
    badgeTone: 'neutral',
  },
);
</script>
