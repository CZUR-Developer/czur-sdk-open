<template>
  <div class="space-y-6">
    <SectionPanel :title="t('sections.protocolTemplates')" :description="t('pages.protocolDebugger.subtitle')">
      <div class="grid gap-4 md:grid-cols-2 xl:grid-cols-4">
        <InfoCard
          v-for="card in protocolTemplateCards"
          :key="card.id"
          :eyebrow="card.eyebrow"
          :title="card.title"
          :description="card.description"
        >
          <template #footer>
            <StatusPill :label="t('actions.runTemplate')" tone="primary" />
          </template>
        </InfoCard>
      </div>
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-2">
      <SectionPanel :title="t('sections.requestEditor')" :description="t('common.request')">
        <JsonPanel
          :title="protocolRequestSnippet.title"
          :caption="protocolRequestSnippet.caption"
          :payload="protocolRequestSnippet.payload"
        />
      </SectionPanel>

      <SectionPanel :title="t('sections.responseViewer')" :description="t('common.response')">
        <JsonPanel
          :title="protocolResponseSnippet.title"
          :caption="protocolResponseSnippet.caption"
          :payload="protocolResponseSnippet.payload"
        />
      </SectionPanel>
    </div>

    <SectionPanel :title="t('sections.requestHistory')" :description="t('common.history')">
      <DataTableCard :columns="protocolColumns" :rows="protocolRows" />
    </SectionPanel>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import DataTableCard from '../components/blocks/DataTableCard.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { protocolHistory, protocolRequestSnippet, protocolResponseSnippet, protocolTemplateCards } from '../data/demoSite';

const { t } = useI18n();

const protocolColumns = computed(() => [
  { key: 'method', label: t('labels.method'), monospace: true },
  { key: 'requestId', label: t('labels.requestId'), monospace: true },
  { key: 'code', label: t('labels.code'), align: 'right' },
  { key: 'traceId', label: t('labels.traceId'), monospace: true },
  { key: 'duration', label: t('common.latency'), align: 'right' },
]);

const protocolRows = computed(() =>
  protocolHistory.map((item) => ({
    id: item.id,
    cells: {
      method: item.method,
      requestId: item.requestId,
      code: item.code,
      traceId: item.traceId,
      duration: item.duration,
    },
  })),
);
</script>
