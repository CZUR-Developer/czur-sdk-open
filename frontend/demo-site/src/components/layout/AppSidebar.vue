<template>
  <aside
    class="flex h-full w-76 flex-col border-r border-slate-900/70 bg-slate-950/95 text-slate-100"
    :class="mobile ? 'shadow-2xl' : ''"
  >
    <div class="flex min-h-[92px] items-center justify-between border-b border-slate-800 px-4 py-3">
      <div>
        <div class="text-[11px] font-semibold uppercase tracking-[0.24em] text-cyan-300">{{ t('app.brand') }}</div>
        <div class="mt-2 text-lg font-semibold tracking-tight text-white">{{ t('app.tagline') }}</div>
        <p class="mt-2 max-w-[220px] text-sm leading-5 text-slate-400">{{ t('app.description') }}</p>
      </div>

      <button
        v-if="mobile"
        type="button"
        class="inline-flex size-9 items-center justify-center rounded-xl text-slate-400 hover:bg-slate-900 hover:text-white"
        @click="$emit('close')"
      >
        <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
          <path d="M5 5l10 10M15 5 5 15" />
        </svg>
      </button>
    </div>

    <div class="flex flex-1 flex-col px-3 py-4">
      <div class="mb-3 px-3 text-[11px] font-semibold uppercase tracking-[0.22em] text-slate-500">
        {{ t('common.menu') }}
      </div>

      <nav class="flex flex-col gap-1">
        <RouterLink
          v-for="item in items"
          :key="item.key"
          :to="item.to"
          class="group flex items-center gap-3 rounded-2xl px-3 py-2.5 text-sm font-medium transition-colors"
          :class="
            item.key === activeNavKey
              ? 'bg-slate-800 text-white'
              : 'text-slate-300 hover:bg-slate-900 hover:text-white'
          "
          @click="handleItemClick"
        >
          <span
            class="flex size-10 shrink-0 items-center justify-center rounded-xl border transition-colors"
            :class="
              item.key === activeNavKey
                ? 'border-slate-700 bg-slate-700/80 text-cyan-300'
                : 'border-slate-800 bg-slate-900 text-slate-400 group-hover:border-slate-700 group-hover:text-slate-200'
            "
          >
            <NavigationIcon :nav-key="item.key" />
          </span>
          <span class="truncate">{{ t(item.labelKey) }}</span>
        </RouterLink>
      </nav>
    </div>
  </aside>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { RouterLink } from 'vue-router';

import type { DemoNavKey, NavigationItem } from '../../types/demo';
import NavigationIcon from './NavigationIcon.vue';

const props = withDefaults(
  defineProps<{
    items: NavigationItem[];
    activeNavKey: DemoNavKey;
    mobile?: boolean;
  }>(),
  {
    mobile: false,
  },
);

const emit = defineEmits<{
  close: [];
}>();

const { t } = useI18n();

function handleItemClick() {
  if (props.mobile) {
    emit('close');
  }
}
</script>
