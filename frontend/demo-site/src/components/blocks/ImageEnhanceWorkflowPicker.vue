<template>
  <div class="rounded-2xl border border-slate-200 bg-white/80 p-4">
    <div class="flex flex-wrap items-center justify-between gap-3">
      <div>
        <p class="text-sm font-semibold text-slate-900">{{ title }}</p>
        <p class="mt-1 text-xs text-slate-500">{{ selectedWorkflow ? selectedWorkflow.name : emptyLabel }}</p>
      </div>
      <button type="button" class="secondary-button" :disabled="loading" @click="refresh">
        {{ loading ? loadingLabel : refreshLabel }}
      </button>
    </div>
    <div class="mt-3 grid gap-3 sm:grid-cols-[minmax(0,1fr)_auto]">
      <select :value="modelValue" class="field-input" @change="handleChange">
        <option value="">{{ noneLabel }}</option>
        <option v-for="workflow in workflows" :key="workflow.workflow_id" :value="workflow.workflow_id">
          {{ workflow.name }}
        </option>
      </select>
      <button type="button" class="secondary-button" :disabled="!modelValue" @click="clearSelection">
        {{ clearLabel }}
      </button>
    </div>
    <p v-if="error" class="mt-3 rounded-xl border border-rose-200 bg-rose-50 px-3 py-2 text-xs text-rose-600">{{ error }}</p>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';

import { listEnhanceWorkflows, type EnhanceWorkflow } from '../../services/image-enhance-workflows';

const props = withDefaults(
  defineProps<{
    modelValue: string;
    title: string;
    emptyLabel: string;
    noneLabel: string;
    clearLabel: string;
    refreshLabel: string;
    loadingLabel: string;
  }>(),
  {},
);

const emit = defineEmits<{
  'update:modelValue': [value: string];
  selected: [workflow: EnhanceWorkflow | null];
}>();

const workflows = ref<EnhanceWorkflow[]>([]);
const loading = ref(false);
const error = ref('');

const selectedWorkflow = computed(() => workflows.value.find((workflow) => workflow.workflow_id === props.modelValue) ?? null);

onMounted(() => {
  void refresh();
});

async function refresh(): Promise<void> {
  loading.value = true;
  error.value = '';
  try {
    workflows.value = await listEnhanceWorkflows();
    emit('selected', selectedWorkflow.value);
  } catch (err) {
    error.value = err instanceof Error ? err.message : 'failed';
  } finally {
    loading.value = false;
  }
}

function handleChange(event: Event): void {
  const value = event.target instanceof HTMLSelectElement ? event.target.value : '';
  emit('update:modelValue', value);
  emit('selected', workflows.value.find((workflow) => workflow.workflow_id === value) ?? null);
}

function clearSelection(): void {
  emit('update:modelValue', '');
  emit('selected', null);
}
</script>
