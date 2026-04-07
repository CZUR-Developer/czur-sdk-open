<template>
  <svg
    viewBox="0 0 20 20"
    fill="none"
    stroke="currentColor"
    stroke-linecap="round"
    stroke-linejoin="round"
    class="size-5"
    aria-hidden="true"
  >
    <template v-if="navKey === 'dashboard'">
      <rect x="3" y="3" width="6" height="6" rx="1.2" stroke-width="1.7" />
      <rect x="11" y="3" width="6" height="10" rx="1.2" stroke-width="1.7" />
      <rect x="3" y="11" width="6" height="6" rx="1.2" stroke-width="1.7" />
      <rect x="11" y="15" width="6" height="2" rx="1" stroke-width="1.7" />
    </template>
    <template v-else-if="navKey === 'websocket'">
      <path d="M6 6.5a6 6 0 0 1 8 0" stroke-width="1.7" />
      <path d="M4 9.5a9 9 0 0 1 12 0" stroke-width="1.7" />
      <path d="M8.5 12.5a3.5 3.5 0 0 1 3 0" stroke-width="1.7" />
      <circle cx="10" cy="15.5" r="1.2" fill="currentColor" stroke="none" />
    </template>
    <template v-else-if="navKey === 'system'">
      <rect x="4" y="4" width="12" height="12" rx="2" stroke-width="1.7" />
      <path d="M7 8h6M7 12h6" stroke-width="1.7" />
      <path d="M6 2v2M10 2v2M14 2v2M6 16v2M10 16v2M14 16v2M2 6h2M2 10h2M2 14h2M16 6h2M16 10h2M16 14h2" stroke-width="1.4" />
    </template>
    <template v-else-if="navKey === 'auth'">
      <path d="M10 3l5 2v4c0 3.6-2.2 5.9-5 7-2.8-1.1-5-3.4-5-7V5l5-2Z" stroke-width="1.7" />
      <path d="M8.4 9.7l1.1 1.2 2.3-2.7" stroke-width="1.7" />
    </template>
    <template v-else-if="navKey === 'log'">
      <path d="M5 4.5h10a1.5 1.5 0 0 1 1.5 1.5v8A1.5 1.5 0 0 1 15 15.5H5A1.5 1.5 0 0 1 3.5 14V6A1.5 1.5 0 0 1 5 4.5Z" stroke-width="1.7" />
      <path d="M6.5 8h7M6.5 11h4.5" stroke-width="1.7" />
    </template>
    <template v-else-if="navKey === 'support'">
      <path d="M10 15.5v-1.8" stroke-width="1.7" />
      <path d="M6.3 12.4A5.5 5.5 0 1 1 15.5 8" stroke-width="1.7" />
      <path d="M5.1 12.3H4.6A1.6 1.6 0 0 1 3 10.7V9.8a1.6 1.6 0 0 1 1.6-1.6h.8" stroke-width="1.7" />
      <path d="M14.9 8.2h.5A1.6 1.6 0 0 1 17 9.8v.9a1.6 1.6 0 0 1-1.6 1.6h-.8" stroke-width="1.7" />
      <path d="M8.8 15.8h2.4" stroke-width="1.7" />
    </template>
    <template v-else-if="navKey === 'documention'">
      <path d="M5.5 4.5h7a2 2 0 0 1 2 2v9h-7a2 2 0 0 0-2 2Z" stroke-width="1.7" />
      <path d="M14.5 15.5h-7a2 2 0 0 0-2 2V6.5a2 2 0 0 1 2-2h7Z" stroke-width="1.7" />
      <path d="M8 8h4.5M8 10.8h4.5M8 13.6h3" stroke-width="1.7" />
    </template>
    <template v-else>
      <path d="M5 5h10v10H5z" stroke-width="1.7" />
      <path d="M7.5 8h5M7.5 10.5h5M7.5 13h3" stroke-width="1.7" />
    </template>
  </svg>
</template>

<script setup lang="ts">
import type { NavKey } from '../../types/admin';

defineProps<{
  navKey: NavKey;
}>();
</script>
