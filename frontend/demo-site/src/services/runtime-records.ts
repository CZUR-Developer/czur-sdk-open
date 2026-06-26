import { reactive, readonly } from 'vue';

import type { ErrorItem, ExecutionState, RequestHistoryItem, TimelineItem } from '../types/demo';

const MAX_REQUESTS = 24;
const MAX_EVENTS = 24;
const MAX_ERRORS = 24;

const state = reactive({
  requests: [] as RequestHistoryItem[],
  events: [] as TimelineItem[],
  internalEvents: [] as TimelineItem[],
  errors: [] as ErrorItem[],
});

export const runtimeRecordState = readonly(state);

export function recordCommandRequest(entry: {
  method: string;
  requestId: string;
  code: string | number;
  traceId: string;
  durationMs: number;
  state?: ExecutionState;
}): void {
  state.requests.unshift({
    id: `${entry.requestId}-${Date.now().toString(36)}`,
    method: entry.method,
    requestId: entry.requestId,
    code: String(entry.code),
    traceId: entry.traceId,
    duration: `${Math.max(1, Math.round(entry.durationMs))}ms`,
    state: entry.state ?? (String(entry.code) === '0' ? 'success' : 'error'),
  });
  state.requests.splice(MAX_REQUESTS);
}

export function recordRuntimeEvent(entry: {
  title: string;
  detail: string;
  tone: TimelineItem['tone'];
  meta?: string;
}): void {
  state.events.unshift({
    id: `${entry.title}-${Date.now().toString(36)}`,
    title: entry.title,
    detail: entry.detail,
    meta: entry.meta ?? nowTimeLabel(),
    tone: entry.tone,
  });
  state.events.splice(MAX_EVENTS);
}

export function recordInternalRuntimeEvent(entry: {
  title: string;
  detail: string;
  tone: TimelineItem['tone'];
  meta?: string;
}): void {
  state.internalEvents.unshift({
    id: `${entry.title}-${Date.now().toString(36)}`,
    title: entry.title,
    detail: entry.detail,
    meta: entry.meta ?? nowTimeLabel(),
    tone: entry.tone,
  });
  state.internalEvents.splice(MAX_EVENTS);
}

export function recordAlert(entry: {
  name: string;
  method: string;
  code: string | number;
  message: string;
  traceId?: string;
  occurredAt?: string;
}): void {
  state.errors.unshift({
    id: `${entry.name}-${Date.now().toString(36)}`,
    code: String(entry.code),
    name: entry.name,
    method: entry.method,
    message: entry.message,
    traceId: entry.traceId ?? '-',
    occurredAt: entry.occurredAt ?? nowTimeLabel(),
  });
  state.errors.splice(MAX_ERRORS);
}

export function nowTimeLabel(): string {
  return new Date().toLocaleTimeString('en-GB', { hour12: false });
}
