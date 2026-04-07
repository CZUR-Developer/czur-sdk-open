<template>
  <header class="border-b border-slate-200/80 bg-white/95 px-4 py-2 md:px-6">
    <div class="flex min-h-[56px] flex-col gap-3 xl:flex-row xl:items-center xl:justify-between">
      <div class="flex min-w-0 items-center gap-3">
        <button
          type="button"
          class="inline-flex size-9 shrink-0 items-center justify-center rounded-xl border border-slate-200 text-slate-600 shadow-sm lg:hidden"
          @click="$emit('toggle-sidebar')"
        >
          <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
            <path d="M4 6h12M4 10h12M4 14h12" />
          </svg>
        </button>

        <div class="min-w-0">
          <h1 class="truncate text-xl font-semibold tracking-tight text-slate-950 md:text-2xl">{{ title }}</h1>
        </div>
      </div>

      <div class="flex flex-wrap items-center gap-2 xl:justify-end">
        <button
          type="button"
          class="inline-flex size-10 items-center justify-center rounded-2xl border border-slate-200 bg-white text-slate-600 shadow-sm transition hover:border-slate-300 hover:text-slate-950"
          :title="t('actions.setApiKey')"
          @click="$emit('open-api-key')"
        >
          <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
            <path d="M12.5 7.5a3.5 3.5 0 1 0-6.55 1.72L3 12.17V15h2.83l.95-.95H8.5v-1.72h1.72l.96-.95A3.48 3.48 0 0 0 12.5 7.5Z" />
            <path d="M10 7.5h.01" />
          </svg>
        </button>

        <button
          type="button"
          class="inline-flex size-10 items-center justify-center rounded-2xl border border-slate-200 bg-slate-950 text-white shadow-sm transition hover:bg-slate-800"
          :title="t('actions.openDrawer')"
          @click="$emit('open-results')"
        >
          <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
            <path d="M4 6h12M4 10h12M4 14h8" />
          </svg>
        </button>

        <div class="inline-flex items-center gap-1 rounded-2xl border border-slate-200 bg-white/90 p-1">
          <span class="hidden px-2 text-xs font-medium text-slate-500 sm:inline">{{ t('common.locale') }}</span>
          <button
            type="button"
            class="rounded-xl px-2.5 py-1.5 text-sm font-medium transition-colors"
            :class="locale === 'en' ? 'bg-slate-950 text-white shadow-sm' : 'text-slate-500 hover:text-slate-900'"
            @click="$emit('set-locale', 'en')"
          >
            {{ t('common.english') }}
          </button>
          <button
            type="button"
            class="rounded-xl px-2.5 py-1.5 text-sm font-medium transition-colors"
            :class="locale === 'zh-CN' ? 'bg-slate-950 text-white shadow-sm' : 'text-slate-500 hover:text-slate-900'"
            @click="$emit('set-locale', 'zh-CN')"
          >
            {{ t('common.chinese') }}
          </button>
        </div>
      </div>
    </div>
  </header>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';

import type { LocaleKey } from '../../types/demo';

defineProps<{
  title: string;
  locale: LocaleKey;
}>();

defineEmits<{
  'set-locale': [locale: LocaleKey];
  'toggle-sidebar': [];
  'open-results': [];
  'open-api-key': [];
}>();

const { t } = useI18n();
</script>
