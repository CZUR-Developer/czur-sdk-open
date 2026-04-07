export type LocaleKey = 'en' | 'zh-CN';
export type NavKey =
  | 'dashboard'
  | 'websocket'
  | 'system'
  | 'auth'
  | 'log'
  | 'config'
  | 'support'
  | 'documention';
export type Tone = 'primary' | 'success' | 'warning' | 'neutral';

export interface NavigationItem {
  key: NavKey;
  labelKey: string;
  to?: string;
  href?: string;
  external?: boolean;
}

export interface MetricCardItem {
  id: string;
  value: string;
  labelKey: string;
  detailKey: string;
  trend: string;
  tone: Tone;
}

export interface EntryCardStat {
  labelKey: string;
  value: string;
}

export interface EntryCardItem {
  key: NavKey;
  eyebrowKey: string;
  titleKey: string;
  descriptionKey: string;
  to: string;
  ctaKey: string;
  stats: EntryCardStat[];
}

export interface InfoCardItem {
  id: string;
  eyebrowKey: string;
  title: string;
  descriptionKey: string;
  meta: string;
  badgeKey?: string;
  tone: Tone;
}

export interface TimelineItem {
  id: string;
  titleKey: string;
  detailKey: string;
  meta: string;
  tone: Tone;
}

export interface ConfigLine {
  labelKey: string;
  value: string;
  hintKey?: string;
}
