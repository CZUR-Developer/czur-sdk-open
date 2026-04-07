<template>
  <div class="min-h-screen">
    <div class="flex min-h-screen">
      <div class="sticky top-0 hidden h-screen lg:block">
        <AppSidebar :items="navigationItems" :active-nav-key="activeNavKey" />
      </div>

      <div class="flex min-w-0 flex-1 flex-col">
        <AppHeader
          :title="title"
          :locale="locale"
          @set-locale="setLocale"
          @toggle-sidebar="mobileSidebarOpen = true"
          @open-results="drawerOpen = true"
          @open-api-key="apiKeyDialogOpen = true"
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
      :requests="drawerRequests"
      :events="drawerEvents"
      :errors="drawerErrors"
      @close="drawerOpen = false"
    />

    <ApiKeyDialog
      :open="apiKeyDialogOpen"
      :api-key="authSessionState.apiKey"
      @close="apiKeyDialogOpen = false"
      @save="saveApiKey"
      @clear="clearApiKey"
    />
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute } from 'vue-router';

import { navigationItems } from '../../data/demoSite';
import { authSessionState, clearApiKey as clearStoredApiKey, initializeAuthSession, saveApiKey as persistApiKey } from '../../services/auth-session';
import { runtimeRecordState } from '../../services/runtime-records';
import type { DemoNavKey, LocaleKey } from '../../types/demo';
import ApiKeyDialog from './ApiKeyDialog.vue';
import AppHeader from './AppHeader.vue';
import AppSidebar from './AppSidebar.vue';
import GlobalResultsDrawer from './GlobalResultsDrawer.vue';

const route = useRoute();
const { locale, t } = useI18n();
const mobileSidebarOpen = ref(false);
const drawerOpen = ref(false);
const apiKeyDialogOpen = ref(false);

const activeNavKey = computed(() => (route.meta.navKey as DemoNavKey) ?? 'quick-start');
const title = computed(() => t((route.meta.titleKey as string) || 'pages.quickStart.title'));
const drawerRequests = computed(() => runtimeRecordState.requests);
const drawerEvents = computed(() => runtimeRecordState.events);
const drawerErrors = computed(() => runtimeRecordState.errors);

function setLocale(nextLocale: LocaleKey) {
  locale.value = nextLocale;
}

async function saveApiKey(value: string) {
  persistApiKey(value);
  apiKeyDialogOpen.value = false;
  await initializeAuthSession();
}

function clearApiKey() {
  clearStoredApiKey();
  apiKeyDialogOpen.value = false;
}

onMounted(async () => {
  await initializeAuthSession();
});

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
