<template>
  <PlaceholderModulePage
    surface-title-key="sections.logSurface"
    surface-subtitle-key="sections.logSurfaceHint"
    timeline-title-key="sections.logTimeline"
    timeline-subtitle-key="sections.logTimelineHint"
    :metrics="logMetrics"
    :cards="logCards"
    :timeline="logTimeline"
  />
</template>

<script setup lang="ts">
import PlaceholderModulePage from '../components/blocks/PlaceholderModulePage.vue';
import { logCards, logMetrics, logTimeline } from '../data/adminApp';
</script>
