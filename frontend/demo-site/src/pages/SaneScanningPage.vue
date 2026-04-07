<template>
  <div class="space-y-6">
    <SectionPanel :title="t('sections.saneAvailability')" :description="t('pages.saneScanning.subtitle')">
      <div class="grid gap-4 xl:grid-cols-2">
        <InfoCard
          v-for="card in saneAvailabilityCards"
          :key="card.id"
          :eyebrow="card.eyebrow"
          :title="card.title"
          :description="card.description"
          :meta="card.meta"
          :badge-label="card.state ? t(executionStateLabelKey(card.state)) : card.status ? t(capabilityStatusLabelKey(card.status)) : undefined"
          :badge-tone="
            card.state
              ? executionStateTone(card.state)
              : card.status
                ? capabilityStatusTone(card.status)
                : 'neutral'
          "
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

    <div class="grid gap-6 xl:grid-cols-[1.05fr_0.95fr]">
      <SectionPanel :title="t('sections.deviceInventory')" :description="t('common.preview')">
        <DataTableCard :columns="saneDeviceColumns" :rows="saneDeviceRows" />
      </SectionPanel>

      <SectionPanel :title="t('sections.saneOptions')" :description="t('common.parameters')">
        <DataTableCard :columns="saneOptionColumns" :rows="saneOptionRows" />
      </SectionPanel>
    </div>

    <div class="grid gap-6 xl:grid-cols-[0.95fr_1.05fr]">
      <SectionPanel :title="t('sections.realtimeEvents')" :description="t('common.timeline')">
        <EventTimeline :items="saneEvents" />
      </SectionPanel>

      <JsonPanel
        :title="sharedJsonSnippets.sane.title"
        :caption="sharedJsonSnippets.sane.caption"
        :payload="sharedJsonSnippets.sane.payload"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import DataTableCard from '../components/blocks/DataTableCard.vue';
import EventTimeline from '../components/blocks/EventTimeline.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { saneAvailabilityCards, saneDeviceRows, saneEvents, saneOptionRows, sharedJsonSnippets } from '../data/demoSite';
import {
  capabilityStatusLabelKey,
  capabilityStatusTone,
  executionStateLabelKey,
  executionStateTone,
  requirementTagLabelKey,
  requirementTagTone,
} from '../utils/presentation';

const { t } = useI18n();

const saneDeviceColumns = computed(() => [
  { key: 'name', label: 'name', monospace: true },
  { key: 'model', label: t('labels.model') },
  { key: 'type', label: t('labels.type') },
  { key: 'status', label: t('labels.status') },
]);

const saneOptionColumns = computed(() => [
  { key: 'option', label: t('labels.option') },
  { key: 'value', label: t('labels.value') },
  { key: 'type', label: t('labels.type') },
  { key: 'status', label: t('labels.status') },
]);
</script>
