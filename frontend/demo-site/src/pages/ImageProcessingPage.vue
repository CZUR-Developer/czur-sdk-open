<template>
  <div class="space-y-6">
    <SectionPanel :title="t('sections.inputResources')" :description="t('pages.imageProcessing.subtitle')">
      <div class="grid gap-4 xl:grid-cols-3">
        <InfoCard
          v-for="card in imageInputCards"
          :key="card.id"
          :eyebrow="card.eyebrow"
          :title="card.title"
          :description="card.description"
          :meta="card.meta"
        />
      </div>
    </SectionPanel>

    <SectionPanel :title="t('sections.operationChain')" :description="t('common.actions')">
      <div class="grid gap-4 md:grid-cols-2 xl:grid-cols-4">
        <InfoCard
          v-for="card in imageOperationCards"
          :key="card.id"
          :eyebrow="card.eyebrow"
          :title="card.title"
          :description="card.description"
          :badge-label="card.status ? t(capabilityStatusLabelKey(card.status)) : undefined"
          :badge-tone="card.status ? capabilityStatusTone(card.status) : 'neutral'"
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

    <div class="grid gap-6 xl:grid-cols-[0.95fr_1.05fr]">
      <SectionPanel :title="t('common.parameters')" :description="t('common.workspace')">
        <KeyValueGrid :items="imageParameterItems" />
      </SectionPanel>

      <SectionPanel :title="t('sections.comparisonStage')" :description="t('actions.compare')">
        <PreviewStage
          eyebrow="image.process"
          title="Before / after comparison"
          description="The comparison stage keeps input, output, and chain summary visible in one place so QA can validate each transformation without leaving the page."
          :metrics="imageComparisonMetrics"
          :badge-label="t('actions.compare')"
          badge-tone="primary"
          variant="image"
        />
      </SectionPanel>
    </div>

    <JsonPanel
      :title="sharedJsonSnippets.image.title"
      :caption="sharedJsonSnippets.image.caption"
      :payload="sharedJsonSnippets.image.payload"
    />
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import JsonPanel from '../components/blocks/JsonPanel.vue';
import KeyValueGrid from '../components/blocks/KeyValueGrid.vue';
import PreviewStage from '../components/blocks/PreviewStage.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { imageComparisonMetrics, imageInputCards, imageOperationCards, sharedJsonSnippets } from '../data/demoSite';
import { capabilityStatusLabelKey, capabilityStatusTone, requirementTagLabelKey, requirementTagTone } from '../utils/presentation';

const { t } = useI18n();

const imageParameterItems = computed(() => [
  { label: t('labels.inputMode'), value: 'capture handoff + local path' },
  { label: t('labels.pipeline'), value: 'normalize -> add_watermark -> merge' },
  { label: 'paper_size', value: 'A4' },
  { label: 'background_color', value: '#FFFFFF', monospace: true },
  { label: 'contrast', value: '+12' },
  { label: 'sharpness', value: '+8' },
  { label: 'watermark', value: 'Demo QA / bottom-right / 24%' },
  { label: 'merge_direction', value: 'vertical' },
]);
</script>
