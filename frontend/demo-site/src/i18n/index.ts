import { createI18n } from 'vue-i18n';

import type { LocaleKey } from '../types/demo';
import { messages } from './messages';

const DEFAULT_LOCALE: LocaleKey = 'zh-CN';

function resolveInitialLocale(): LocaleKey {
  try {
    const saved = localStorage.getItem('sdk-demo-site-locale');
    if (saved === 'en' || saved === 'zh-CN') {
      return saved;
    }
  } catch {
    return DEFAULT_LOCALE;
  }

  return DEFAULT_LOCALE;
}

export const i18n = createI18n({
  legacy: false,
  locale: resolveInitialLocale(),
  fallbackLocale: 'en',
  messages,
});
