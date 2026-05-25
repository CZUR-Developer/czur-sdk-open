<template>
  <div class="space-y-6">
    <SectionPanel title-key="pages.config.onlineEnhanceTitle" subtitle-key="pages.config.onlineEnhanceSubtitle">
      <div class="grid gap-4 lg:grid-cols-[minmax(0,1fr)_minmax(280px,0.6fr)]">
        <form class="space-y-4" @submit.prevent="saveConfig">
          <label class="block">
            <span class="text-sm font-semibold text-slate-800">{{ t('pages.config.onlineEnhanceBaseUrl') }}</span>
            <input
              v-model.trim="onlineEnhanceBaseUrl"
              class="mt-2 w-full rounded-2xl border border-slate-200 bg-white px-4 py-3 font-mono text-sm text-slate-900 shadow-sm outline-none transition focus:border-sky-400 focus:ring-4 focus:ring-sky-100"
              placeholder="https://gateway-cn.czur.com"
            />
          </label>
          <label class="block">
            <span class="text-sm font-semibold text-slate-800">{{ t('pages.config.authzBaseUrl') }}</span>
            <input
              v-model.trim="authzBaseUrl"
              class="mt-2 w-full rounded-2xl border border-slate-200 bg-white px-4 py-3 font-mono text-sm text-slate-900 shadow-sm outline-none transition focus:border-sky-400 focus:ring-4 focus:ring-sky-100"
              placeholder="https://gateway-cn.czur.com"
            />
          </label>
          <div class="flex flex-wrap gap-3">
            <button type="submit" class="rounded-2xl bg-slate-950 px-4 py-2.5 text-sm font-semibold text-white shadow-sm transition hover:bg-slate-800">
              {{ saving ? t('pages.config.saving') : t('pages.config.save') }}
            </button>
            <button type="button" class="rounded-2xl border border-slate-200 px-4 py-2.5 text-sm font-semibold text-slate-700 transition hover:bg-slate-50" @click="loadConfig">
              {{ t('pages.config.reload') }}
            </button>
          </div>
          <p v-if="errorMessage" class="rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700">{{ errorMessage }}</p>
          <p v-if="savedMessage" class="rounded-2xl border border-emerald-200 bg-emerald-50 px-4 py-3 text-sm text-emerald-700">{{ savedMessage }}</p>
        </form>

        <dl class="grid gap-3 rounded-2xl bg-slate-50 p-4">
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.onlineEnhanceEffectiveBaseUrl') }}</dt>
            <dd class="mt-1 break-all font-mono text-sm text-slate-900">{{ config?.onlineImageEnhance.effectiveBaseUrl || 'https://gateway-cn.czur.com' }}</dd>
          </div>
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.onlineEnhanceSource') }}</dt>
            <dd class="mt-1 font-mono text-sm text-slate-900">{{ config?.onlineImageEnhance.source || 'default' }}</dd>
          </div>
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.authzEffectiveBaseUrl') }}</dt>
            <dd class="mt-1 break-all font-mono text-sm text-slate-900">{{ config?.authz.effectiveBaseUrl || 'https://gateway-cn.czur.com' }}</dd>
          </div>
          <div>
            <dt class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">{{ t('pages.config.authzSource') }}</dt>
            <dd class="mt-1 font-mono text-sm text-slate-900">{{ config?.authz.source || 'default' }}</dd>
          </div>
        </dl>
      </div>
    </SectionPanel>
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import SectionPanel from '../components/blocks/SectionPanel.vue';
import type { RuntimeConfig } from '../services/admin-api';
import { fetchConfig, saveRuntimeConfig } from '../services/admin-api';

const { t } = useI18n();

const onlineEnhanceBaseUrl = ref('');
const authzBaseUrl = ref('');
const config = ref<RuntimeConfig | null>(null);
const saving = ref(false);
const errorMessage = ref('');
const savedMessage = ref('');

async function loadConfig(): Promise<void> {
  errorMessage.value = '';
  savedMessage.value = '';
  const nextConfig = await fetchConfig();
  config.value = nextConfig;
  onlineEnhanceBaseUrl.value = nextConfig.onlineImageEnhance.baseUrl;
  authzBaseUrl.value = nextConfig.authz.baseUrl;
}

async function saveConfig(): Promise<void> {
  saving.value = true;
  errorMessage.value = '';
  savedMessage.value = '';
  try {
    const nextConfig = await saveRuntimeConfig({
      onlineImageEnhanceBaseUrl: onlineEnhanceBaseUrl.value,
      authzBaseUrl: authzBaseUrl.value,
    });
    config.value = nextConfig;
    onlineEnhanceBaseUrl.value = nextConfig.onlineImageEnhance.baseUrl;
    authzBaseUrl.value = nextConfig.authz.baseUrl;
    savedMessage.value = t('pages.config.saved');
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  } finally {
    saving.value = false;
  }
}

onMounted(() => {
  loadConfig().catch((error) => {
    errorMessage.value = error instanceof Error ? error.message : String(error);
  });
});
</script>
