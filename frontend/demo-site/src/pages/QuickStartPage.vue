<template>
  <div class="space-y-6">
    <div class="grid gap-4 xl:grid-cols-3">
      <MetricCard
        v-for="metric in quickStartMetrics"
        :key="metric.id"
        :label="t(metric.labelKey)"
        :value="metric.value"
        :detail="metric.detailKey ? t(metric.detailKey) : metric.detail"
        :trend="metric.trendKey ? t(metric.trendKey) : metric.trend"
        :tone="metric.tone"
      />
    </div>

    <SectionPanel :title="t('sections.endpoints')" :description="t('pages.quickStart.subtitle')">
      <template #actions>
        <StatusPill :label="t('actions.healthCheck')" tone="primary" />
      </template>

      <div class="grid gap-4 xl:grid-cols-2">
        <InfoCard
          v-for="endpoint in quickStartEndpoints"
          :key="endpoint.id"
          :eyebrow="endpoint.protocol"
          :title="endpoint.titleKey ? t(endpoint.titleKey) : endpoint.title"
          :description="endpoint.noteKey ? t(endpoint.noteKey) : endpoint.note"
          :meta="endpoint.url"
          :badge-label="t(executionStateLabelKey(endpoint.state))"
          :badge-tone="executionStateTone(endpoint.state)"
        >
          <template #footer>
            <StatusPill :label="t('actions.copyUrl')" tone="info" />
            <StatusPill :label="t('actions.healthCheck')" tone="success" />
          </template>
        </InfoCard>
      </div>
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-[1.45fr_0.95fr]">
      <SectionPanel :title="t('sections.quickChecks')" :description="t('common.primaryFlow')">
        <div class="grid gap-4 xl:grid-cols-3">
          <InfoCard
            v-for="check in quickStartChecks"
            :key="check.id"
            :eyebrow="check.method"
            :title="check.latency"
            :description="check.noteKey ? t(check.noteKey) : check.note"
            :badge-label="t(executionStateLabelKey(check.state))"
            :badge-tone="executionStateTone(check.state)"
          >
            <template #footer>
              <StatusPill :label="t('common.method')" tone="info" />
              <StatusPill :label="check.method" tone="neutral" />
            </template>
          </InfoCard>
        </div>
      </SectionPanel>

      <JsonPanel
        :title="sharedJsonSnippets.quickStart.title"
        :caption="sharedJsonSnippets.quickStart.caption"
        :payload="sharedJsonSnippets.quickStart.payload"
      />
    </div>

    <SectionPanel :title="t('sections.capabilityMap')" :description="t('common.mockMode')">
      <div class="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
        <InfoCard
          v-for="capability in quickStartCapabilities"
          :key="capability.id"
          :eyebrow="capability.domain"
          :title="capability.coverage"
          :description="`${t('common.status')} · ${t(capabilityStatusLabelKey(capability.status))}`"
          :badge-label="t(capabilityStatusLabelKey(capability.status))"
          :badge-tone="capabilityStatusTone(capability.status)"
        >
          <template #footer>
            <StatusPill :label="`${t('common.coverage')} · ${capability.coverage}`" tone="info" />
            <StatusPill
              v-for="tag in capability.tags"
              :key="tag"
              :label="t(requirementTagLabelKey(tag))"
              :tone="requirementTagTone(tag)"
            />
          </template>
        </InfoCard>
      </div>
    </SectionPanel>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';

import JsonPanel from '../components/blocks/JsonPanel.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import MetricCard from '../components/cards/MetricCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { quickStartCapabilities, quickStartChecks, quickStartEndpoints, quickStartMetrics, sharedJsonSnippets } from '../data/demoSite';
import {
  capabilityStatusLabelKey,
  capabilityStatusTone,
  executionStateLabelKey,
  executionStateTone,
  requirementTagLabelKey,
  requirementTagTone,
} from '../utils/presentation';

const { t } = useI18n();
</script>
