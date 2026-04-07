<template>
  <div
    class="inline-flex items-center gap-2 rounded-full border px-3 py-1.5 text-sm"
    :class="toneClasses"
  >
    <span class="text-xs font-medium text-slate-500">{{ label }}</span>
    <span class="font-semibold">{{ value }}</span>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';

import type { Tone } from '../../types/admin';

const props = defineProps<{
  label: string;
  value: string;
  tone: Tone;
}>();

const toneClasses = computed(() => {
  const classes: Record<Tone, string> = {
    primary: 'border-sky-200 bg-sky-50 text-sky-700',
    success: 'border-emerald-200 bg-emerald-50 text-emerald-700',
    warning: 'border-amber-200 bg-amber-50 text-amber-700',
    neutral: 'border-slate-200 bg-slate-50 text-slate-700',
  };

  return classes[props.tone];
});
</script>
