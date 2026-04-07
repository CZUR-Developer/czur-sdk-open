<template>
  <article class="rounded-2xl border border-slate-200 bg-white p-5 shadow-sm">
    <div class="text-xs font-semibold uppercase tracking-[0.18em] text-slate-500">{{ t(item.eyebrowKey) }}</div>
    <h4 class="mt-3 text-lg font-semibold text-slate-950">{{ t(item.titleKey) }}</h4>
    <p class="mt-2 text-sm leading-6 text-slate-600">{{ t(item.descriptionKey) }}</p>

    <dl class="mt-4 grid grid-cols-2 gap-3">
      <div
        v-for="stat in item.stats"
        :key="stat.labelKey"
        class="rounded-xl bg-slate-50 px-3 py-3"
      >
        <dt class="text-xs text-slate-500">{{ t(stat.labelKey) }}</dt>
        <dd class="mt-1 font-mono text-base font-semibold text-slate-950">{{ stat.value }}</dd>
      </div>
    </dl>

    <RouterLink :to="item.to" class="mt-4 inline-flex text-sm font-semibold text-sky-700 hover:text-sky-800">
      {{ t(item.ctaKey) }}
    </RouterLink>
  </article>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { RouterLink } from 'vue-router';

import type { EntryCardItem } from '../../types/admin';

defineProps<{
  item: EntryCardItem;
}>();

const { t } = useI18n();
</script>
