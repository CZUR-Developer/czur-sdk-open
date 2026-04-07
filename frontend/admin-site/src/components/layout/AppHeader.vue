<template>
  <header class="min-h-[84px] border-b border-slate-200 bg-white/90 px-4 py-2.5 backdrop-blur md:px-6">
    <div class="flex min-h-[64px] flex-col justify-center gap-2 xl:flex-row xl:items-center xl:justify-between">
      <div class="flex min-w-0 items-start gap-3">
        <button
          type="button"
          class="inline-flex size-10 shrink-0 items-center justify-center rounded-lg border border-slate-200 text-slate-600 shadow-sm lg:hidden"
          @click="$emit('toggle-sidebar')"
        >
          <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
            <path d="M4 6h12M4 10h12M4 14h12" />
          </svg>
        </button>

        <div class="min-w-0">
          <h1 class="text-xl font-semibold tracking-tight text-slate-950 md:text-2xl">{{ title }}</h1>
          <p class="mt-1 max-w-3xl text-sm leading-5 text-slate-600">{{ subtitle }}</p>
        </div>
      </div>

      <div class="flex items-center gap-2 xl:justify-end">
        <div class="hidden text-xs font-semibold uppercase tracking-[0.18em] text-slate-500 md:block">
          {{ t('common.lastSync') }}
        </div>
        <div class="inline-flex items-center gap-2 rounded-xl border border-slate-200 bg-slate-50 p-1">
          <span class="hidden px-2 text-xs font-medium text-slate-500 sm:inline">{{ t('common.locale') }}</span>
          <button
            type="button"
            class="rounded-lg px-3 py-1.5 text-sm font-medium transition-colors"
            :class="locale === 'en' ? 'bg-white text-slate-950 shadow-sm' : 'text-slate-500 hover:text-slate-900'"
            @click="$emit('set-locale', 'en')"
          >
            {{ t('common.english') }}
          </button>
          <button
            type="button"
            class="rounded-lg px-3 py-1.5 text-sm font-medium transition-colors"
            :class="locale === 'zh-CN' ? 'bg-white text-slate-950 shadow-sm' : 'text-slate-500 hover:text-slate-900'"
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

import type { LocaleKey } from '../../types/admin';

defineProps<{
  title: string;
  subtitle: string;
  locale: LocaleKey;
}>();

defineEmits<{
  setLocale: [locale: LocaleKey];
  toggleSidebar: [];
}>();

const { t } = useI18n();
</script>
