<template>
  <div class="space-y-6">
    <SectionPanel :title="t('sections.ocrWorkbench')" :description="t('pages.ocrRecognition.subtitle')">
      <div class="grid gap-4 xl:grid-cols-3">
        <InfoCard
          v-for="card in ocrWorkbenchCards"
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

    <div class="grid gap-6 xl:grid-cols-[1.1fr_0.9fr]">
      <SectionPanel :title="t('common.results')" :description="t('common.response')">
        <DataTableCard :columns="ocrColumns" :rows="ocrBlockRows" />
      </SectionPanel>

      <div class="space-y-6">
        <SectionPanel :title="t('sections.barcodeWorkbench')" :description="t('common.parameters')">
          <KeyValueGrid :items="ocrResultItems" />
        </SectionPanel>

        <SectionPanel :title="t('sections.realtimeEvents')" :description="t('common.timeline')">
          <EventTimeline :items="ocrEvents" />
        </SectionPanel>
      </div>
    </div>

    <JsonPanel
      :title="sharedJsonSnippets.ocr.title"
      :caption="sharedJsonSnippets.ocr.caption"
      :payload="sharedJsonSnippets.ocr.payload"
    />
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import DataTableCard from '../components/blocks/DataTableCard.vue';
import EventTimeline from '../components/blocks/EventTimeline.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import KeyValueGrid from '../components/blocks/KeyValueGrid.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import { ocrBlockRows, ocrEvents, ocrWorkbenchCards, sharedJsonSnippets } from '../data/demoSite';
import { executionStateLabelKey, executionStateTone } from '../utils/presentation';

const { t } = useI18n();

const ocrColumns = computed(() => [
  { key: 'type', label: t('labels.type') },
  { key: 'value', label: t('labels.value') },
  { key: 'confidence', label: t('labels.confidence'), align: 'right', monospace: true },
  { key: 'language', label: t('common.locale') },
]);

const ocrResultItems = computed(() => [
  { label: 'text', value: 'SDK Demo Site Validation Flow' },
  { label: t('common.locale'), value: 'en + zh-CN' },
  { label: t('labels.confidence'), value: '0.99 / 0.96' },
  { label: t('labels.pageCount'), value: '1' },
  { label: 'barcode_types', value: 'QR_CODE, CODE128' },
  { label: 'realtime_barcode', value: 'enabled · debounce=180ms' },
]);
</script>
