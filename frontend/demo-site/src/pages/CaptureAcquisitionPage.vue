<template>
  <div class="space-y-6">
    <SectionPanel :title="t('sections.capturePresets')" :description="t('pages.captureAcquisition.subtitle')">
      <div class="grid gap-4 xl:grid-cols-3">
        <InfoCard
          v-for="card in capturePresetCards"
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

    <div class="grid gap-6 xl:grid-cols-[1fr_1.1fr]">
      <SectionPanel :title="t('common.parameters')" :description="t('common.primaryFlow')">
        <KeyValueGrid :items="captureParameterItems" />
      </SectionPanel>

      <SectionPanel :title="t('sections.captureStrategies')" :description="t('common.actions')">
        <div class="grid gap-4 xl:grid-cols-3">
          <InfoCard
            v-for="card in captureStrategyCards"
            :key="card.id"
            :eyebrow="card.eyebrow"
            :title="card.title"
            :description="card.description"
            :meta="card.meta"
            :badge-label="card.state ? t(executionStateLabelKey(card.state)) : undefined"
            :badge-tone="card.state ? executionStateTone(card.state) : 'neutral'"
          />
        </div>
      </SectionPanel>
    </div>

    <div class="grid gap-6 xl:grid-cols-[1.05fr_0.95fr]">
      <SectionPanel :title="t('sections.captureResults')" :description="t('common.results')">
        <PreviewStage
          :eyebrow="t('labels.outputPath')"
          title="capture-042.jpg"
          description="Captured page can be sent forward to image processing, OCR recognition, or file conversion without rebuilding the input contract."
          :metrics="capturePreviewMetrics"
          :badge-label="t('status.success')"
          badge-tone="success"
          variant="image"
        />
      </SectionPanel>

      <div class="space-y-6">
        <SectionPanel :title="t('sections.realtimeEvents')" :description="t('common.timeline')">
          <EventTimeline :items="captureEvents" />
        </SectionPanel>

        <JsonPanel
          :title="sharedJsonSnippets.capture.title"
          :caption="sharedJsonSnippets.capture.caption"
          :payload="sharedJsonSnippets.capture.payload"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import EventTimeline from '../components/blocks/EventTimeline.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import KeyValueGrid from '../components/blocks/KeyValueGrid.vue';
import PreviewStage from '../components/blocks/PreviewStage.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import {
  captureEvents,
  capturePresetCards,
  capturePreviewMetrics,
  captureStrategyCards,
  sharedJsonSnippets,
} from '../data/demoSite';
import { executionStateLabelKey, executionStateTone, requirementTagLabelKey, requirementTagTone } from '../utils/presentation';

const { t } = useI18n();

const captureParameterItems = computed(() => [
  { label: 'device_id', value: 'dev-01', monospace: true },
  { label: t('labels.outputPath'), value: '/tmp/sdk-demo/capture-042.jpg', monospace: true },
  { label: t('labels.format'), value: 'jpeg' },
  { label: 'dpi', value: '300' },
  { label: 'color_mode', value: 'document-color' },
  { label: 'auto_crop', value: 'true' },
  { label: 'auto_deskew', value: 'true' },
  { label: 'detect_barcode', value: 'true' },
  { label: 'page_fill', value: 'paper-white' },
  { label: 'edge_repair', value: 'soft' },
  { label: 'scan_wide_mode', value: 'false' },
  { label: 'curve_flatten', value: 'balanced' },
]);
</script>
