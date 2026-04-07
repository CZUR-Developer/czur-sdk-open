<template>
  <section class="rounded-3xl border border-slate-200 bg-white p-6 shadow-sm">
    <header class="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
      <div>
        <h3 class="text-lg font-semibold text-slate-950">{{ t(titleKey) }}</h3>
        <p class="mt-1 text-sm leading-6 text-slate-600">{{ t(subtitleKey) }}</p>
      </div>
      <slot name="header" />
    </header>

    <div class="mt-5 min-w-0">
      <slot />
    </div>
  </section>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';

defineProps<{
  titleKey: string;
  subtitleKey: string;
}>();

const { t } = useI18n();
</script>
