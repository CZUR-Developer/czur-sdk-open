<template>
  <div class="space-y-6">
    <div class="grid gap-4 xl:grid-cols-3">
      <MetricCard
        v-for="metric in deviceVideoMetrics"
        :key="metric.id"
        :label="t(metric.labelKey)"
        :value="metric.value"
        :detail="metric.detail"
        :trend="metric.trend"
        :tone="metric.tone"
      />
    </div>

    <SectionPanel :title="t('sections.deviceInventory')" :description="t('pages.deviceVideo.subtitle')">
      <DataTableCard :columns="deviceColumns" :rows="deviceRows" />
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-[1.1fr_0.9fr]">
      <SectionPanel :title="t('sections.deviceControl')" :description="t('common.actions')">
        <div class="grid gap-4 xl:grid-cols-2">
          <InfoCard
            v-for="card in [...deviceControlCards, ...streamCards]"
            :key="card.id"
            :eyebrow="card.eyebrow"
            :title="card.title"
            :description="card.description"
            :meta="card.meta"
            :badge-label="card.state ? t(executionStateLabelKey(card.state)) : undefined"
            :badge-tone="card.state ? executionStateTone(card.state) : 'neutral'"
          >
            <template #footer>
              <StatusPill
                v-for="tag in card.tags"
                :key="tag"
                :label="t(requirementTagLabelKey(tag))"
                :tone="requirementTagTone(tag)"
              />
            </template>
          </InfoCard>
        </div>
      </SectionPanel>

      <div class="space-y-6">
        <SectionPanel :title="t('sections.videoPreview')" :description="t('common.preview')">
          <PreviewStage
            :eyebrow="t('labels.selectedDevice')"
            title="CZUR ET18 Pro preview lane"
            description="Live mock preview keeps frame cadence, lane posture, and current resolution visible without binding to a real camera."
            :metrics="videoPreviewMetrics"
            :badge-label="t('actions.subscribe')"
            badge-tone="primary"
            variant="video"
          />
        </SectionPanel>

        <SectionPanel :title="t('sections.realtimeEvents')" :description="t('common.timeline')">
          <EventTimeline :items="deviceVideoEvents" />
        </SectionPanel>
      </div>
    </div>

    <JsonPanel
      :title="sharedJsonSnippets.deviceVideo.title"
      :caption="sharedJsonSnippets.deviceVideo.caption"
      :payload="sharedJsonSnippets.deviceVideo.payload"
    />
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import DataTableCard from '../components/blocks/DataTableCard.vue';
import EventTimeline from '../components/blocks/EventTimeline.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import PreviewStage from '../components/blocks/PreviewStage.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import MetricCard from '../components/cards/MetricCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import {
  deviceControlCards,
  deviceRows,
  deviceVideoEvents,
  deviceVideoMetrics,
  sharedJsonSnippets,
  streamCards,
  videoPreviewMetrics,
} from '../data/demoSite';
import { executionStateLabelKey, executionStateTone, requirementTagLabelKey, requirementTagTone } from '../utils/presentation';

const { t } = useI18n();

const deviceColumns = computed(() => [
  { key: 'deviceId', label: 'device_id', monospace: true },
  { key: 'model', label: t('labels.model') },
  { key: 'displayName', label: 'display_name' },
  { key: 'vid', label: 'vid', monospace: true },
  { key: 'pid', label: 'pid', monospace: true },
  { key: 'status', label: t('labels.status') },
  { key: 'authorized', label: t('labels.auth') },
  { key: 'cameras', label: t('labels.cameras') },
]);
</script>
