<template>
  <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" class="size-5">
    <path v-if="navKey === 'quick-start'" d="M4 5h12M4 10h12M4 15h8" />
    <path v-else-if="navKey === 'connection-auth'" d="M6 8V6a4 4 0 1 1 8 0v2M5 8h10v7H5z" />
    <path v-else-if="navKey === 'device-video'" d="M4 6h9v8H4zM13 8l3-2v8l-3-2" />
    <path v-else-if="navKey === 'capture-acquisition'" d="M6 6h8l1 2h1v6H4V8h1z" />
    <path v-else-if="navKey === 'image-processing'" d="M4 5h12v10H4zM7 11l2-2 2 2 2-3 1 1" />
    <path v-else-if="navKey === 'image-enhancement'" d="M4 14c4-7 8-7 12 0M5 5h4v4H5zM11 5h4v4h-4zM8 9l4 2" />
    <path v-else-if="navKey === 'ocr-recognition'" d="M5 5h10v10H5zM7 8h6M7 11h4" />
    <path v-else-if="navKey === 'file-conversion'" d="M6 4h6l3 3v9H6zM12 4v3h3" />
    <path v-else-if="navKey === 'sane-scanning'" d="M5 5h10v4H5zM7 13h6M6 9h8v6H6z" />
    <path v-else d="M5 5h10v3H5zM5 9h10v6H5zM8 12h4" />
  </svg>
</template>

<script setup lang="ts">
import type { DemoNavKey } from '../../types/demo';

defineProps<{
  navKey: DemoNavKey;
}>();
</script>
