<template>
  <ol class="space-y-3">
    <li
      v-for="item in items"
      :key="item.id"
      class="rounded-[24px] border border-white/70 bg-white/85 p-4 shadow-[var(--demo-shadow)]"
    >
      <div class="flex flex-wrap items-start justify-between gap-3">
        <div class="min-w-0">
          <p class="text-sm font-semibold text-slate-950">{{ item.title }}</p>
          <p class="mt-1 text-sm leading-6 text-slate-600">{{ item.detail }}</p>
        </div>
        <StatusPill :label="item.meta" :tone="item.tone" />
      </div>
    </li>
  </ol>
</template>

<script setup lang="ts">
import type { TimelineItem } from '../../types/demo';
import StatusPill from '../cards/StatusPill.vue';

defineProps<{
  items: TimelineItem[];
}>();
</script>
