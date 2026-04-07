<template>
  <PlaceholderModulePage
    surface-title-key="sections.configSurface"
    surface-subtitle-key="sections.configSurfaceHint"
    timeline-title-key="sections.configTimeline"
    timeline-subtitle-key="sections.configTimelineHint"
    :metrics="configMetricsPage"
    :cards="configCards"
    :timeline="configTimeline"
  />
</template>

<script setup lang="ts">
import PlaceholderModulePage from '../components/blocks/PlaceholderModulePage.vue';
import { configCards, configMetricsPage, configTimeline } from '../data/adminApp';
</script>
