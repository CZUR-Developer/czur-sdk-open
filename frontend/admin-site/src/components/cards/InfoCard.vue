<template>
  <article class="rounded-2xl border border-slate-200 bg-white p-5 shadow-sm">
    <div class="flex items-center justify-between gap-3">
      <div class="text-xs font-semibold uppercase tracking-[0.18em] text-slate-500">{{ t(item.eyebrowKey) }}</div>
      <span
        v-if="item.badgeKey"
        class="rounded-full px-2.5 py-1 text-xs font-medium"
        :class="badgeClasses"
      >
        {{ t(item.badgeKey) }}
      </span>
    </div>
    <h4 class="mt-3 text-lg font-semibold text-slate-950">{{ item.title }}</h4>
    <p class="mt-2 text-sm leading-6 text-slate-600">{{ t(item.descriptionKey) }}</p>
    <div class="mt-4 font-mono text-xs text-slate-500">{{ item.meta }}</div>
  </article>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import type { InfoCardItem, Tone } from '../../types/admin';

const props = defineProps<{
  item: InfoCardItem;
}>();

const { t } = useI18n();

const badgeClasses = computed(() => {
  const classes: Record<Tone, string> = {
    primary: 'bg-sky-50 text-sky-700',
    success: 'bg-emerald-50 text-emerald-700',
    warning: 'bg-amber-50 text-amber-700',
    neutral: 'bg-slate-100 text-slate-700',
  };

  return classes[props.item.tone];
});
</script>
