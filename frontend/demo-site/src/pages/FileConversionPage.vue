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
  { label: 'source_type', value: 'images' },
  { label: 'source_files', value: 'capture-042.jpg, image-process-017.png, page-03.png' },
  { label: 'target_type', value: 'pdf' },
  { label: t('labels.outputPath'), value: '/tmp/sdk-demo/demo-bundle.pdf', monospace: true },
  { label: 'template', value: 'images -> pdf' },
  { label: 'append_bookmarks', value: 'true' },
]);

const fileResultItems = computed(() => [
  { label: t('labels.outputPath'), value: '/tmp/sdk-demo/demo-bundle.pdf', monospace: true },
  { label: 'target_type', value: 'pdf' },
  { label: t('labels.fileSize'), value: '12.4MB' },
  { label: t('labels.pageCount'), value: '3' },
  { label: 'rename_target', value: 'demo-bundle-final.pdf' },
  { label: 'workspace', value: '/tmp/sdk-demo/exports' },
]);
</script>
