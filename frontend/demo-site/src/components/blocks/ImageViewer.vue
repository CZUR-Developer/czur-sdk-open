<template>
  <div
    v-if="imageViewerState.open"
    class="fixed inset-0 z-[80] bg-slate-950/88 text-white"
    @click.self="closeImageViewer"
  >
    <div class="flex h-full min-h-0 flex-col">
      <header class="flex flex-wrap items-center justify-between gap-3 border-b border-white/10 bg-slate-950/70 px-4 py-3 backdrop-blur md:px-6">
        <div class="min-w-0">
          <p class="truncate text-sm font-semibold text-white">{{ imageViewerState.title || t('common.imageViewer') }}</p>
          <p v-if="imageViewerState.subtitle" class="mt-0.5 truncate text-xs text-slate-300">{{ imageViewerState.subtitle }}</p>
        </div>

        <div class="flex items-center gap-1.5">
          <button type="button" class="viewer-button" :title="t('actions.zoomOut')" :aria-label="t('actions.zoomOut')" @click="zoomBy(1 / zoomStep)">
            <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
              <path d="M5 10h10" />
            </svg>
          </button>
          <span class="min-w-14 text-center font-mono text-xs text-slate-300">{{ zoomLabel }}</span>
          <button type="button" class="viewer-button" :title="t('actions.zoomIn')" :aria-label="t('actions.zoomIn')" @click="zoomBy(zoomStep)">
            <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
              <path d="M10 5v10M5 10h10" />
            </svg>
          </button>
          <button type="button" class="viewer-button" :title="t('actions.rotateLeft')" :aria-label="t('actions.rotateLeft')" @click="rotateBy(-90)">
            <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
              <path d="M7 4H4v3" />
              <path d="M4.5 7A6 6 0 1 0 10 4" />
            </svg>
          </button>
          <button type="button" class="viewer-button" :title="t('actions.rotateRight')" :aria-label="t('actions.rotateRight')" @click="rotateBy(90)">
            <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
              <path d="M13 4h3v3" />
              <path d="M15.5 7A6 6 0 1 1 10 4" />
            </svg>
          </button>
          <button type="button" class="viewer-button" :title="t('actions.resetView')" :aria-label="t('actions.resetView')" @click="resetTransform">
            <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
              <path d="M5 5h10v10H5z" />
              <path d="M8 8h4v4H8z" />
            </svg>
          </button>
          <button type="button" class="viewer-button" :title="t('actions.close')" :aria-label="t('actions.close')" @click="closeImageViewer">
            <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
              <path d="M5 5l10 10M15 5 5 15" />
            </svg>
          </button>
        </div>
      </header>

      <div
        ref="stageRef"
        class="relative min-h-0 flex-1 cursor-grab overflow-hidden active:cursor-grabbing"
        @wheel.prevent="handleWheel"
        @pointerdown="handlePointerDown"
        @pointermove="handlePointerMove"
        @pointerup="handlePointerUp"
        @pointercancel="handlePointerUp"
      >
        <img
          :src="imageViewerState.src"
          :alt="imageViewerState.alt"
          class="absolute left-1/2 top-1/2 max-h-[82vh] max-w-[92vw] select-none object-contain"
          draggable="false"
          :style="imageStyle"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, reactive, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import { closeImageViewer, imageViewerState } from '../../services/image-viewer';

const { t } = useI18n();

const minScale = 0.25;
const maxScale = 6;
const zoomStep = 1.2;
const transform = reactive({
  scale: 1,
  rotation: 0,
  offsetX: 0,
  offsetY: 0,
});

let dragging = false;
let dragPointerId = 0;
let dragStartX = 0;
let dragStartY = 0;
let dragOriginX = 0;
let dragOriginY = 0;

const zoomLabel = computed(() => `${Math.round(transform.scale * 100)}%`);
const imageStyle = computed(() => ({
  transform: `translate(calc(-50% + ${transform.offsetX}px), calc(-50% + ${transform.offsetY}px)) rotate(${transform.rotation}deg) scale(${transform.scale})`,
}));

watch(
  () => imageViewerState.src,
  () => resetTransform(),
);

watch(
  () => imageViewerState.open,
  (open) => {
    if (open) {
      resetTransform();
      window.addEventListener('keydown', handleKeydown);
    } else {
      window.removeEventListener('keydown', handleKeydown);
    }
  },
);

onBeforeUnmount(() => {
  window.removeEventListener('keydown', handleKeydown);
});

function resetTransform(): void {
  transform.scale = 1;
  transform.rotation = 0;
  transform.offsetX = 0;
  transform.offsetY = 0;
}

function zoomBy(factor: number): void {
  transform.scale = clamp(transform.scale * factor, minScale, maxScale);
}

function rotateBy(degrees: number): void {
  transform.rotation = (transform.rotation + degrees) % 360;
}

function handleWheel(event: WheelEvent): void {
  zoomBy(event.deltaY < 0 ? zoomStep : 1 / zoomStep);
}

function handlePointerDown(event: PointerEvent): void {
  dragging = true;
  dragPointerId = event.pointerId;
  dragStartX = event.clientX;
  dragStartY = event.clientY;
  dragOriginX = transform.offsetX;
  dragOriginY = transform.offsetY;
  (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
}

function handlePointerMove(event: PointerEvent): void {
  if (!dragging || event.pointerId !== dragPointerId) {
    return;
  }
  transform.offsetX = dragOriginX + event.clientX - dragStartX;
  transform.offsetY = dragOriginY + event.clientY - dragStartY;
}

function handlePointerUp(event: PointerEvent): void {
  if (event.pointerId === dragPointerId) {
    dragging = false;
  }
}

function handleKeydown(event: KeyboardEvent): void {
  if (event.key === 'Escape') {
    closeImageViewer();
  }
}

function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}
</script>

<style scoped>
.viewer-button {
  display: inline-flex;
  width: 2.5rem;
  height: 2.5rem;
  align-items: center;
  justify-content: center;
  border-radius: 0.75rem;
  border: 1px solid rgb(255 255 255 / 0.14);
  background: rgb(255 255 255 / 0.08);
  color: rgb(226 232 240);
  transition: border-color 0.15s ease, background-color 0.15s ease, color 0.15s ease;
}

.viewer-button:hover {
  border-color: rgb(103 232 249 / 0.6);
  background: rgb(8 145 178 / 0.28);
  color: white;
}
</style>
