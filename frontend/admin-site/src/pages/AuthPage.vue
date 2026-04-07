<template>
  <PlaceholderModulePage
    surface-title-key="sections.authSurface"
    surface-subtitle-key="sections.authSurfaceHint"
    timeline-title-key="sections.authTimeline"
    timeline-subtitle-key="sections.authTimelineHint"
    :metrics="authMetrics"
    :cards="authCards"
    :timeline="authTimeline"
  />
</template>

<script setup lang="ts">
import PlaceholderModulePage from '../components/blocks/PlaceholderModulePage.vue';
import { authCards, authMetrics, authTimeline } from '../data/adminApp';
</script>
