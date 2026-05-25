import { createRouter, createWebHistory } from 'vue-router';

import CaptureAcquisitionPage from '../pages/CaptureAcquisitionPage.vue';
import ConnectionAuthPage from '../pages/ConnectionAuthPage.vue';
import DeviceVideoPage from '../pages/DeviceVideoPage.vue';
import EventsResultsPage from '../pages/EventsResultsPage.vue';
import FileConversionPage from '../pages/FileConversionPage.vue';
import ImageEnhancementPage from '../pages/ImageEnhancementPage.vue';
import ImageProcessingPage from '../pages/ImageProcessingPage.vue';
import OcrRecognitionPage from '../pages/OcrRecognitionPage.vue';
import QuickStartPage from '../pages/QuickStartPage.vue';
import SaneScanningPage from '../pages/SaneScanningPage.vue';

export const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/',
      redirect: '/quick-start',
    },
    {
      path: '/quick-start',
      name: 'quick-start',
      component: QuickStartPage,
      meta: {
        navKey: 'quick-start',
        titleKey: 'pages.quickStart.title',
        subtitleKey: 'pages.quickStart.subtitle',
      },
    },
    {
      path: '/connection-auth',
      name: 'connection-auth',
      component: ConnectionAuthPage,
      meta: {
        navKey: 'connection-auth',
        titleKey: 'pages.connectionAuth.title',
        subtitleKey: 'pages.connectionAuth.subtitle',
      },
    },
    {
      path: '/device-video',
      name: 'device-video',
      component: DeviceVideoPage,
      meta: {
        navKey: 'device-video',
        titleKey: 'pages.deviceVideo.title',
        subtitleKey: 'pages.deviceVideo.subtitle',
      },
    },
    {
      path: '/capture-acquisition',
      name: 'capture-acquisition',
      component: CaptureAcquisitionPage,
      meta: {
        navKey: 'capture-acquisition',
        titleKey: 'pages.captureAcquisition.title',
        subtitleKey: 'pages.captureAcquisition.subtitle',
      },
    },
    {
      path: '/image-processing',
      name: 'image-processing',
      component: ImageProcessingPage,
      meta: {
        navKey: 'image-processing',
        titleKey: 'pages.imageProcessing.title',
        subtitleKey: 'pages.imageProcessing.subtitle',
      },
    },
    {
      path: '/image-enhancement',
      name: 'image-enhancement',
      component: ImageEnhancementPage,
      meta: {
        navKey: 'image-enhancement',
        titleKey: 'pages.imageEnhancement.title',
        subtitleKey: 'pages.imageEnhancement.subtitle',
      },
    },
    {
      path: '/ocr-recognition',
      name: 'ocr-recognition',
      component: OcrRecognitionPage,
      meta: {
        navKey: 'ocr-recognition',
        titleKey: 'pages.ocrRecognition.title',
        subtitleKey: 'pages.ocrRecognition.subtitle',
      },
    },
    {
      path: '/file-conversion',
      name: 'file-conversion',
      component: FileConversionPage,
      meta: {
        navKey: 'file-conversion',
        titleKey: 'pages.fileConversion.title',
        subtitleKey: 'pages.fileConversion.subtitle',
      },
    },
    {
      path: '/sane-scanning',
      name: 'sane-scanning',
      component: SaneScanningPage,
      meta: {
        navKey: 'sane-scanning',
        titleKey: 'pages.saneScanning.title',
        subtitleKey: 'pages.saneScanning.subtitle',
      },
    },
    {
      path: '/events-results',
      name: 'events-results',
      component: EventsResultsPage,
      meta: {
        navKey: 'events-results',
        titleKey: 'pages.eventsResults.title',
        subtitleKey: 'pages.eventsResults.subtitle',
      },
    },
  ],
});
