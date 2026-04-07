<template>
  <div class="space-y-6">
    <section class="grid gap-4 md:grid-cols-3">
      <MetricCard v-for="item in metrics" :key="item.id" :item="item" />
    </section>

    <div class="grid gap-6 2xl:grid-cols-[minmax(0,1.2fr)_minmax(0,0.8fr)]">
      <SectionPanel :title-key="surfaceTitleKey" :subtitle-key="surfaceSubtitleKey">
        <div class="grid gap-4 xl:grid-cols-3">
          <InfoCard v-for="item in cards" :key="item.id" :item="item" />
        </div>
      </SectionPanel>

      <SectionPanel :title-key="timelineTitleKey" :subtitle-key="timelineSubtitleKey">
        <ul class="space-y-3">
          <li
            v-for="item in timeline"
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
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';

import type { InfoCardItem, MetricCardItem, TimelineItem } from '../../types/admin';
import SectionPanel from './SectionPanel.vue';
import InfoCard from '../cards/InfoCard.vue';
import MetricCard from '../cards/MetricCard.vue';

defineProps<{
  surfaceTitleKey: string;
  surfaceSubtitleKey: string;
  timelineTitleKey: string;
  timelineSubtitleKey: string;
  metrics: MetricCardItem[];
  cards: InfoCardItem[];
  timeline: TimelineItem[];
}>();

const { t } = useI18n();
</script>
