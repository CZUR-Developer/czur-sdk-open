<template>
  <article class="overflow-hidden rounded-[32px] border border-white/70 bg-white/90 shadow-[var(--demo-shadow)]">
    <div class="relative bg-slate-950" :class="stageSizeClass">
      <canvas
        v-if="variant === 'video'"
        ref="videoCanvas"
        class="absolute inset-0 h-full w-full object-contain"
      />
      <img
        v-else-if="imageUrl"
        :src="imageUrl"
        alt=""
        class="absolute inset-0 h-full w-full object-contain"
      />
      <slot name="video-overlay" />
      <div v-if="showPlaceholder" class="absolute inset-0 opacity-70" :class="variantClass" />
      <div v-if="showOverlay" class="absolute inset-0 bg-[radial-gradient(circle_at_top,_rgba(255,255,255,0.12),_transparent_38%)]" />
      <div v-if="showOverlay" class="absolute inset-x-0 top-0 h-px bg-cyan-300/50" />

      <div class="relative flex flex-col justify-between p-6" :class="stageSizeClass">
        <div class="flex flex-wrap gap-2">
          <StatusPill v-if="badgeLabel" :label="badgeLabel" :tone="badgeTone" />
        </div>

        <div v-if="showTextOverlay">
          <p class="text-xs font-semibold uppercase tracking-[0.22em] text-slate-400">{{ eyebrow }}</p>
          <h3 class="mt-3 text-2xl font-semibold tracking-tight text-white">{{ title }}</h3>
          <p class="mt-3 max-w-xl text-sm leading-6 text-slate-300">{{ description }}</p>
        </div>
      </div>
    </div>

    <div v-if="metrics.length > 0" class="grid gap-3 p-5 sm:grid-cols-2">
      <div
        v-for="metric in metrics"
        :key="metric.label"
        class="rounded-2xl border border-slate-200/80 bg-slate-50/80 p-4"
      >
        <p class="text-xs font-semibold uppercase tracking-[0.2em] text-slate-500">{{ metric.label }}</p>
        <p class="mt-2 text-sm font-medium text-slate-900" :class="metric.monospace ? 'font-mono text-[13px]' : ''">
          {{ metric.value }}
        </p>
      </div>
    </div>
  </article>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue';

import type { PreviewMetric, Tone } from '../../types/demo';
import StatusPill from '../cards/StatusPill.vue';

const props = withDefaults(
  defineProps<{
    eyebrow: string;
    title: string;
    description: string;
    metrics: PreviewMetric[];
    imageUrl?: string;
    badgeLabel?: string;
    badgeTone?: Tone;
    variant?: 'image' | 'video';
    stageSize?: 'normal' | 'large';
    showTextOverlay?: boolean;
  }>(),
  {
    badgeLabel: undefined,
    imageUrl: '',
    badgeTone: 'neutral',
    variant: 'image',
    stageSize: 'normal',
    showTextOverlay: true,
  },
);

const emit = defineEmits<{
  'video-canvas': [canvas: HTMLCanvasElement | null];
}>();

const videoCanvas = ref<HTMLCanvasElement | null>(null);

const variantClass = computed(() =>
  props.variant === 'video'
    ? 'bg-[linear-gradient(135deg,_rgba(14,116,144,0.72),_rgba(15,23,42,0.95))]'
    : 'bg-[linear-gradient(135deg,_rgba(8,145,178,0.62),_rgba(15,23,42,0.92))]',
);

const showPlaceholder = computed(() => props.variant !== 'video' && !props.imageUrl);
const showOverlay = computed(() => props.variant !== 'video');
const stageSizeClass = computed(() => (props.stageSize === 'large' ? 'min-h-[460px]' : 'min-h-[300px]'));

watch(
  videoCanvas,
  (canvas) => {
    if (props.variant === 'video') {
      emit('video-canvas', canvas);
    }
  },
  { immediate: true },
);

watch(
  () => props.variant,
  (variant) => {
    emit('video-canvas', variant === 'video' ? videoCanvas.value : null);
  },
);

onBeforeUnmount(() => {
  emit('video-canvas', null);
});
</script>
