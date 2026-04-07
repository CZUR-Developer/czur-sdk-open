<template>
  <section class="rounded-[32px] border border-[color:var(--demo-panel-border)] bg-[var(--demo-panel)] p-6 shadow-[var(--demo-shadow)] xl:p-7">
    <div class="flex flex-col gap-4 md:flex-row md:items-start md:justify-between">
      <div class="max-w-3xl">
        <p v-if="eyebrow" class="text-xs font-semibold uppercase tracking-[0.22em] text-cyan-700">{{ eyebrow }}</p>
        <h2 class="mt-2 text-2xl font-semibold tracking-tight text-slate-950">{{ title }}</h2>
        <p v-if="description" class="mt-3 text-sm leading-6 text-slate-600">{{ description }}</p>
      </div>
      <div v-if="$slots.actions" class="shrink-0">
        <slot name="actions" />
      </div>
    </div>

    <div class="mt-6">
      <slot />
    </div>
  </section>
</template>

<script setup lang="ts">
withDefaults(
  defineProps<{
    eyebrow?: string;
    title: string;
    description?: string;
  }>(),
  {
    eyebrow: undefined,
    description: undefined,
  },
);
</script>
