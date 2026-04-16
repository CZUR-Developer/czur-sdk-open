<template>
  <div
    v-if="open"
    class="fixed inset-0 z-50 bg-slate-950/38"
    @click.self="$emit('close')"
  >
    <div class="flex min-h-full items-center justify-center p-4">
      <section class="w-full max-w-md rounded-[28px] border border-slate-200 bg-white p-6 shadow-[var(--demo-shadow)]">
        <div class="flex items-start justify-between gap-4">
          <div>
            <p class="text-xs font-semibold uppercase tracking-[0.22em] text-cyan-700">{{ t('labels.apiKey') }}</p>
            <h2 class="mt-2 text-2xl font-semibold tracking-tight text-slate-950">{{ t('actions.setApiKey') }}</h2>
            <p class="mt-3 text-sm leading-6 text-slate-600">
              {{ description }}
            </p>
          </div>

          <button
            type="button"
            class="inline-flex size-10 items-center justify-center rounded-xl border border-slate-200 text-slate-500 hover:text-slate-900"
            @click="$emit('close')"
          >
            <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
              <path d="M5 5l10 10M15 5 5 15" />
            </svg>
          </button>
        </div>

        <label class="mt-5 block">
          <span class="text-xs font-semibold uppercase tracking-[0.18em] text-slate-500">{{ t('labels.apiKey') }}</span>
          <input
            v-model="draftValue"
            type="password"
            autocomplete="off"
            spellcheck="false"
            class="mt-2 w-full rounded-2xl border border-slate-200 bg-slate-50 px-4 py-3 text-sm text-slate-900 outline-none transition focus:border-cyan-400 focus:bg-white"
            placeholder="demo-token-..."
          />
        </label>

        <div class="mt-6 flex items-center justify-between gap-3">
          <button
            type="button"
            class="inline-flex items-center rounded-2xl border border-slate-200 px-4 py-2 text-sm font-medium text-slate-600 transition hover:border-slate-300 hover:text-slate-900"
            @click="handleClear"
          >
            {{ t('actions.clear') }}
          </button>

          <button
            type="button"
            class="inline-flex items-center rounded-2xl bg-slate-950 px-4 py-2 text-sm font-medium text-white transition hover:bg-slate-800"
            @click="handleSave"
          >
            {{ t('actions.save') }}
          </button>
        </div>
      </section>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

const props = defineProps<{
  open: boolean;
  apiKey: string;
}>();

const emit = defineEmits<{
  close: [];
  save: [value: string];
  clear: [];
}>();

const { locale, t } = useI18n();
const draftValue = ref(props.apiKey);

const description = computed(() =>
  locale.value === 'zh-CN'
    ? '该值仅保存在当前浏览器本地，用于演示站建立运行时授权会话。'
    : 'This value is only stored locally in the browser and used to establish the runtime auth session.',
);

watch(
  () => props.open,
  (open) => {
    if (open) {
      draftValue.value = props.apiKey;
    }
  },
);

watch(
  () => props.apiKey,
  (value) => {
    if (!props.open) {
      draftValue.value = value;
    }
  },
);

function handleSave() {
  emit('save', draftValue.value.trim());
}

function handleClear() {
  draftValue.value = '';
  emit('clear');
}
</script>
