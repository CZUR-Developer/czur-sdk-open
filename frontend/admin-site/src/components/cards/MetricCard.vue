<template>
  <article class="rounded-2xl border border-slate-200 bg-white p-5 shadow-sm">
    <div class="text-xs font-medium uppercase tracking-[0.18em] text-slate-400">{{ item.trend }}</div>
    <div class="mt-3 text-3xl font-semibold tracking-tight" :class="valueClasses">{{ item.value }}</div>
    <div class="mt-3 text-sm font-semibold text-slate-950">{{ t(item.labelKey) }}</div>
    <p class="mt-2 text-sm leading-6 text-slate-600">{{ t(item.detailKey) }}</p>
  </article>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import type { MetricCardItem, Tone } from '../../types/admin';

const props = defineProps<{
  item: MetricCardItem;
}>();

const { t } = useI18n();

const valueClasses = computed(() => {
  const classes: Record<Tone, string> = {
    primary: 'text-sky-700',
    success: 'text-emerald-700',
    warning: 'text-amber-700',
    neutral: 'text-slate-900',
  };

  return classes[props.item.tone];
});
</script>
