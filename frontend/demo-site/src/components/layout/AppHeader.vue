<template>
  <header class="border-b border-slate-200/80 bg-white/95 px-4 py-3 md:px-6">
    <div class="flex flex-col gap-4">
      <div class="flex flex-col gap-3 xl:flex-row xl:items-start xl:justify-between">
        <div class="flex min-w-0 items-start gap-3">
          <button
            type="button"
            class="inline-flex size-10 shrink-0 items-center justify-center rounded-xl border border-slate-200 text-slate-600 shadow-sm lg:hidden"
            @click="$emit('toggle-sidebar')"
          >
            <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
              <path d="M4 6h12M4 10h12M4 14h12" />
            </svg>
          </button>

          <div class="min-w-0">
            <p class="text-xs font-semibold uppercase tracking-[0.24em] text-cyan-700">{{ t('common.environment') }}</p>
            <h1 class="mt-2 text-2xl font-semibold tracking-tight text-slate-950 md:text-3xl">{{ title }}</h1>
            <p class="mt-2 max-w-4xl text-sm leading-6 text-slate-600">{{ subtitle }}</p>
          </div>
        </div>

        <div class="flex flex-wrap items-center gap-2 xl:justify-end">
          <button
            type="button"
            class="inline-flex items-center gap-2 rounded-2xl border border-slate-200 bg-slate-950 px-4 py-2 text-sm font-medium text-white shadow-sm"
            @click="$emit('open-results')"
          >
            <span>{{ t('actions.openDrawer') }}</span>
          </button>

          <div class="inline-flex items-center gap-1 rounded-2xl border border-slate-200 bg-white/90 p-1">
            <span class="hidden px-2 text-xs font-medium text-slate-500 sm:inline">{{ t('common.locale') }}</span>
            <button
              type="button"
              class="rounded-xl px-3 py-1.5 text-sm font-medium transition-colors"
              :class="locale === 'en' ? 'bg-slate-950 text-white shadow-sm' : 'text-slate-500 hover:text-slate-900'"
              @click="$emit('set-locale', 'en')"
            >
              {{ t('common.english') }}
            </button>
            <button
              type="button"
              class="rounded-xl px-3 py-1.5 text-sm font-medium transition-colors"
              :class="locale === 'zh-CN' ? 'bg-slate-950 text-white shadow-sm' : 'text-slate-500 hover:text-slate-900'"
              @click="$emit('set-locale', 'zh-CN')"
            >
              {{ t('common.chinese') }}
            </button>
          </div>
        </div>
      </div>

      <div class="flex flex-wrap gap-2">
        <StatusPill
          v-for="item in statusItems"
          :key="item.id"
          :label="`${t(item.labelKey)} · ${item.value}`"
          :tone="item.tone"
        />
      </div>
    </div>
  </header>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';

import type { LocaleKey, ShellStatusItem } from '../../types/demo';
import StatusPill from '../cards/StatusPill.vue';

defineProps<{
  title: string;
  subtitle: string;
  locale: LocaleKey;
  statusItems: ShellStatusItem[];
}>();

defineEmits<{
  'set-locale': [locale: LocaleKey];
  'toggle-sidebar': [];
  'open-results': [];
}>();

const { t } = useI18n();
</script>
