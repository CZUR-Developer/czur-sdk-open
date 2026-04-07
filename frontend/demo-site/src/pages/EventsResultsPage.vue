<template>
  <div class="space-y-6">
    <div class="grid gap-4 xl:grid-cols-3">
      <MetricCard
        v-for="metric in resultCenterMetrics"
        :key="metric.id"
        :label="t(metric.labelKey)"
        :value="metric.value"
        :detail="metric.detail"
        :trend="metric.trend"
        :tone="metric.tone"
      />
    </div>

    <SectionPanel :title="t('sections.requestHistory')" :description="t('common.recentRequests')">
      <template #actions>
        <div class="flex flex-wrap gap-2">
          <StatusPill :label="t('common.exportSummary')" tone="primary" />
          <StatusPill :label="t('common.clearSession')" tone="warning" />
        </div>
      </template>

      <DataTableCard :columns="requestColumns" :rows="requestRows" />
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-[1.05fr_0.95fr]">
      <SectionPanel :title="t('sections.eventCenter')" :description="t('common.recentEvents')">
        <EventTimeline :items="globalEvents" />
      </SectionPanel>

      <SectionPanel :title="t('sections.errorCenter')" :description="t('common.recentErrors')">
        <div class="space-y-4">
          <InfoCard
            v-for="error in globalErrors"
            :key="error.id"
            :eyebrow="error.method"
            :title="error.name"
            :description="error.message"
            :meta="`${error.occurredAt} · ${error.traceId}`"
            :badge-label="error.code"
            badge-tone="danger"
          />
        </div>
      </SectionPanel>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import DataTableCard from '../components/blocks/DataTableCard.vue';
import EventTimeline from '../components/blocks/EventTimeline.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import MetricCard from '../components/cards/MetricCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { globalErrors, globalEvents, globalRequestHistory, resultCenterMetrics } from '../data/demoSite';

const { t } = useI18n();

const requestColumns = computed(() => [
  { key: 'method', label: t('labels.method'), monospace: true },
  { key: 'requestId', label: t('labels.requestId'), monospace: true },
  { key: 'code', label: t('labels.code'), align: 'right' },
  { key: 'traceId', label: t('labels.traceId'), monospace: true },
  { key: 'duration', label: t('common.latency'), align: 'right' },
]);

const requestRows = computed(() =>
  globalRequestHistory.map((item) => ({
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
