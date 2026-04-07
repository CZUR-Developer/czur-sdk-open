<template>
  <div class="min-h-screen">
    <div class="flex min-h-screen">
      <div class="sticky top-0 hidden h-screen lg:block">
        <AppSidebar :items="navigationItems" :active-nav-key="activeNavKey" />
      </div>

      <div class="flex min-w-0 flex-1 flex-col">
        <AppHeader
          :title="title"
          :subtitle="subtitle"
          :locale="locale"
          :status-items="shellStatusItems"
          @set-locale="setLocale"
          @toggle-sidebar="mobileSidebarOpen = true"
          @open-results="drawerOpen = true"
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
      <AppSidebar
        mobile
        :items="navigationItems"
        :active-nav-key="activeNavKey"
        @close="mobileSidebarOpen = false"
      />
    </div>

    <GlobalResultsDrawer
      :open="drawerOpen"
      :requests="globalRequestHistory"
      :events="globalEvents"
      :errors="globalErrors"
      @close="drawerOpen = false"
    />
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute } from 'vue-router';

import { globalErrors, globalEvents, globalRequestHistory, navigationItems, shellStatusItems } from '../../data/demoSite';
import type { DemoNavKey, LocaleKey } from '../../types/demo';
import AppHeader from './AppHeader.vue';
import AppSidebar from './AppSidebar.vue';
import GlobalResultsDrawer from './GlobalResultsDrawer.vue';

const route = useRoute();
const { locale, t } = useI18n();
const mobileSidebarOpen = ref(false);
const drawerOpen = ref(false);

const activeNavKey = computed(() => (route.meta.navKey as DemoNavKey) ?? 'quick-start');
const title = computed(() => t((route.meta.titleKey as string) || 'pages.quickStart.title'));
const subtitle = computed(() => t((route.meta.subtitleKey as string) || 'pages.quickStart.subtitle'));

function setLocale(nextLocale: LocaleKey) {
  locale.value = nextLocale;
}

watch(
  locale,
  (value) => {
    try {
      localStorage.setItem('sdk-demo-site-locale', value);
    } catch {
      // Ignore storage write failures in restricted environments.
    }
  },
  { immediate: true },
);
</script>
