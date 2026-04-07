<template>
  <div class="min-h-screen bg-transparent">
    <div class="flex min-h-screen">
      <div class="sticky top-0 hidden h-screen lg:block">
        <AppSidebar
          :items="navigationItems"
          :bottom-items="supportNavigationItems"
          :active-nav-key="activeNavKey"
        />
      </div>

      <div class="flex min-w-0 flex-1 flex-col">
        <AppHeader
          :title="title"
          :subtitle="subtitle"
          :locale="locale"
          @set-locale="setLocale"
          @toggle-sidebar="mobileSidebarOpen = true"
        />

        <main class="min-w-0 flex-1 px-4 py-6 md:px-6 xl:px-8">
          <slot />
        </main>
      </div>
    </div>

    <div
      v-if="mobileSidebarOpen"
      class="fixed inset-0 z-40 bg-slate-950/45 lg:hidden"
      @click.self="mobileSidebarOpen = false"
    >
      <div class="h-full">
        <AppSidebar
          mobile
          :items="navigationItems"
          :bottom-items="supportNavigationItems"
          :active-nav-key="activeNavKey"
          @close="mobileSidebarOpen = false"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute } from 'vue-router';

import { navigationItems, supportNavigationItems } from '../../data/adminApp';
import type { LocaleKey, NavKey } from '../../types/admin';
import AppHeader from './AppHeader.vue';
import AppSidebar from './AppSidebar.vue';

const route = useRoute();
const { locale, t } = useI18n();
const mobileSidebarOpen = ref(false);

const activeNavKey = computed(() => (route.meta.navKey as NavKey) ?? 'dashboard');
const title = computed(() => t((route.meta.titleKey as string) || 'pages.dashboard.title'));
const subtitle = computed(() => t((route.meta.subtitleKey as string) || 'pages.dashboard.subtitle'));

function setLocale(nextLocale: LocaleKey) {
  locale.value = nextLocale;
}
</script>
