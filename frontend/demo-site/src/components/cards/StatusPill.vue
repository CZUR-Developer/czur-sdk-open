<template>
  <span
    class="inline-flex items-center gap-1 rounded-full border px-2.5 py-1 text-[11px] font-semibold uppercase tracking-[0.16em]"
    :class="toneClass"
  >
    <span class="size-1.5 rounded-full bg-current opacity-70" />
    {{ label }}
  </span>
</template>

<script setup lang="ts">
import { computed } from 'vue';

import type { Tone } from '../../types/demo';

const props = withDefaults(
  defineProps<{
    label: string;
    tone?: Tone;
  }>(),
  {
    tone: 'neutral',
  },
);

const toneClass = computed(() => {
  switch (props.tone) {
    case 'primary':
      return 'border-cyan-200 bg-cyan-50 text-cyan-700';
    case 'success':
      return 'border-emerald-200 bg-emerald-50 text-emerald-700';
    case 'warning':
      return 'border-amber-200 bg-amber-50 text-amber-700';
    case 'danger':
      return 'border-rose-200 bg-rose-50 text-rose-700';
    case 'info':
      return 'border-sky-200 bg-sky-50 text-sky-700';
    default:
      return 'border-slate-200 bg-white text-slate-600';
  }
});
</script>
