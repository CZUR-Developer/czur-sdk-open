<template>
  <div class="space-y-6">
    <SectionPanel :title="t('sections.fileTemplates')" :description="t('pages.fileConversion.subtitle')">
      <div class="grid gap-4 md:grid-cols-2 xl:grid-cols-4">
        <InfoCard
          v-for="card in fileTemplateCards"
          :key="card.id"
          :eyebrow="card.eyebrow"
          :title="card.title"
          :description="card.description"
          :badge-label="card.status ? t(capabilityStatusLabelKey(card.status)) : undefined"
          :badge-tone="card.status ? capabilityStatusTone(card.status) : 'neutral'"
        />
      </div>
    </SectionPanel>

    <div class="grid gap-6 xl:grid-cols-[0.95fr_1.05fr]">
      <SectionPanel :title="t('common.parameters')" :description="t('common.request')">
        <KeyValueGrid :items="fileParameterItems" />
      </SectionPanel>

      <SectionPanel :title="t('sections.fileWorkspace')" :description="t('common.workspace')">
        <div class="grid gap-4 xl:grid-cols-3">
          <InfoCard
            v-for="card in fileWorkspaceCards"
            :key="card.id"
            :eyebrow="card.eyebrow"
            :title="card.title"
            :description="card.description"
            :badge-label="card.state ? t(executionStateLabelKey(card.state)) : undefined"
            :badge-tone="card.state ? executionStateTone(card.state) : 'neutral'"
          />
        </div>
      </SectionPanel>
    </div>

    <div class="grid gap-6 xl:grid-cols-[0.9fr_1.1fr]">
      <SectionPanel :title="t('common.results')" :description="t('common.response')">
        <KeyValueGrid :items="fileResultItems" />
      </SectionPanel>

      <JsonPanel
        :title="sharedJsonSnippets.file.title"
        :caption="sharedJsonSnippets.file.caption"
        :payload="sharedJsonSnippets.file.payload"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';

import JsonPanel from '../components/blocks/JsonPanel.vue';
import KeyValueGrid from '../components/blocks/KeyValueGrid.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import InfoCard from '../components/cards/InfoCard.vue';
import { fileTemplateCards, fileWorkspaceCards, sharedJsonSnippets } from '../data/demoSite';
import { capabilityStatusLabelKey, capabilityStatusTone, executionStateLabelKey, executionStateTone } from '../utils/presentation';

const { t } = useI18n();

const fileParameterItems = computed(() => [
  { label: 'method', value: 'file.convert', monospace: true },
  { label: 'input_upload_id', value: 'img-1760000000-1', monospace: true },
  { label: 'output_format', value: 'png' },
  { label: t('labels.outputPath'), value: '/tmp/sdk-demo/converted.png', monospace: true },
  { label: 'scope', value: 'image format conversion' },
  { label: 'note', value: 'paper/color processing keeps the source format' },
]);

const fileResultItems = computed(() => [
  { label: t('labels.outputPath'), value: '/tmp/sdk-demo/converted.png', monospace: true },
  { label: 'output_format', value: 'png' },
  { label: t('labels.fileSize'), value: '2.8MB' },
  { label: 'asset_id', value: 'asset-converted', monospace: true },
  { label: 'content_type', value: 'image/png' },
  { label: 'workspace', value: '/tmp/sdk-demo/file/assets' },
]);
</script>
