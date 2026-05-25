import { createRouter, createWebHistory } from 'vue-router';

import AuthPage from '../pages/AuthPage.vue';
import ConfigPage from '../pages/ConfigPage.vue';
import DashboardOverviewPage from '../pages/DashboardOverviewPage.vue';
import LogPage from '../pages/LogPage.vue';
import RecordsPage from '../pages/RecordsPage.vue';
import SystemInfoConfigPage from '../pages/SystemInfoConfigPage.vue';
import WebSocketServicesPage from '../pages/WebSocketServicesPage.vue';

export const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/',
      redirect: '/dashboard',
    },
    {
      path: '/dashboard',
      name: 'dashboard',
      component: DashboardOverviewPage,
      meta: {
        navKey: 'dashboard',
        titleKey: 'pages.dashboard.title',
        subtitleKey: 'pages.dashboard.subtitle',
      },
    },
    {
      path: '/websocket',
      name: 'websocket',
      component: WebSocketServicesPage,
      meta: {
        navKey: 'websocket',
        titleKey: 'pages.websocket.title',
        subtitleKey: 'pages.websocket.subtitle',
      },
    },
    {
      path: '/system',
      name: 'system',
      component: SystemInfoConfigPage,
      meta: {
        navKey: 'system',
        titleKey: 'pages.system.title',
        subtitleKey: 'pages.system.subtitle',
      },
    },
    {
      path: '/auth',
      name: 'auth',
      component: AuthPage,
      meta: {
        navKey: 'auth',
        titleKey: 'pages.auth.title',
        subtitleKey: 'pages.auth.subtitle',
      },
    },
    {
      path: '/log',
      name: 'log',
      component: LogPage,
      meta: {
        navKey: 'log',
        titleKey: 'pages.log.title',
        subtitleKey: 'pages.log.subtitle',
      },
    },
    {
      path: '/records',
      name: 'records',
      component: RecordsPage,
      meta: {
        navKey: 'records',
        titleKey: 'pages.records.title',
        subtitleKey: 'pages.records.subtitle',
      },
    },
    {
      path: '/config',
      name: 'config',
      component: ConfigPage,
      meta: {
        navKey: 'config',
        titleKey: 'pages.config.title',
        subtitleKey: 'pages.config.subtitle',
      },
    },
  ],
});
