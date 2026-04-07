<template>
  <div class="space-y-6">
    <section class="grid gap-4 md:grid-cols-3">
      <MetricCard v-for="item in websocketMetrics" :key="item.id" :item="item" />
    </section>

    <div class="grid gap-6 2xl:grid-cols-[minmax(0,1.2fr)_minmax(0,0.8fr)]">
      <SectionPanel title-key="sections.websocketChannels" subtitle-key="sections.websocketChannelsHint">
        <div class="grid gap-4 xl:grid-cols-3">
          <InfoCard v-for="item in websocketChannels" :key="item.id" :item="item" />
        </div>
      </SectionPanel>

      <SectionPanel title-key="sections.websocketPipeline" subtitle-key="sections.websocketPipelineHint">
        <ul class="space-y-3">
          <li
            v-for="item in websocketPipeline"
            :key="item.id"
            class="rounded-2xl bg-slate-50 px-4 py-4"
          >
            <div class="font-mono text-xs text-slate-500">{{ item.meta }}</div>
            <h4 class="mt-2 text-base font-semibold text-slate-950">{{ t(item.titleKey) }}</h4>
            <p class="mt-1 text-sm leading-6 text-slate-600">{{ t(item.detailKey) }}</p>
          </li>
        </ul>
      </SectionPanel>
    </div>

    <SectionPanel title-key="sections.websocketIncidents" subtitle-key="sections.websocketIncidentsHint">
      <div class="grid gap-4 lg:grid-cols-2">
        <article
          v-for="item in websocketIncidents"
          :key="item.id"
          class="rounded-2xl bg-slate-50 px-5 py-5"
        >
          <div class="font-mono text-xs text-slate-500">{{ item.meta }}</div>
          <h4 class="mt-2 text-base font-semibold text-slate-950">{{ t(item.titleKey) }}</h4>
          <p class="mt-1 text-sm leading-6 text-slate-600">{{ t(item.detailKey) }}</p>
        </article>
      </div>
    </SectionPanel>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';

import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import MetricCard from '../components/cards/MetricCard.vue';
import { websocketChannels, websocketIncidents, websocketMetrics, websocketPipeline } from '../data/adminApp';

const { t } = useI18n();
</script>
