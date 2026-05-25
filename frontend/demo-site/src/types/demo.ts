export type LocaleKey = 'en' | 'zh-CN';

export type DemoNavKey =
  | 'quick-start'
  | 'connection-auth'
  | 'device-video'
  | 'capture-acquisition'
  | 'image-processing'
  | 'image-enhancement'
  | 'ocr-recognition'
  | 'file-conversion'
  | 'sane-scanning';

export type Tone = 'primary' | 'success' | 'warning' | 'danger' | 'neutral' | 'info';
export type ExecutionState = 'idle' | 'running' | 'success' | 'error' | 'blocked' | 'planned';
export type CapabilityStatus = 'ga' | 'beta' | 'planned';
export type RequirementTag = 'device' | 'provider' | 'account';

export interface NavigationItem {
  key: DemoNavKey;
  labelKey: string;
  to: string;
}

export interface ShellStatusItem {
  id: string;
  labelKey: string;
  value: string;
  tone: Tone;
}

export interface MetricCardItem {
  id: string;
  value: string;
  labelKey: string;
  detail: string;
  detailKey?: string;
  trend?: string;
  trendKey?: string;
  tone: Tone;
}

export interface EndpointCardItem {
  id: string;
  title: string;
  titleKey?: string;
  url: string;
  protocol: string;
  note: string;
  noteKey?: string;
  state: ExecutionState;
}

export interface QuickCheckItem {
  id: string;
  method: string;
  latency: string;
  note: string;
  noteKey?: string;
  state: ExecutionState;
  payload: string;
}

export interface CapabilityItem {
  id: string;
  domain: string;
  coverage: string;
  status: CapabilityStatus;
  tags: RequirementTag[];
}

export interface InfoCardItem {
  id: string;
  eyebrow?: string;
  title: string;
  description: string;
  meta?: string;
  tone?: Tone;
  state?: ExecutionState;
  status?: CapabilityStatus;
  tags?: RequirementTag[];
}

export interface KeyValueItem {
  label: string;
  value: string;
  hint?: string;
  monospace?: boolean;
  tone?: Tone;
  fullWidth?: boolean;
}

export interface TableColumn {
  key: string;
  label: string;
  align?: 'left' | 'right';
  monospace?: boolean;
}

export interface TableRow {
  id: string;
  cells: Record<string, string>;
  tone?: Tone;
}

export interface TimelineItem {
  id: string;
  title: string;
  detail: string;
  meta?: string;
  tone: Tone;
}

export interface ErrorItem {
  id: string;
  code: string;
  name: string;
  method: string;
  message: string;
  traceId: string;
  occurredAt: string;
}

export interface RequestHistoryItem {
  id: string;
  method: string;
  requestId: string;
  code: string;
  traceId: string;
  duration: string;
  state: ExecutionState;
}

export interface JsonSnippet {
  id: string;
  title: string;
  payload: string;
  caption?: string;
}

export interface PreviewMetric {
  label: string;
  value: string;
  monospace?: boolean;
}
