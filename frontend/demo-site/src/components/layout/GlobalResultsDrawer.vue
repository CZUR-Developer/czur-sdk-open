<template>
  <div
    v-if="open"
    class="fixed inset-0 z-50 bg-slate-950/38"
    @click.self="$emit('close')"
  >
    <aside class="ml-auto flex h-full w-full max-w-xl flex-col border-l border-slate-200 bg-white">
      <div class="flex items-center justify-between border-b border-slate-200 px-5 py-4">
        <div>
          <p class="text-xs font-semibold uppercase tracking-[0.22em] text-cyan-700">{{ t('common.actionCenter') }}</p>
          <h2 class="mt-2 text-xl font-semibold tracking-tight text-slate-950">{{ t('common.runtimeActivity') }}</h2>
        </div>

        <button
          type="button"
          class="inline-flex size-10 items-center justify-center rounded-xl border border-slate-200 text-slate-500 hover:text-slate-900"
          @click="$emit('close')"
        >
          <svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" class="size-5">
            <path d="M5 5l10 10M15 5 5 15" />
          </svg>
        </button>
      </div>

      <div class="flex-1 space-y-6 overflow-y-auto px-5 py-5">
        <section>
          <div class="mb-3 flex items-center justify-between">
            <h3 class="text-sm font-semibold uppercase tracking-[0.18em] text-slate-500">{{ t('common.recentRequests') }}</h3>
            <StatusPill :label="String(requests.length)" tone="primary" />
          </div>
          <div v-if="requests.length > 0" class="space-y-3">
            <article
              v-for="request in requests"
              :key="request.id"
              class="rounded-2xl border border-slate-200 bg-slate-50/70 p-4"
            >
              <div class="flex items-start justify-between gap-3">
                <div>
                  <p class="font-mono text-[13px] text-slate-900">{{ request.method }}</p>
                  <p class="mt-2 text-xs uppercase tracking-[0.16em] text-slate-500">
                    {{ t('labels.requestId') }} · {{ request.requestId }}
                  </p>
                </div>
                <StatusPill
                  :label="t(executionStateLabelKey(request.state))"
                  :tone="executionStateTone(request.state)"
                />
              </div>
              <div class="mt-3 grid gap-2 text-xs text-slate-500 sm:grid-cols-3">
                <span>{{ t('labels.code') }} · {{ request.code }}</span>
                <span class="font-mono">{{ t('labels.traceId') }} · {{ request.traceId }}</span>
                <span>{{ request.duration }}</span>
              </div>
            </article>
          </div>
          <div
            v-else
            class="rounded-2xl border border-dashed border-slate-200 bg-slate-50/70 px-4 py-5 text-sm leading-6 text-slate-500"
          >
            {{ t('common.noRequestsYet') }}
          </div>
        </section>

        <section>
          <div class="mb-3 flex items-center justify-between">
            <h3 class="text-sm font-semibold uppercase tracking-[0.18em] text-slate-500">{{ t('common.recentEvents') }}</h3>
            <StatusPill :label="String(events.length)" tone="success" />
          </div>
          <EventTimeline v-if="events.length > 0" :items="events" />
          <div
            v-else
            class="rounded-2xl border border-dashed border-slate-200 bg-slate-50/70 px-4 py-5 text-sm leading-6 text-slate-500"
          >
            {{ t('common.noEventsYet') }}
          </div>
        </section>

        <section>
          <div class="mb-3 flex items-center justify-between">
            <h3 class="text-sm font-semibold uppercase tracking-[0.18em] text-slate-500">{{ t('common.recentErrors') }}</h3>
            <StatusPill :label="String(errors.length)" tone="warning" />
          </div>
          <div v-if="errors.length > 0" class="space-y-3">
            <article
              v-for="error in errors"
              :key="error.id"
              class="rounded-2xl border border-rose-100 bg-rose-50/70 p-4"
            >
              <div class="flex items-start justify-between gap-3">
                <div>
                  <p class="text-sm font-semibold text-slate-950">{{ error.name }}</p>
                  <p class="mt-1 font-mono text-[13px] text-slate-700">{{ error.method }}</p>
                </div>
                <StatusPill :label="error.code" tone="danger" />
              </div>
              <p class="mt-3 text-sm leading-6 text-slate-600">{{ error.message }}</p>
              <p class="mt-3 text-xs uppercase tracking-[0.16em] text-slate-500">
                {{ error.occurredAt }} · {{ error.traceId }}
              </p>
            </article>
          </div>
          <div
            v-else
            class="rounded-2xl border border-dashed border-slate-200 bg-slate-50/70 px-4 py-5 text-sm leading-6 text-slate-500"
          >
            {{ t('common.noErrorsYet') }}
          </div>
        </section>
      </div>
    </aside>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';

import type { ErrorItem, RequestHistoryItem, TimelineItem } from '../../types/demo';
import { executionStateLabelKey, executionStateTone } from '../../utils/presentation';
import EventTimeline from '../blocks/EventTimeline.vue';
import StatusPill from '../cards/StatusPill.vue';

defineProps<{
  open: boolean;
  requests: RequestHistoryItem[];
  events: TimelineItem[];
  errors: ErrorItem[];
}>();

defineEmits<{
  close: [];
}>();

const { t } = useI18n();
</script>
