<template>
  <article class="rounded-[28px] border border-slate-200/80 bg-slate-950 p-5 shadow-[var(--demo-shadow)]">
    <div class="flex items-center justify-between gap-4">
      <div>
        <h3 class="text-sm font-semibold uppercase tracking-[0.18em] text-slate-100">{{ title }}</h3>
        <p v-if="caption" class="mt-2 text-xs leading-5 text-slate-400">{{ caption }}</p>
      </div>
      <span class="rounded-full border border-slate-700 bg-slate-900 px-2.5 py-1 text-[11px] font-semibold uppercase tracking-[0.16em] text-slate-300">
        json
      </span>
    </div>

    <pre class="mt-5 overflow-x-auto text-[13px] leading-6 text-slate-200"><code>{{ payload }}</code></pre>
  </article>
</template>

<script setup lang="ts">
withDefaults(
  defineProps<{
    title: string;
    payload: string;
    caption?: string;
  }>(),
  {
    caption: undefined,
  },
);
</script>
