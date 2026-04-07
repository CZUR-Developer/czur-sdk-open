<template>
  <div class="space-y-6">
    <section class="grid gap-4 md:grid-cols-3">
      <MetricCard v-for="item in systemMetrics" :key="item.id" :item="item" />
    </section>

    <div class="grid gap-6 2xl:grid-cols-[minmax(0,1.2fr)_minmax(0,0.8fr)]">
      <SectionPanel title-key="sections.systemProfiles" subtitle-key="sections.systemProfilesHint">
        <dl class="space-y-3">
          <div
            v-for="item in systemConfig"
            :key="item.labelKey"
            class="rounded-2xl bg-slate-50 px-4 py-4"
          >
            <dt class="text-xs font-medium uppercase tracking-[0.18em] text-slate-500">{{ t(item.labelKey) }}</dt>
            <dd class="mt-2 font-mono text-sm leading-6 text-slate-900">{{ item.value }}</dd>
            <p v-if="item.hintKey" class="mt-2 text-sm leading-6 text-slate-600">{{ t(item.hintKey) }}</p>
          </div>
        </dl>
      </SectionPanel>

      <SectionPanel title-key="sections.systemFlags" subtitle-key="sections.systemFlagsHint">
        <ul class="space-y-3">
          <li
            v-for="item in systemChecklist"
            :key="item.id"
            class="rounded-2xl bg-slate-50 px-4 py-4"
          >
            <div class="font-mono text-xs text-slate-500">{{ item.meta }}</div>
            <h4 class="mt-2 text-base font-semibold text-slate-950">{{ t(item.titleKey) }}</h4>
            <p class="mt-1 text-sm leading-6 text-slate-600">{{ t(item.detailKey) }}</p>
          </li>
        </ul>
      </SectionPanel>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';

import SectionPanel from '../components/blocks/SectionPanel.vue';
import MetricCard from '../components/cards/MetricCard.vue';
import { systemChecklist, systemConfig, systemMetrics } from '../data/adminApp';

const { t } = useI18n();
</script>
