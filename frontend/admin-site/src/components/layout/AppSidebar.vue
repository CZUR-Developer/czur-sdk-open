<template>
  <aside
    class="flex h-full w-72 flex-col border-r border-slate-800/80 bg-slate-950 text-slate-100"
    :class="mobile ? 'shadow-2xl' : ''"
  >
    <div class="flex min-h-[84px] items-center justify-between border-b border-slate-800 px-4 py-2.5">
      <div class="min-w-0">
        <div class="text-lg font-semibold tracking-tight text-white">{{ t('app.tagline') }}</div>
      </div>

      <button
        v-if="mobile"
        type="button"
        class="inline-flex size-9 items-center justify-center rounded-md text-slate-400 hover:bg-slate-900 hover:text-white"
        @click="$emit('close')"
      >
        <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
          <path d="M5 5l10 10M15 5 5 15" />
        </svg>
      </button>
    </div>

    <div class="flex flex-1 flex-col px-3 py-3">
      <div class="mb-3 px-3 text-[11px] font-semibold uppercase tracking-[0.22em] text-slate-500">
        {{ t('common.menu') }}
      </div>

      <nav class="flex flex-col gap-1">
        <RouterLink
          v-for="item in items"
          :key="item.key"
          :to="item.to"
          class="group flex min-h-11 items-center gap-3 rounded-lg px-3 py-2 text-sm font-medium transition-colors"
          :class="
            item.key === activeNavKey
              ? 'bg-slate-800 text-white'
              : 'text-slate-300 hover:bg-slate-900 hover:text-white'
          "
          @click="handleItemClick"
        >
          <span
            class="flex size-8 shrink-0 items-center justify-center rounded-md border transition-colors"
            :class="
              item.key === activeNavKey
                ? 'border-slate-700 bg-slate-700/70 text-sky-300'
                : 'border-slate-800 bg-slate-900 text-slate-400 group-hover:border-slate-700 group-hover:text-slate-200'
            "
          >
            <NavigationIcon :nav-key="item.key" />
          </span>
          <span class="truncate">{{ t(item.labelKey) }}</span>
        </RouterLink>
      </nav>

      <div class="mt-auto border-t border-slate-800 pt-4">
        <nav class="flex flex-col gap-1">
          <component
            v-for="item in bottomItems"
            :key="item.key"
            :is="item.external ? 'a' : RouterLink"
            :href="item.external ? item.href : undefined"
            :to="item.external ? undefined : item.to"
            :target="item.external ? '_blank' : undefined"
            :rel="item.external ? 'noreferrer noopener' : undefined"
            class="group flex min-h-11 items-center gap-3 rounded-lg px-3 py-2 text-sm font-medium transition-colors"
            :class="
              item.key === activeNavKey
                ? 'bg-slate-800 text-white'
                : 'text-slate-300 hover:bg-slate-900 hover:text-white'
            "
            @click="handleItemClick"
          >
            <span
              class="flex size-8 shrink-0 items-center justify-center rounded-md border transition-colors"
              :class="
                item.key === activeNavKey
                  ? 'border-slate-700 bg-slate-700/70 text-sky-300'
                  : 'border-slate-800 bg-slate-900 text-slate-400 group-hover:border-slate-700 group-hover:text-slate-200'
              "
            >
              <NavigationIcon :nav-key="item.key" />
            </span>
            <span class="truncate">{{ t(item.labelKey) }}</span>
          </component>
        </nav>
      </div>
    </div>

  </aside>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { RouterLink } from 'vue-router';

import type { NavKey, NavigationItem } from '../../types/admin';
import NavigationIcon from './NavigationIcon.vue';

const props = withDefaults(
  defineProps<{
    items: NavigationItem[];
    bottomItems: NavigationItem[];
    activeNavKey: NavKey;
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
