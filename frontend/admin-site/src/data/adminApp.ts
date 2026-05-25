import type { NavigationItem } from '../types/admin';

export const navigationItems: NavigationItem[] = [
  { key: 'dashboard', to: '/dashboard', labelKey: 'nav.dashboard' },
  { key: 'websocket', to: '/websocket', labelKey: 'nav.websocket' },
  { key: 'system', to: '/system', labelKey: 'nav.system' },
  { key: 'auth', to: '/auth', labelKey: 'nav.auth' },
  { key: 'log', to: '/log', labelKey: 'nav.log' },
  { key: 'records', to: '/records', labelKey: 'nav.records' },
  { key: 'config', to: '/config', labelKey: 'nav.config' },
];

export const supportNavigationItems: NavigationItem[] = [
  {
    key: 'support',
    href: 'https://www.czur.com/support',
    external: true,
    labelKey: 'nav.support',
  },
  {
    key: 'documention',
    href: 'https://www.czur.com/support',
    external: true,
    labelKey: 'nav.documention',
  },
];
