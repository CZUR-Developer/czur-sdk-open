<template>
  <dl class="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
    <div
      v-for="item in items"
      :key="`${item.label}-${item.value}`"
      class="rounded-2xl border border-slate-200/80 bg-slate-50/80 p-4"
    >
      <dt class="text-xs font-semibold uppercase tracking-[0.2em] text-slate-500">{{ item.label }}</dt>
      <dd class="mt-2 text-sm font-medium text-slate-900" :class="item.monospace ? 'font-mono text-[13px]' : ''">
        {{ item.value }}
      </dd>
      <p v-if="item.hint" class="mt-2 text-xs leading-5 text-slate-500">{{ item.hint }}</p>
    </div>
  </dl>
</template>

<script setup lang="ts">
import type { KeyValueItem } from '../../types/demo';

defineProps<{
  items: KeyValueItem[];
}>();
</script>
