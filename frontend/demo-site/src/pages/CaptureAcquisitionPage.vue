<template>
  <div class="space-y-6">
    <div class="grid gap-4 xl:grid-cols-4">
      <MetricCard v-for="metric in captureMetrics" :key="metric.label" v-bind="metric" />
    </div>

    <div class="grid gap-6 xl:grid-cols-[minmax(0,1.35fr)_minmax(360px,0.65fr)]">
      <div class="space-y-6">
        <SectionPanel :title="t('pages.captureAcquisition.capturePreview')" :description="t('pages.captureAcquisition.previewPanelDescription')">
          <div class="space-y-5">
            <PreviewStage
              :eyebrow="t('pages.captureAcquisition.liveDeviceStream')"
              :title="previewTitle"
              :description="previewDescription"
              :metrics="[]"
              :badge-label="deviceVideoState.videoState"
              :badge-tone="videoBadgeTone"
              stage-size="large"
              :show-text-overlay="false"
              variant="video"
              @video-canvas="handleVideoCanvas"
            >
              <template v-if="captureConfig.pageProcessing === 'selected_area'" #video-overlay>
                <div
                  class="absolute inset-0 z-10 cursor-crosshair touch-none"
                  @pointerdown="handleSelectedAreaPointerDown"
                  @pointermove="handleSelectedAreaPointerMove"
                  @pointerup="handleSelectedAreaPointerUp"
                  @pointercancel="handleSelectedAreaPointerCancel"
                >
                  <div
                    v-if="selectedAreaOverlayRect"
                    class="absolute border-2 border-cyan-300 bg-cyan-300/10 shadow-[0_0_0_9999px_rgba(15,23,42,0.28)]"
                    :style="{
                      left: `${selectedAreaOverlayRect.x}px`,
                      top: `${selectedAreaOverlayRect.y}px`,
                      width: `${selectedAreaOverlayRect.width}px`,
                      height: `${selectedAreaOverlayRect.height}px`,
                    }"
                  />
                  <svg class="pointer-events-none absolute inset-0 h-full w-full">
                    <polygon
                      v-if="selectedAreaOverlayPolygon"
                      :points="selectedAreaOverlayPolygon"
                      fill="rgba(34, 211, 238, 0.12)"
                      stroke="#22d3ee"
                      stroke-width="2"
                    />
                  </svg>
                  <span
                    v-for="(point, index) in selectedAreaOverlayPoints"
                    :key="index"
                    class="pointer-events-none absolute h-3 w-3 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-white bg-cyan-400 shadow"
                    :style="{ left: `${point.x}px`, top: `${point.y}px` }"
                  />
                </div>
              </template>
            </PreviewStage>

            <div class="grid gap-4 lg:grid-cols-[minmax(0,1fr)_260px]">
              <div class="rounded-2xl border border-slate-200 bg-white/80 p-4 shadow-sm">
                <div class="mb-3 flex items-center justify-between gap-3">
                  <div>
                    <p class="text-sm font-semibold text-slate-900">{{ t('pages.captureAcquisition.devices') }}</p>
                    <p class="text-xs text-slate-500">{{ t('pages.captureAcquisition.availableCount', { count: deviceInventoryState.devices.length }) }}</p>
                  </div>
                  <button
                    type="button"
                    class="rounded-full border border-slate-200 px-3 py-1.5 text-xs font-semibold text-slate-600 transition hover:border-cyan-300 hover:text-cyan-700 disabled:cursor-not-allowed disabled:opacity-50"
                    :disabled="!canLoadDevices || deviceInventoryState.loading"
                    @click="loadDeviceInventory"
                  >
                    device.list
                  </button>
                </div>

                <div class="max-h-48 overflow-auto rounded-xl border border-slate-100">
                  <table class="min-w-full divide-y divide-slate-100 text-left text-sm">
                    <thead class="bg-slate-50 text-xs uppercase tracking-wide text-slate-500">
                      <tr>
                        <th class="px-3 py-2 font-semibold">{{ t('labels.selectedDevice') }}</th>
                        <th class="px-3 py-2 font-semibold">{{ t('labels.model') }}</th>
                        <th class="px-3 py-2 font-semibold">{{ t('labels.status') }}</th>
                      </tr>
                    </thead>
                    <tbody class="divide-y divide-slate-100 bg-white">
                      <tr
                        v-for="device in deviceInventoryState.devices"
                        :key="device.device_id"
                        class="cursor-pointer transition hover:bg-cyan-50/60"
                        :class="device.device_id === deviceVideoState.selectedDeviceId ? 'bg-cyan-50' : ''"
                        @click="selectDevice(device.device_id)"
                      >
                        <td class="px-3 py-2 font-mono text-xs text-slate-800">{{ device.device_id }}</td>
                        <td class="px-3 py-2 text-slate-700">{{ device.model || '-' }}</td>
                        <td class="px-3 py-2">
                          <StatusPill :label="device.status || 'unknown'" :tone="device.status === 'online' ? 'success' : 'neutral'" />
                        </td>
                      </tr>
                      <tr v-if="deviceInventoryState.devices.length === 0">
                        <td colspan="3" class="px-3 py-8 text-center text-sm text-slate-500">{{ t('pages.captureAcquisition.noDeviceData') }}</td>
                      </tr>
                    </tbody>
                  </table>
                </div>
              </div>

              <div class="rounded-2xl border border-slate-200 bg-white/80 p-4 shadow-sm">
                <p class="mb-3 text-sm font-semibold text-slate-900">{{ t('sections.deviceControl') }}</p>
                <div class="space-y-3">
                  <label class="block text-xs font-semibold uppercase tracking-wide text-slate-500">
                    {{ t('labels.resolution') }}
                    <select
                      class="mt-1 w-full rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition focus:border-cyan-400 focus:ring-2 focus:ring-cyan-100 disabled:cursor-not-allowed disabled:opacity-60"
                      :value="deviceVideoState.selectedResolutionKey"
                      :disabled="captureResolutionOptions.length === 0 || deviceVideoState.features.image_transfer_protocol"
                      @change="handleResolutionChange"
                    >
                      <option v-if="!deviceVideoState.features.image_transfer_protocol" value="">{{ t('pages.captureAcquisition.autoResolution') }}</option>
                      <option v-for="resolution in captureResolutionOptions" :key="resolutionKey(resolution)" :value="resolutionKey(resolution)">
                        {{ resolution.width }} x {{ resolution.height }} @ {{ resolution.fps }} fps
                      </option>
                    </select>
                    <span v-if="deviceVideoState.features.image_transfer_protocol" class="mt-2 block text-xs font-medium normal-case tracking-normal text-amber-600">
                      {{ t('pages.captureAcquisition.imageTransferCaptureResolutionLocked') }}
                    </span>
                  </label>

                  <div class="grid grid-cols-2 gap-2">
                    <button type="button" class="control-button" :disabled="!deviceVideoState.selectedDeviceId" @click="loadDeviceDetail">
                      device.get
                    </button>
                    <button
                      type="button"
                      class="control-button"
                      :disabled="!deviceVideoState.selectedDeviceId || deviceVideoState.opened || Boolean(deviceVideoState.streamId) || deviceVideoState.openState === 'running'"
                      @click="openCaptureDevice"
                    >
                      device.open
                    </button>
                    <button
                      type="button"
                      class="control-button"
                      :disabled="!deviceVideoState.opened || Boolean(deviceVideoState.streamId) || deviceVideoState.startState === 'running'"
                      @click="startCaptureVideo"
                    >
                      video.start
                    </button>
                    <button type="button" class="control-button" :disabled="!deviceVideoState.streamId" @click="stopVideo">
                      video.stop
                    </button>
                    <button
                      type="button"
                      class="control-button col-span-2"
                      :disabled="!deviceVideoState.selectedDeviceId || (!deviceVideoState.opened && !deviceVideoState.streamId) || deviceVideoState.closeState === 'running'"
                      @click="closeCaptureDevice"
                    >
                      device.close
                    </button>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </SectionPanel>

        <SectionPanel :title="t('pages.captureAcquisition.currentCaptureFiles')" :description="t('pages.captureAcquisition.currentCaptureFilesDescription')">
          <template #actions>
            <button
              type="button"
              class="rounded-full border border-slate-200 px-3 py-1.5 text-xs font-semibold text-slate-600 transition hover:border-rose-300 hover:text-rose-700 disabled:cursor-not-allowed disabled:opacity-50"
              :disabled="captureResults.length === 0"
              @click="clearCaptureResults"
            >
              Clear
            </button>
          </template>
          <div v-if="capturePreviewCards.length > 0" class="mb-4 grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
            <article
              v-for="card in capturePreviewCards"
              :key="card.id"
              class="overflow-hidden rounded-2xl border border-slate-200 bg-white shadow-sm"
            >
              <div class="flex aspect-[4/3] items-center justify-center bg-slate-100">
                <img
                  v-if="card.thumbnailObjectUrl"
                  :src="card.thumbnailObjectUrl"
                  :alt="card.filename"
                  class="h-full w-full cursor-zoom-in object-contain"
                  @click="openCapturePreviewViewer(card)"
                />
                <div v-else class="px-4 text-center text-sm text-slate-500">
                  <p class="font-semibold text-slate-700">{{ thumbnailStateLabel(card) }}</p>
                  <p v-if="card.thumbnailError" class="mt-1 text-xs text-rose-500">{{ card.thumbnailError }}</p>
                </div>
              </div>
              <div class="space-y-2 p-3">
                <div class="flex items-start justify-between gap-3">
                  <p class="min-w-0 truncate text-sm font-semibold text-slate-900">{{ card.filename }}</p>
                  <StatusPill :label="card.status" :tone="captureStatusTone(card.status)" />
                </div>
                <div class="grid gap-1 text-xs text-slate-500">
                  <p v-if="card.thumbnailAssetKind" class="truncate">{{ card.thumbnailAssetKind }}</p>
                  <p class="truncate">{{ card.resolution }}</p>
                  <p>{{ card.createdAt }}</p>
                </div>
              </div>
            </article>
          </div>
          <DataTableCard :columns="captureColumns" :rows="captureRows" :empty-label="t('pages.captureAcquisition.noCaptures')" />
        </SectionPanel>
      </div>

      <div class="space-y-6">
        <SectionPanel :title="t('pages.captureAcquisition.captureProfile')" :description="t('pages.captureAcquisition.captureProfileDescription')">
          <div class="space-y-5">
            <div>
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.pageProcessing') }}</p>
              <div class="grid gap-2">
                <button
                  v-for="option in pageProcessingOptions"
                  :key="option.value"
                  type="button"
                  class="option-button"
                  :class="captureConfig.pageProcessing === option.value ? 'option-button-active' : ''"
                  @click="captureConfig.pageProcessing = option.value"
                >
                  <span class="font-semibold">{{ option.label }}</span>
                  <span class="text-xs text-slate-500">{{ option.description }}</span>
                </button>
              </div>
            </div>

            <div v-if="captureConfig.pageProcessing === 'single_page'">
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.singlePageOptions') }}</p>
              <div class="space-y-2">
                <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                  <span>{{ t('pages.captureAcquisition.realtimeDetectRects') }}</span>
                  <input v-model="captureConfig.singlePageRealtimeDetectRects" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
                </label>
                <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                  <span>{{ t('pages.captureAcquisition.cropBorder') }}</span>
                  <input v-model="captureConfig.singlePageCropBorder" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
                </label>
                <div v-if="captureConfig.singlePageCropBorder" class="grid grid-cols-2 gap-2">
                  <label class="block text-xs font-semibold uppercase tracking-wide text-slate-500">
                    {{ t('pages.captureAcquisition.cropBorderWidth') }}
                    <input v-model.number="captureConfig.singlePageCropBorderWidth" type="number" min="-100" max="100" class="mt-1 w-full rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition focus:border-cyan-400 focus:ring-2 focus:ring-cyan-100" />
                  </label>
                  <label class="block text-xs font-semibold uppercase tracking-wide text-slate-500">
                    {{ t('pages.captureAcquisition.cropBorderHeight') }}
                    <input v-model.number="captureConfig.singlePageCropBorderHeight" type="number" min="-100" max="100" class="mt-1 w-full rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition focus:border-cyan-400 focus:ring-2 focus:ring-cyan-100" />
                  </label>
                </div>
                <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                  <span>{{ t('pages.captureAcquisition.idCardRoundCorner') }}</span>
                  <input v-model="captureConfig.singlePageIdCardRoundCorner" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
                </label>
                <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                  <span>{{ t('pages.captureAcquisition.autoRotate') }}</span>
                  <input v-model="captureConfig.singlePageAutoRotate" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
                </label>
                <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                  <span>{{ t('pages.captureAcquisition.smartBlackEdgeOptimize') }}</span>
                  <input v-model="captureConfig.singlePageSmartBlackEdgeOptimize" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
                </label>
                <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                  <span>{{ t('pages.captureAcquisition.multiTargetPaging') }}</span>
                  <input v-model="captureConfig.singlePageMultiTargetPaging" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
                </label>
              </div>
            </div>

            <div v-if="captureConfig.pageProcessing === 'curved_book'" class="space-y-2">
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.curvedBookOptions') }}</p>
              <p class="rounded-xl border border-amber-200 bg-amber-50 px-3 py-2 text-xs font-medium text-amber-800">
                {{ t('pages.captureAcquisition.curvedBookLaserNotice') }}
              </p>
              <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                <span>{{ t('pages.captureAcquisition.removeFinger') }}</span>
                <input v-model="captureConfig.curvedBookRemoveFinger" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
              </label>
              <label v-if="captureConfig.curvedBookRemoveFinger" class="block text-xs font-semibold uppercase tracking-wide text-slate-500">
                {{ t('pages.captureAcquisition.fingerType') }}
                <select v-model="captureConfig.curvedBookFingerType" class="mt-1 w-full rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition focus:border-cyan-400 focus:ring-2 focus:ring-cyan-100">
                  <option value="with_sleeve">{{ t('pages.captureAcquisition.fingerWithSleeve') }}</option>
                  <option value="without_sleeve">{{ t('pages.captureAcquisition.fingerWithoutSleeve') }}</option>
                </select>
              </label>
              <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                <span>{{ t('pages.captureAcquisition.smartPaging') }}</span>
                <input v-model="captureConfig.curvedBookSmartPaging" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
              </label>
              <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                <span>{{ t('pages.captureAcquisition.cropBorder') }}</span>
                <input v-model="captureConfig.curvedBookCropBorder" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
              </label>
              <div v-if="captureConfig.curvedBookCropBorder" class="grid grid-cols-2 gap-2">
                <label class="block text-xs font-semibold uppercase tracking-wide text-slate-500">
                  {{ t('pages.captureAcquisition.cropBorderWidth') }}
                  <input v-model.number="captureConfig.curvedBookCropBorderWidth" type="number" min="-100" max="100" class="mt-1 w-full rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition focus:border-cyan-400 focus:ring-2 focus:ring-cyan-100" />
                </label>
                <label class="block text-xs font-semibold uppercase tracking-wide text-slate-500">
                  {{ t('pages.captureAcquisition.cropBorderHeight') }}
                  <input v-model.number="captureConfig.curvedBookCropBorderHeight" type="number" min="-100" max="100" class="mt-1 w-full rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition focus:border-cyan-400 focus:ring-2 focus:ring-cyan-100" />
                </label>
              </div>
              <label class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                <span>{{ t('pages.captureAcquisition.autoComplete') }}</span>
                <input v-model="captureConfig.curvedBookAutoComplete" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
              </label>
            </div>

            <div v-if="captureConfig.pageProcessing === 'selected_area'" class="space-y-3">
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.selectedAreaOptions') }}</p>
              <div class="grid grid-cols-2 gap-2">
                <button
                  type="button"
                  class="option-button"
                  :class="captureConfig.selectedAreaMode === 'rectangle' ? 'option-button-active' : ''"
                  @click="setSelectedAreaMode('rectangle')"
                >
                  <span class="font-semibold">{{ t('pages.captureAcquisition.selectedAreaRectangle') }}</span>
                </button>
                <button
                  type="button"
                  class="option-button"
                  :class="captureConfig.selectedAreaMode === 'points' ? 'option-button-active' : ''"
                  @click="setSelectedAreaMode('points')"
                >
                  <span class="font-semibold">{{ t('pages.captureAcquisition.selectedAreaPoints') }}</span>
                </button>
              </div>
              <div class="flex items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                <span>{{ selectedAreaStatusLabel }}</span>
                <button type="button" class="rounded-full border border-slate-200 px-3 py-1 text-xs font-semibold text-slate-600 transition hover:border-cyan-300 hover:text-cyan-700" @click="clearSelectedArea">
                  {{ t('pages.captureAcquisition.clearSelectedArea') }}
                </button>
              </div>
            </div>

            <div>
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.colorMode') }}</p>
              <div class="grid gap-2 sm:grid-cols-2">
                <button
                  v-for="option in colorModeOptions"
                  :key="option.value"
                  type="button"
                  class="option-button"
                  :class="captureConfig.colorMode === option.value ? 'option-button-active' : ''"
                  @click="captureConfig.colorMode = option.value"
                >
                  <span class="font-semibold">{{ option.label }}</span>
                  <span class="text-xs text-slate-500">{{ option.description }}</span>
                </button>
              </div>
            </div>

            <div>
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.outputFormat') }}</p>
              <div class="grid grid-cols-3 gap-2">
                <button
                  v-for="format in outputFormats"
                  :key="format"
                  type="button"
                  class="rounded-xl border px-3 py-2 text-sm font-semibold uppercase transition"
                  :class="
                    captureConfig.outputFormat === format
                      ? 'border-cyan-400 bg-cyan-50 text-cyan-800'
                      : 'border-slate-200 bg-white text-slate-600 hover:border-cyan-200'
                  "
                  @click="captureConfig.outputFormat = format"
                >
                  {{ format }}
                </button>
              </div>
            </div>

            <div>
              <p class="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">{{ t('pages.captureAcquisition.thumbnailOutputs') }}</p>
              <div class="space-y-2">
                <label v-for="item in thumbnailOptions" :key="item.key" class="flex cursor-pointer items-center justify-between gap-3 rounded-xl border border-slate-200 bg-white px-3 py-2 text-sm text-slate-700">
                  <span>{{ item.label }}</span>
                  <input v-model="captureConfig.thumbnails[item.key]" type="checkbox" class="h-4 w-4 rounded border-slate-300 text-cyan-600 focus:ring-cyan-500" />
                </label>
              </div>
            </div>

            <ImageEnhanceWorkflowPicker
              v-model="selectedEnhanceWorkflowId"
              :title="t('pages.captureAcquisition.enhanceWorkflow')"
              :empty-label="t('pages.captureAcquisition.enhanceWorkflowDescription')"
              :none-label="t('pages.captureAcquisition.noEnhanceWorkflow')"
              :clear-label="t('pages.captureAcquisition.clearEnhanceWorkflow')"
              :refresh-label="t('pages.captureAcquisition.refreshEnhanceWorkflows')"
              :loading-label="t('pages.captureAcquisition.loadingEnhanceWorkflows')"
              @selected="handleEnhanceWorkflowSelected"
            />

            <button
              type="button"
              class="w-full rounded-xl bg-cyan-600 px-4 py-3 text-sm font-semibold text-white transition hover:bg-cyan-700 disabled:cursor-not-allowed disabled:bg-slate-300"
              :disabled="!canCapture"
              @click="handleCapture"
            >
              {{ t('pages.captureAcquisition.capture') }}
            </button>
          </div>
        </SectionPanel>

        <JsonPanel :title="t('pages.captureAcquisition.profileJson')" :caption="t('pages.captureAcquisition.initializationPayload')" :payload="profilePayload" />

        <SectionPanel :title="t('sections.realtimeEvents')" :description="t('pages.captureAcquisition.realtimeEventsDescription')">
          <div class="max-h-72 overflow-y-auto pr-1">
            <EventTimeline v-if="eventItems.length > 0" :items="eventItems" />
            <p v-else class="rounded-2xl border border-dashed border-slate-200 px-4 py-8 text-center text-sm text-slate-500">
              {{ t('pages.captureAcquisition.noRealtimeEvents') }}
            </p>
          </div>
        </SectionPanel>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, reactive, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import DataTableCard from '../components/blocks/DataTableCard.vue';
import EventTimeline from '../components/blocks/EventTimeline.vue';
import ImageEnhanceWorkflowPicker from '../components/blocks/ImageEnhanceWorkflowPicker.vue';
import JsonPanel from '../components/blocks/JsonPanel.vue';
import PreviewStage from '../components/blocks/PreviewStage.vue';
import SectionPanel from '../components/blocks/SectionPanel.vue';
import MetricCard from '../components/cards/MetricCard.vue';
import StatusPill from '../components/cards/StatusPill.vue';
import { authSessionState, onCommandEvent, sendBoundCommand } from '../services/auth-session';
import { deviceInventoryState, loadDeviceInventory, resetDeviceInventory } from '../services/device-inventory';
import {
  attachVideoCanvas,
  applyCaptureAcquisitionResolution,
  closeSelectedDevice,
  type DeviceResolution,
  deviceVideoState,
  loadDeviceDetail,
  openSelectedDevice,
  resetDeviceVideo,
  resolutionKey,
  selectDevice,
  setSelectedResolution,
  setVideoProfile,
  startVideo,
  stopVideo,
} from '../services/device-video';
import type { EnhancePipeline, EnhanceWorkflow } from '../services/image-enhance-workflows';
import { openImageViewer } from '../services/image-viewer';
import { recordRuntimeEvent, runtimeRecordState } from '../services/runtime-records';
import type { TableColumn, TableRow, TimelineItem, Tone } from '../types/demo';

type PageProcessingMode = 'single_page' | 'curved_book' | 'selected_area' | 'keep_original';
type ColorMode = 'auto_optimize' | 'color_enhance' | 'black_white' | 'grayscale' | 'white_paper_seal' | 'certificate' | 'ancient' | 'no_optimize';
type OutputFormat = 'jpg' | 'png' | 'tiff';
type ThumbnailKey = 'original' | 'pageProcessed' | 'colorProcessed' | 'final';
type ThumbnailState = 'idle' | 'loading' | 'ready' | 'error' | 'unavailable';
type SelectedAreaMode = 'rectangle' | 'points';

interface ImagePoint {
  x: number;
  y: number;
}

interface Rect4P {
  left_top: ImagePoint;
  right_top: ImagePoint;
  right_down: ImagePoint;
  left_down: ImagePoint;
}

interface OverlayRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

interface CapturePreview {
  id: string;
  assetId: string;
  kind: string;
  url: string;
  objectUrl: string;
  state: ThumbnailState;
  error: string;
}

interface CaptureResult {
  id: string;
  taskId: string;
  filename: string;
  format: OutputFormat;
  pageProcessing: string;
  colorMode: string;
  thumbnails: string;
  resolution: string;
  createdAt: string;
  status: string;
  path: string;
  assetUrl: string;
  downloadUrl: string;
  assets: CaptureAssetPayload[];
  previews: CapturePreview[];
  thumbnailAssetId: string;
  thumbnailAssetKind: string;
  thumbnailUrl: string;
  thumbnailObjectUrl: string;
  thumbnailState: ThumbnailState;
  thumbnailError: string;
}

interface CapturePreviewCard {
  id: string;
  filename: string;
  status: string;
  resolution: string;
  createdAt: string;
  thumbnailAssetKind: string;
  thumbnailObjectUrl: string;
  thumbnailState: ThumbnailState;
  thumbnailError: string;
}

const { t } = useI18n();

const pageProcessingOptions = computed<Array<{ value: PageProcessingMode; label: string; description: string }>>(() => [
  {
    value: 'single_page',
    label: t('pages.captureAcquisition.pageSingle'),
    description: t('pages.captureAcquisition.pageSingleDescription'),
  },
  {
    value: 'curved_book',
    label: t('pages.captureAcquisition.pageCurved'),
    description: t('pages.captureAcquisition.pageCurvedDescription'),
  },
  {
    value: 'selected_area',
    label: t('pages.captureAcquisition.pageSelectedArea'),
    description: t('pages.captureAcquisition.pageSelectedAreaDescription'),
  },
  {
    value: 'keep_original',
    label: t('pages.captureAcquisition.pageKeepOriginal'),
    description: t('pages.captureAcquisition.pageKeepOriginalDescription'),
  },
]);

const colorModeOptions = computed<Array<{ value: ColorMode; label: string; description: string }>>(() => [
  {
    value: 'auto_optimize',
    label: t('pages.captureAcquisition.colorAutoOptimize'),
    description: t('pages.captureAcquisition.colorAutoOptimizeDescription'),
  },
  {
    value: 'color_enhance',
    label: t('pages.captureAcquisition.colorFullColor'),
    description: t('pages.captureAcquisition.colorFullColorDescription'),
  },
  {
    value: 'black_white',
    label: t('pages.captureAcquisition.colorBlackWhite'),
    description: t('pages.captureAcquisition.colorBlackWhiteDescription'),
  },
  {
    value: 'grayscale',
    label: t('pages.captureAcquisition.colorGrayscale'),
    description: t('pages.captureAcquisition.colorGrayscaleDescription'),
  },
  {
    value: 'white_paper_seal',
    label: t('pages.captureAcquisition.colorWhitePaperSeal'),
    description: t('pages.captureAcquisition.colorWhitePaperSealDescription'),
  },
  {
    value: 'certificate',
    label: t('pages.captureAcquisition.colorCertificate'),
    description: t('pages.captureAcquisition.colorCertificateDescription'),
  },
  {
    value: 'ancient',
    label: t('pages.captureAcquisition.colorAncient'),
    description: t('pages.captureAcquisition.colorAncientDescription'),
  },
  {
    value: 'no_optimize',
    label: t('pages.captureAcquisition.colorNoOptimize'),
    description: t('pages.captureAcquisition.colorNoOptimizeDescription'),
  },
]);

const outputFormats: OutputFormat[] = ['jpg', 'png', 'tiff'];

const thumbnailOptions = computed<Array<{ key: ThumbnailKey; label: string }>>(() => [
  { key: 'original', label: t('pages.captureAcquisition.originalThumbnail') },
  { key: 'pageProcessed', label: t('pages.captureAcquisition.pageProcessedThumbnail') },
  { key: 'colorProcessed', label: t('pages.captureAcquisition.colorProcessedThumbnail') },
  { key: 'final', label: t('pages.captureAcquisition.finalThumbnail') },
]);

const captureConfig = reactive({
  pageProcessing: 'single_page' as PageProcessingMode,
  singlePageRealtimeDetectRects: true,
  singlePageCropBorder: false,
  singlePageCropBorderWidth: 0,
  singlePageCropBorderHeight: 0,
  singlePageIdCardRoundCorner: false,
  singlePageAutoRotate: false,
  singlePageSmartBlackEdgeOptimize: true,
  singlePageMultiTargetPaging: false,
  curvedBookRemoveFinger: true,
  curvedBookFingerType: 'with_sleeve' as 'with_sleeve' | 'without_sleeve',
  curvedBookSmartPaging: true,
  curvedBookCropBorder: false,
  curvedBookCropBorderWidth: 0,
  curvedBookCropBorderHeight: 0,
  curvedBookAutoComplete: false,
  selectedAreaMode: 'rectangle' as SelectedAreaMode,
  selectedAreaPoints: [] as ImagePoint[],
  colorMode: 'color_enhance' as ColorMode,
  outputFormat: 'jpg' as OutputFormat,
  thumbnails: {
    original: true,
    pageProcessed: true,
    colorProcessed: false,
    final: true,
  } as Record<ThumbnailKey, boolean>,
});

const profileRevision = ref(1);
const profileInitializedAt = ref(timeLabel());
const selectedEnhanceWorkflowId = ref('');
const selectedEnhanceWorkflow = ref<EnhanceWorkflow | null>(null);
const captureResults = ref<CaptureResult[]>([]);
const activeCaptureTasks = new Set<string>();
const thumbnailRequests = new Map<string, number>();
let commandEventUnsubscribe: (() => void) | null = null;
const suppressAutoOpen = ref(false);
const captureVideoCanvas = ref<HTMLCanvasElement | null>(null);
const selectedAreaDragStart = ref<ImagePoint | null>(null);
const selectedAreaDragging = ref(false);

const canLoadDevices = computed(() => authSessionState.commandState === 'success' && Boolean(authSessionState.sessionToken));
const canCapture = computed(() =>
  Boolean(deviceVideoState.selectedDeviceId && deviceVideoState.opened && deviceVideoState.streamId && deviceVideoState.videoState === 'success') &&
  (captureConfig.pageProcessing !== 'selected_area' || Boolean(selectedAreaRect.value)),
);

const captureResolutionOptions = computed<DeviceResolution[]>(() => {
  if (!deviceVideoState.features.image_transfer_protocol) {
    return deviceVideoState.resolutions;
  }
  const lockedResolution = deviceVideoState.resolutions.find((resolution) => resolution.width === 1536 && resolution.height === 1152);
  return lockedResolution
    ? [lockedResolution]
    : [{ width: 1536, height: 1152, real_width: 1536, real_height: 1152, fps: 15, pixel_format: 'mjpeg', is_default: false }];
});

const selectedResolution = computed(() =>
  deviceVideoState.resolutions.find((resolution) => resolutionKey(resolution) === deviceVideoState.selectedResolutionKey),
);

const selectedResolutionLabel = computed(() => {
  if (!selectedResolution.value) {
    return t('pages.captureAcquisition.autoResolution');
  }
  return `${selectedResolution.value.width} x ${selectedResolution.value.height} @ ${selectedResolution.value.fps} fps`;
});

const previewTitle = computed(() => deviceVideoState.selectedDeviceId || t('pages.captureAcquisition.selectConnectedDevice'));
const previewDescription = computed(() => {
  if (!canLoadDevices.value) {
    return t('pages.captureAcquisition.authorizeBeforeDevices');
  }
  if (!deviceVideoState.selectedDeviceId) {
    return t('pages.captureAcquisition.chooseDeviceDescription');
  }
  return t('pages.captureAcquisition.resolutionValue', { value: selectedResolutionLabel.value });
});

const videoBadgeTone = computed(() => {
  if (deviceVideoState.videoState === 'success') {
    return 'success';
  }
  if (deviceVideoState.videoState === 'error') {
    return 'danger';
  }
  if (deviceVideoState.videoState === 'running') {
    return 'warning';
  }
  return 'neutral';
});

const activePageProcessing = computed(() => pageProcessingOptions.value.find((item) => item.value === captureConfig.pageProcessing));
const activeColorMode = computed(() => colorModeOptions.value.find((item) => item.value === captureConfig.colorMode));

const enabledThumbnailLabels = computed(() =>
  thumbnailOptions.value.filter((item) => captureConfig.thumbnails[item.key]).map((item) => item.label),
);

const selectedAreaRect = computed<Rect4P | null>(() => {
  if (captureConfig.selectedAreaPoints.length < 4) {
    return null;
  }
  return normalizeAreaPoints(captureConfig.selectedAreaPoints.slice(0, 4));
});

const selectedAreaSource = computed(() => {
  const canvas = captureVideoCanvas.value;
  return canvas && canvas.width > 0 && canvas.height > 0
    ? { width: canvas.width, height: canvas.height }
    : { width: 0, height: 0 };
});

const selectedAreaOverlayPoints = computed<ImagePoint[]>(() =>
  captureConfig.selectedAreaPoints
    .map((point) => imagePointToOverlayPoint(point))
    .filter((point): point is ImagePoint => Boolean(point)),
);

const selectedAreaOverlayPolygon = computed(() => {
  const rect = selectedAreaRect.value;
  if (!rect) {
    return '';
  }
  return [rect.left_top, rect.right_top, rect.right_down, rect.left_down]
    .map((point) => imagePointToOverlayPoint(point))
    .filter((point): point is ImagePoint => Boolean(point))
    .map((point) => `${point.x},${point.y}`)
    .join(' ');
});

const selectedAreaOverlayRect = computed<OverlayRect | null>(() => {
  if (captureConfig.selectedAreaMode !== 'rectangle' || captureConfig.selectedAreaPoints.length !== 2) {
    return null;
  }
  const points = captureConfig.selectedAreaPoints
    .slice(0, 2)
    .map((point) => imagePointToOverlayPoint(point))
    .filter((point): point is ImagePoint => Boolean(point));
  if (points.length !== 2) {
    return null;
  }
  const x = Math.min(points[0].x, points[1].x);
  const y = Math.min(points[0].y, points[1].y);
  return {
    x,
    y,
    width: Math.abs(points[1].x - points[0].x),
    height: Math.abs(points[1].y - points[0].y),
  };
});

const selectedAreaStatusLabel = computed(() => {
  if (selectedAreaRect.value) {
    return t('pages.captureAcquisition.selectedAreaReady');
  }
  if (captureConfig.selectedAreaMode === 'points') {
    return t('pages.captureAcquisition.selectedAreaPointProgress', { count: captureConfig.selectedAreaPoints.length });
  }
  return t('pages.captureAcquisition.selectedAreaPending');
});

const captureMetrics = computed(() => [
  {
    label: t('pages.captureAcquisition.deviceState'),
    value: deviceVideoState.opened
      ? t('pages.captureAcquisition.opened')
      : deviceVideoState.selectedDeviceId
        ? t('pages.captureAcquisition.selected')
        : t('pages.captureAcquisition.noDevice'),
    detail: deviceVideoState.selectedDeviceId || t('pages.captureAcquisition.deviceCount', { count: deviceInventoryState.devices.length }),
    tone: deviceVideoState.opened ? 'success' : 'neutral',
  },
  {
    label: t('labels.resolution'),
    value: selectedResolutionLabel.value,
    detail: t('pages.captureAcquisition.resolutionOptionCount', { count: deviceVideoState.resolutions.length }),
    tone: selectedResolution.value ? 'success' : 'neutral',
  },
  {
    label: t('pages.captureAcquisition.profileUpdatedAt'),
    value: profileInitializedAt.value,
    detail: t('pages.captureAcquisition.profileUpdatedAtDescription'),
    tone: 'warning',
  },
  {
    label: t('pages.captureAcquisition.sessionFiles'),
    value: String(captureResults.value.length),
    detail: captureConfig.outputFormat.toUpperCase(),
    tone: captureResults.value.length > 0 ? 'success' : 'neutral',
  },
]);

const captureProfile = computed(() => ({
  profile_version: 'capture.profile.v1',
  revision: profileRevision.value,
  initialized_at: profileInitializedAt.value,
  device: {
    device_id: deviceVideoState.selectedDeviceId || null,
    resolution: selectedResolution.value
      ? {
          width: selectedResolution.value.width,
          height: selectedResolution.value.height,
          fps: selectedResolution.value.fps,
        }
      : null,
  },
  capture: {
    page_processing: captureConfig.pageProcessing,
    single_page: {
      realtime_detect_rects: captureConfig.singlePageRealtimeDetectRects,
      crop_border: {
        enabled: captureConfig.singlePageCropBorder,
        width: clampCropMargin(captureConfig.singlePageCropBorderWidth),
        height: clampCropMargin(captureConfig.singlePageCropBorderHeight),
      },
      id_card_round_corner: captureConfig.singlePageIdCardRoundCorner,
      auto_rotate: captureConfig.singlePageAutoRotate,
      smart_black_edge_optimize: captureConfig.singlePageSmartBlackEdgeOptimize,
      multi_target_paging: captureConfig.singlePageMultiTargetPaging,
    },
    curved_book: {
      remove_finger: {
        enabled: captureConfig.curvedBookRemoveFinger,
        finger_type: captureConfig.curvedBookFingerType,
      },
      smart_paging: captureConfig.curvedBookSmartPaging,
      crop_border: {
        enabled: captureConfig.curvedBookCropBorder,
        width: clampCropMargin(captureConfig.curvedBookCropBorderWidth),
        height: clampCropMargin(captureConfig.curvedBookCropBorderHeight),
      },
      auto_complete: captureConfig.curvedBookAutoComplete,
    },
    selected_area: selectedAreaRect.value
      ? {
          points: selectedAreaRect.value,
          source: selectedAreaSource.value,
        }
      : null,
    color_mode: captureConfig.colorMode,
  },
  output: {
    format: captureConfig.outputFormat,
    thumbnails: {
      original: captureConfig.thumbnails.original,
      page_processed: captureConfig.thumbnails.pageProcessed,
      color_processed: captureConfig.thumbnails.colorProcessed,
      final: captureConfig.thumbnails.final,
    },
  },
}));

const profilePayload = computed(() => JSON.stringify(captureProfile.value, null, 2));

const captureColumns = computed<TableColumn[]>(() => [
  { key: 'filename', label: t('pages.captureAcquisition.file') },
  { key: 'format', label: t('labels.format') },
  { key: 'pageProcessing', label: t('pages.captureAcquisition.page') },
  { key: 'colorMode', label: t('pages.captureAcquisition.color') },
  { key: 'thumbnails', label: t('pages.captureAcquisition.thumbnails') },
  { key: 'resolution', label: t('labels.resolution') },
  { key: 'createdAt', label: t('labels.time') },
  { key: 'status', label: t('labels.status') },
]);

const captureRows = computed<TableRow[]>(() =>
  captureResults.value.map((result) => ({
    id: result.id,
    cells: {
      filename: result.filename,
      format: result.format.toUpperCase(),
      pageProcessing: result.pageProcessing,
      colorMode: result.colorMode,
      thumbnails: result.thumbnails,
      resolution: result.resolution,
      createdAt: result.createdAt,
      status: result.status,
    },
  })),
);

const capturePreviewCards = computed<CapturePreviewCard[]>(() =>
  captureResults.value.flatMap((result) => {
    if (result.previews.length === 0) {
      return [
        {
          id: result.id,
          filename: result.filename,
          status: result.status,
          resolution: result.resolution,
          createdAt: result.createdAt,
          thumbnailAssetKind: result.thumbnailAssetKind,
          thumbnailObjectUrl: result.thumbnailObjectUrl,
          thumbnailState: result.thumbnailState,
          thumbnailError: result.thumbnailError,
        },
      ];
    }
    return result.previews.map((preview, index) => ({
      id: preview.id,
      filename: result.previews.length === 1 ? result.filename : `${result.filename} #${index + 1}`,
      status: result.status,
      resolution: result.resolution,
      createdAt: result.createdAt,
      thumbnailAssetKind: preview.kind,
      thumbnailObjectUrl: preview.objectUrl,
      thumbnailState: preview.state,
      thumbnailError: preview.error,
    }));
  }),
);

const eventItems = computed<TimelineItem[]>(() =>
  runtimeRecordState.events
    .filter((item) => /^(capture|device|video|stream)\./.test(item.title))
    .slice(0, 20)
    .map((item) => ({
      id: item.id,
      time: item.time,
      title: item.title,
      detail: item.detail,
      meta: item.meta,
      tone: item.tone,
    })),
);

watch(
  () => [authSessionState.commandState, authSessionState.sessionToken] as const,
  ([state, sessionToken]) => {
    commandEventUnsubscribe?.();
    commandEventUnsubscribe = null;
    if (state === 'success' && sessionToken) {
      commandEventUnsubscribe = onCommandEvent(handleCommandEvent);
      void loadDeviceInventory();
      return;
    }
    resetDeviceInventory();
    resetDeviceVideo();
  },
  { immediate: true },
);

watch(
  () => deviceInventoryState.devices.map((device) => device.device_id).join('|'),
  () => {
    if (!suppressAutoOpen.value && !deviceVideoState.selectedDeviceId && deviceInventoryState.devices.length > 0) {
      void openDefaultCaptureDevice();
    }
  },
  { immediate: true },
);

watch(
  () => [
    captureConfig.pageProcessing,
    captureConfig.singlePageRealtimeDetectRects,
    captureConfig.singlePageCropBorder,
    captureConfig.singlePageCropBorderWidth,
    captureConfig.singlePageCropBorderHeight,
    captureConfig.singlePageIdCardRoundCorner,
    captureConfig.singlePageAutoRotate,
    captureConfig.singlePageSmartBlackEdgeOptimize,
    captureConfig.singlePageMultiTargetPaging,
    captureConfig.curvedBookRemoveFinger,
    captureConfig.curvedBookFingerType,
    captureConfig.curvedBookSmartPaging,
    captureConfig.curvedBookCropBorder,
    captureConfig.curvedBookCropBorderWidth,
    captureConfig.curvedBookCropBorderHeight,
    captureConfig.curvedBookAutoComplete,
    captureConfig.selectedAreaMode,
    captureConfig.selectedAreaPoints.map((point) => `${point.x},${point.y}`).join('|'),
    captureConfig.colorMode,
    captureConfig.outputFormat,
    captureConfig.thumbnails.original,
    captureConfig.thumbnails.pageProcessed,
    captureConfig.thumbnails.colorProcessed,
    captureConfig.thumbnails.final,
    deviceVideoState.selectedDeviceId,
    deviceVideoState.selectedResolutionKey,
  ],
  () => {
    profileRevision.value += 1;
    profileInitializedAt.value = timeLabel();
  },
);

watch(
  () => [captureConfig.pageProcessing, captureConfig.singlePageRealtimeDetectRects, captureConfig.singlePageMultiTargetPaging] as const,
  async () => {
    if (!deviceVideoState.streamId || deviceVideoState.startState === 'running' || deviceVideoState.stopState === 'running') {
      return;
    }
    await setVideoProfile(captureProfile.value);
  },
);

onBeforeUnmount(() => {
  commandEventUnsubscribe?.();
  commandEventUnsubscribe = null;
  revokeAllThumbnailObjectUrls();
  void closeSelectedDevice();
  resetDeviceVideo();
});

function handleVideoCanvas(canvas: HTMLCanvasElement | null): void {
  captureVideoCanvas.value = canvas;
  attachVideoCanvas(canvas);
}

function setSelectedAreaMode(mode: SelectedAreaMode): void {
  captureConfig.selectedAreaMode = mode;
  clearSelectedArea();
}

function clearSelectedArea(): void {
  captureConfig.selectedAreaPoints = [];
  selectedAreaDragStart.value = null;
  selectedAreaDragging.value = false;
}

function handleSelectedAreaPointerDown(event: PointerEvent): void {
  const point = pointerEventToImagePoint(event);
  if (!point) {
    return;
  }
  (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
  if (captureConfig.selectedAreaMode === 'rectangle') {
    selectedAreaDragStart.value = point;
    selectedAreaDragging.value = true;
    captureConfig.selectedAreaPoints = [point, point];
    return;
  }
  if (captureConfig.selectedAreaPoints.length >= 4) {
    captureConfig.selectedAreaPoints = [];
  }
  captureConfig.selectedAreaPoints = [...captureConfig.selectedAreaPoints, point];
}

function handleSelectedAreaPointerMove(event: PointerEvent): void {
  if (captureConfig.selectedAreaMode !== 'rectangle' || !selectedAreaDragging.value || !selectedAreaDragStart.value) {
    return;
  }
  const point = pointerEventToImagePoint(event);
  if (!point) {
    return;
  }
  captureConfig.selectedAreaPoints = [selectedAreaDragStart.value, point];
}

function handleSelectedAreaPointerUp(event: PointerEvent): void {
  if (captureConfig.selectedAreaMode !== 'rectangle' || !selectedAreaDragging.value || !selectedAreaDragStart.value) {
    return;
  }
  const point = pointerEventToImagePoint(event);
  selectedAreaDragging.value = false;
  if (!point) {
    return;
  }
  const rect = rectFromCorners(selectedAreaDragStart.value, point);
  captureConfig.selectedAreaPoints = rect ? [rect.left_top, rect.right_top, rect.right_down, rect.left_down] : [];
}

function handleSelectedAreaPointerCancel(): void {
  selectedAreaDragging.value = false;
  selectedAreaDragStart.value = null;
}

function pointerEventToImagePoint(event: PointerEvent): ImagePoint | null {
  const canvas = captureVideoCanvas.value;
  if (!canvas || canvas.width <= 0 || canvas.height <= 0 || !(event.currentTarget instanceof HTMLElement)) {
    return null;
  }
  const rect = event.currentTarget.getBoundingClientRect();
  const fit = containRect(rect.width, rect.height, canvas.width, canvas.height);
  const x = event.clientX - rect.left - fit.x;
  const y = event.clientY - rect.top - fit.y;
  if (x < 0 || y < 0 || x > fit.width || y > fit.height) {
    return null;
  }
  return {
    x: clamp((x / fit.width) * canvas.width, 0, canvas.width),
    y: clamp((y / fit.height) * canvas.height, 0, canvas.height),
  };
}

function imagePointToOverlayPoint(point: ImagePoint): ImagePoint | null {
  const canvas = captureVideoCanvas.value;
  if (!canvas || canvas.width <= 0 || canvas.height <= 0) {
    return null;
  }
  const bounds = canvas.getBoundingClientRect();
  const fit = containRect(bounds.width, bounds.height, canvas.width, canvas.height);
  return {
    x: fit.x + (point.x / canvas.width) * fit.width,
    y: fit.y + (point.y / canvas.height) * fit.height,
  };
}

function containRect(containerWidth: number, containerHeight: number, imageWidth: number, imageHeight: number): OverlayRect {
  const scale = Math.min(containerWidth / imageWidth, containerHeight / imageHeight);
  const width = imageWidth * scale;
  const height = imageHeight * scale;
  return {
    x: (containerWidth - width) / 2,
    y: (containerHeight - height) / 2,
    width,
    height,
  };
}

function rectFromCorners(start: ImagePoint, end: ImagePoint): Rect4P | null {
  const left = Math.min(start.x, end.x);
  const right = Math.max(start.x, end.x);
  const top = Math.min(start.y, end.y);
  const bottom = Math.max(start.y, end.y);
  if (right - left < 8 || bottom - top < 8) {
    return null;
  }
  return {
    left_top: { x: left, y: top },
    right_top: { x: right, y: top },
    right_down: { x: right, y: bottom },
    left_down: { x: left, y: bottom },
  };
}

function normalizeAreaPoints(points: ImagePoint[]): Rect4P {
  const center = points.reduce((acc, point) => ({ x: acc.x + point.x / points.length, y: acc.y + point.y / points.length }), { x: 0, y: 0 });
  const sorted = [...points].sort((a, b) => Math.atan2(a.y - center.y, a.x - center.x) - Math.atan2(b.y - center.y, b.x - center.x));
  const top = sorted.slice().sort((a, b) => a.y - b.y).slice(0, 2).sort((a, b) => a.x - b.x);
  const bottom = sorted.slice().sort((a, b) => b.y - a.y).slice(0, 2).sort((a, b) => a.x - b.x);
  return {
    left_top: top[0],
    right_top: top[1],
    right_down: bottom[1],
    left_down: bottom[0],
  };
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function clampCropMargin(value: number): number {
  return Math.trunc(clamp(Number.isFinite(value) ? value : 0, -100, 100));
}

function handleResolutionChange(event: Event): void {
  const value = (event.target as HTMLSelectElement).value;
  void setSelectedResolution(value);
}

async function openDefaultCaptureDevice(): Promise<void> {
  const defaultDeviceId = deviceInventoryState.devices[0]?.device_id;
  if (!defaultDeviceId) {
    return;
  }
  await selectDevice(defaultDeviceId);
  await openCaptureDevice();
  await startCaptureVideo();
}

async function closeCaptureDevice(): Promise<void> {
  suppressAutoOpen.value = true;
  if (deviceVideoState.streamId) {
    await stopVideo();
    if (deviceVideoState.stopState === 'error') {
      return;
    }
  }
  await closeSelectedDevice();
}

async function openCaptureDevice(): Promise<void> {
  await applyCaptureAcquisitionResolution();
  await openSelectedDevice();
}

async function startCaptureVideo(): Promise<void> {
  await applyCaptureAcquisitionResolution();
  await startVideo(captureProfile.value);
}

function handleEnhanceWorkflowSelected(workflow: EnhanceWorkflow | null): void {
  selectedEnhanceWorkflow.value = workflow;
}

function buildCaptureEnhancePipeline(): EnhancePipeline | undefined {
  if (!selectedEnhanceWorkflow.value?.pipeline?.steps?.length) {
    return undefined;
  }
  return {
    ...selectedEnhanceWorkflow.value.pipeline,
    target: {
      type: 'images',
      format: captureConfig.outputFormat,
      export_type: 'single-page',
    },
  };
}

async function handleCapture(): Promise<void> {
  if (!canCapture.value) {
    return;
  }

  const index = captureResults.value.length + 1;
  const pendingId = `capture-${Date.now()}-${index}`;
  const filename = `capture-${String(index).padStart(3, '0')}.${captureConfig.outputFormat}`;
  const result: CaptureResult = {
    id: pendingId,
    taskId: '',
    filename,
    format: captureConfig.outputFormat,
    pageProcessing: activePageProcessing.value?.label ?? captureConfig.pageProcessing,
    colorMode: activeColorMode.value?.label ?? captureConfig.colorMode,
    thumbnails: enabledThumbnailLabels.value.length > 0 ? enabledThumbnailLabels.value.join(', ') : t('pages.captureAcquisition.none'),
    resolution: selectedResolutionLabel.value,
    createdAt: timeLabel(),
    status: 'pending',
    path: '',
    assetUrl: '',
    downloadUrl: '',
    assets: [],
    previews: [],
    thumbnailAssetId: '',
    thumbnailAssetKind: '',
    thumbnailUrl: '',
    thumbnailObjectUrl: '',
    thumbnailState: 'idle',
    thumbnailError: '',
  };

  captureResults.value = [result, ...captureResults.value];
  try {
    const enhancePipeline = buildCaptureEnhancePipeline();
    const response = await sendBoundCommand('capture.take', {
      params: {
        device_id: deviceVideoState.selectedDeviceId,
        profile: captureProfile.value,
        timeout_ms: 20000,
        ...(enhancePipeline ? { pipeline: enhancePipeline } : {}),
      },
    });
    logCaptureDebug('capture.take response', {
      code: response?.code,
      message: response?.message,
      data: response?.data,
      task_id: response?.data ? asString(response.data.task_id) : '',
      status: response?.data ? asString(response.data.status) : '',
      assets: response?.data && typeof response.data === 'object' ? (response.data as Record<string, unknown>).assets : undefined,
    });
    if (!response || response.code !== 0) {
      const message = response?.message || 'failed';
      updateCaptureResult(pendingId, {
        status: message,
        thumbnailState: 'error',
        thumbnailError: message,
      });
      return;
    }
    const taskId = asString(response.data.task_id);
    updateCaptureResult(pendingId, { taskId, status: asString(response.data.status) || 'queued' });
    if (taskId) {
      activeCaptureTasks.add(taskId);
      void pollCaptureTask(taskId);
    }
    recordRuntimeEvent({
      title: 'capture.submitted',
      detail: taskId ? `capture.take accepted ${taskId}` : 'capture.take accepted',
      tone: 'success',
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : 'failed';
    updateCaptureResult(pendingId, {
      status: message,
      thumbnailState: 'error',
      thumbnailError: message,
    });
  }
}

async function pollCaptureTask(taskId: string): Promise<void> {
  for (let attempt = 0; attempt < 40 && activeCaptureTasks.has(taskId); attempt += 1) {
    await delay(750);
    try {
      const response = await sendBoundCommand('capture.get', {
        params: { task_id: taskId },
      });
      if (response.code !== 0) {
        logCaptureDebug('capture.get non-ok response', {
          task_id: taskId,
          code: response.code,
          message: response.message,
          data: response.data,
        });
        continue;
      }
      logCaptureDebug('capture.get response', {
        task_id: taskId,
        code: response.code,
        message: response.message,
        status: asString(response.data.status),
        assets_count: Array.isArray((response.data as Record<string, unknown>).assets)
          ? ((response.data as Record<string, unknown>).assets as unknown[]).length
          : 0,
        assets: (response.data as Record<string, unknown>).assets,
        data: response.data,
      });
      const task = asTaskPayload(response.data);
      if (!task.task_id) {
        continue;
      }
      applyCaptureTask(task);
      if (task.status === 'succeeded' || task.status === 'failed') {
        activeCaptureTasks.delete(taskId);
        return;
      }
    } catch {
      // Keep polling; command events may still complete the row.
    }
  }
}

function handleCommandEvent(event: { event: string; payload?: Record<string, unknown> }): void {
  if (!event.event.startsWith('capture.')) {
    return;
  }
  logCaptureDebug('capture event payload', {
    event: event.event,
    payload: event.payload,
  });
  const task = asTaskPayload(event.payload);
  if (!task.task_id || !activeCaptureTasks.has(task.task_id)) {
    return;
  }
  applyCaptureTask(task);
  if (event.event === 'capture.completed' || event.event === 'capture.failed' || task.status === 'succeeded' || task.status === 'failed') {
    activeCaptureTasks.delete(task.task_id);
  }
}

function applyCaptureTask(task: CaptureTaskPayload): void {
  const finalAsset = task.assets.find((asset) => asset.kind === 'final') ?? task.assets.find((asset) => asset.kind === 'original');
  const displayAssets = selectDisplayAssets(task.assets);
  const displayAsset = displayAssets[0];
  logCaptureDebug('applyCaptureTask parsed', {
    task_id: task.task_id,
    status: task.status,
    message: task.message,
    error: task.error,
    assets_count: task.assets.length,
    assets: task.assets.map((asset) => ({
      asset_id: asset.assetId,
      kind: asset.kind,
      content_type: asset.contentType,
      url: asset.url,
      download_url: asset.downloadUrl,
      path: asset.path,
      size: asset.size,
    })),
    final_asset: finalAsset,
    display_asset: displayAsset,
    display_assets: displayAssets,
  });
  const currentRow = captureResults.value.find((item) => item.taskId === task.task_id);
  const thumbnailChanged = Boolean(displayAsset && currentRow?.thumbnailAssetId && currentRow.thumbnailAssetId !== displayAsset.assetId);
  if (thumbnailChanged && currentRow?.thumbnailObjectUrl) {
    URL.revokeObjectURL(currentRow.thumbnailObjectUrl);
  }
  const nextPreviews = buildCapturePreviews(currentRow, displayAssets);
  const filename = finalAsset?.path ? finalAsset.path.split('/').pop() || finalAsset.path : undefined;
  const status = task.status === 'succeeded' ? 'succeeded' : task.status === 'failed' ? task.error || task.message || 'failed' : task.status;
  updateCaptureByTaskId(task.task_id, {
    filename,
    path: finalAsset?.path,
    assetUrl: finalAsset?.url,
    downloadUrl: finalAsset?.downloadUrl,
    assets: task.assets,
    previews: nextPreviews,
    thumbnailAssetId: displayAsset?.assetId,
    thumbnailAssetKind: displayAsset?.kind,
    thumbnailUrl: displayAsset ? assetAccessPath(displayAsset) : undefined,
    thumbnailObjectUrl: thumbnailChanged ? '' : undefined,
    thumbnailState: isTerminalCaptureStatus(task.status) && !displayAsset ? 'unavailable' : undefined,
    status,
  });
  const row = captureResults.value.find((item) => item.taskId === task.task_id);
  if (row) {
    for (const preview of row.previews) {
      const asset = displayAssets.find((item) => item.assetId === preview.assetId);
      if (asset && !preview.objectUrl && preview.state !== 'loading') {
        void loadCaptureThumbnail(row.id, preview.id, asset);
      }
    }
  }
}

function updateCaptureByTaskId(taskId: string, patch: Partial<CaptureResult>): void {
  const row = captureResults.value.find((item) => item.taskId === taskId);
  if (!row) {
    return;
  }
  updateCaptureResult(row.id, patch);
}

function updateCaptureResult(id: string, patch: Partial<CaptureResult>): void {
  captureResults.value = captureResults.value.map((item) => (item.id === id ? { ...item, ...withoutUndefinedCapturePatch(patch) } : item));
}

function updateCapturePreview(resultId: string, previewId: string, patch: Partial<CapturePreview>): void {
  captureResults.value = captureResults.value.map((item) => {
    if (item.id !== resultId) {
      return item;
    }
    return {
      ...item,
      previews: item.previews.map((preview) => (preview.id === previewId ? { ...preview, ...patch } : preview)),
    };
  });
}

function clearCaptureResults(): void {
  revokeAllThumbnailObjectUrls();
  captureResults.value = [];
  activeCaptureTasks.clear();
  thumbnailRequests.clear();
  recordRuntimeEvent({
    title: 'capture.results_cleared',
    detail: t('pages.captureAcquisition.resultsCleared'),
    tone: 'neutral',
  });
}

function openCapturePreviewViewer(card: CapturePreviewCard): void {
  if (!card.thumbnailObjectUrl) {
    return;
  }
  openImageViewer({
    src: card.thumbnailObjectUrl,
    title: card.filename,
    subtitle: [card.thumbnailAssetKind, card.resolution, card.status].filter(Boolean).join(' · '),
  });
}

function selectDisplayAssets(assets: CaptureAssetPayload[]): CaptureAssetPayload[] {
  const preferredKinds = [
    'final_thumbnail',
    'color_processed_thumbnail',
    'page_processed_thumbnail',
    'original_thumbnail',
    'color_processed',
    'page_processed',
    'original',
    'final',
  ];
  for (const kind of preferredKinds) {
    const matched = assets.filter((item) => item.kind === kind || item.kind.startsWith(`${kind}_`));
    const renderable = matched.filter((item) => assetAccessPath(item) && isBrowserRenderableImage(item));
    if (renderable.length > 0) {
      return renderable;
    }
  }
  return [];
}

function buildCapturePreviews(currentRow: CaptureResult | undefined, assets: CaptureAssetPayload[]): CapturePreview[] {
  const previous = new Map((currentRow?.previews ?? []).map((preview) => [preview.assetId, preview]));
  const nextAssetIds = new Set(assets.map((asset) => asset.assetId));
  for (const preview of currentRow?.previews ?? []) {
    if (!nextAssetIds.has(preview.assetId) && preview.objectUrl) {
      URL.revokeObjectURL(preview.objectUrl);
    }
  }
  return assets.map((asset, index) => {
    const oldPreview = previous.get(asset.assetId);
    return {
      id: `${currentRow?.id ?? 'capture'}-preview-${asset.assetId || index}`,
      assetId: asset.assetId,
      kind: asset.kind,
      url: assetAccessPath(asset),
      objectUrl: oldPreview?.objectUrl ?? '',
      state: oldPreview?.objectUrl ? 'ready' : oldPreview?.state ?? 'idle',
      error: oldPreview?.error ?? '',
    };
  });
}

async function loadCaptureThumbnail(resultId: string, previewId: string, asset: CaptureAssetPayload): Promise<void> {
  const sessionToken = authSessionState.sessionToken;
  const accessPath = assetAccessPath(asset);
  if (!sessionToken || !accessPath) {
    updateCapturePreview(resultId, previewId, { state: 'error', error: t('pages.captureAcquisition.thumbnailAuthMissing') });
    return;
  }

  const requestToken = Date.now();
  thumbnailRequests.set(previewId, requestToken);
  updateCapturePreview(resultId, previewId, { state: 'loading', error: '' });

  try {
    const fetchUrl = resolveAssetUrl(accessPath);
    logCaptureDebug('thumbnail fetch request', {
      asset_id: asset.assetId,
      kind: asset.kind,
      url: fetchUrl,
    });
    const response = await fetch(fetchUrl, {
      headers: {
        Authorization: `Bearer ${sessionToken}`,
      },
    });
    logCaptureDebug('thumbnail fetch response', {
      asset_id: asset.assetId,
      status: response.status,
      status_text: response.statusText,
      content_type: response.headers.get('content-type'),
      url: response.url,
    });
    if (!response.ok) {
      throw new Error(`${response.status} ${response.statusText}`.trim());
    }
    const contentType = response.headers.get('content-type') || asset.contentType;
    if (!contentType.toLowerCase().startsWith('image/')) {
      throw new Error(`unexpected content-type: ${contentType || 'unknown'}`);
    }
    let blob = await response.blob();
    if (!blob.type.toLowerCase().startsWith('image/') && contentType) {
      blob = blob.slice(0, blob.size, contentType);
    }
    if (thumbnailRequests.get(previewId) !== requestToken) {
      return;
    }
    const objectUrl = URL.createObjectURL(blob);
    const current = captureResults.value.find((item) => item.id === resultId);
    const currentPreview = current?.previews.find((item) => item.id === previewId);
    if (!current || !currentPreview) {
      URL.revokeObjectURL(objectUrl);
      return;
    }
    if (currentPreview.objectUrl) {
      URL.revokeObjectURL(currentPreview.objectUrl);
    }
    updateCapturePreview(resultId, previewId, { objectUrl, state: 'ready', error: '' });
  } catch (error) {
    if (thumbnailRequests.get(previewId) !== requestToken) {
      return;
    }
    updateCapturePreview(resultId, previewId, {
      state: 'error',
      error: error instanceof Error ? error.message : t('pages.captureAcquisition.thumbnailLoadFailed'),
    });
  } finally {
    if (thumbnailRequests.get(previewId) === requestToken) {
      thumbnailRequests.delete(previewId);
    }
  }
}

function resolveAssetUrl(path: string): string {
  if (/^https?:\/\//i.test(path)) {
    return path;
  }
  const protocol = window.location.protocol === 'https:' ? 'https:' : 'http:';
  return `${protocol}//${window.location.hostname || '127.0.0.1'}:17082${path.startsWith('/') ? path : `/${path}`}`;
}

function assetAccessPath(asset: CaptureAssetPayload): string {
  return asset.url || asset.downloadUrl;
}

function isBrowserRenderableImage(asset: CaptureAssetPayload): boolean {
  const contentType = asset.contentType.toLowerCase();
  if (!contentType) {
    return asset.kind.includes('thumbnail') || asset.kind === 'original' || asset.kind.startsWith('page_processed') || asset.kind.startsWith('color_processed');
  }
  return ['image/jpeg', 'image/jpg', 'image/png', 'image/webp', 'image/gif', 'image/bmp'].includes(contentType);
}

function revokeAllThumbnailObjectUrls(): void {
  for (const result of captureResults.value) {
    if (result.thumbnailObjectUrl) {
      URL.revokeObjectURL(result.thumbnailObjectUrl);
    }
    for (const preview of result.previews) {
      if (preview.objectUrl) {
        URL.revokeObjectURL(preview.objectUrl);
      }
    }
  }
}

function thumbnailStateLabel(result: { thumbnailState: ThumbnailState; status: string }): string {
  if (result.thumbnailState === 'loading') {
    return t('pages.captureAcquisition.thumbnailLoading');
  }
  if (result.thumbnailState === 'unavailable') {
    return t('pages.captureAcquisition.thumbnailUnavailable');
  }
  if (result.thumbnailState === 'error') {
    return t('pages.captureAcquisition.thumbnailLoadFailed');
  }
  if (isActiveCaptureStatus(result.status)) {
    return t('pages.captureAcquisition.capturePending');
  }
  if (isTerminalCaptureStatus(result.status)) {
    return t('pages.captureAcquisition.thumbnailUnavailable');
  }
  return t('pages.captureAcquisition.thumbnailWaiting');
}

function captureStatusTone(status: string): Tone {
  if (status === 'succeeded') {
    return 'success';
  }
  if (status === 'pending' || status === 'queued' || status === 'running') {
    return 'warning';
  }
  if (status === 'failed' || status.toLowerCase().includes('fail') || status.toLowerCase().includes('error')) {
    return 'danger';
  }
  if (status.toLowerCase().includes('exhausted') || status.toLowerCase().includes('quota')) {
    return 'danger';
  }
  return 'neutral';
}

function isActiveCaptureStatus(status: string): boolean {
  return ['pending', 'queued', 'running'].includes(status);
}

function isTerminalCaptureStatus(status: string): boolean {
  const normalized = status.toLowerCase();
  return (
    ['succeeded', 'success', 'completed', 'done', 'failed', 'error'].includes(normalized) ||
    normalized.includes('fail') ||
    normalized.includes('error') ||
    normalized.includes('exhausted') ||
    normalized.includes('quota')
  );
}

interface CaptureAssetPayload {
  assetId: string;
  kind: string;
  path: string;
  contentType: string;
  size: number;
  url: string;
  downloadUrl: string;
}

interface CaptureTaskPayload {
  task_id: string;
  status: string;
  message: string;
  error: string;
  assets: CaptureAssetPayload[];
}

function asTaskPayload(value: unknown): CaptureTaskPayload {
  const record = value && typeof value === 'object' ? (value as Record<string, unknown>) : {};
  const taskId = asString(record.task_id);
  return {
    task_id: taskId,
    status: asString(record.status),
    message: asString(record.message),
    error: asString(record.error),
    assets: asAssets(record.assets, taskId),
  };
}

function asAssets(value: unknown, taskId: string): CaptureAssetPayload[] {
  if (!Array.isArray(value)) {
    return [];
  }
  return value
    .filter((item): item is Record<string, unknown> => Boolean(item) && typeof item === 'object')
    .map((item) => {
      const assetId = asString(item.asset_id);
      const synthesizedUrl = taskId && assetId ? `/api/assets/${encodeURIComponent(taskId)}/${encodeURIComponent(assetId)}` : '';
      return {
        assetId,
        kind: asString(item.kind),
        path: asString(item.path),
        contentType: asString(item.content_type),
        size: asNumber(item.size),
        url: asString(item.url) || synthesizedUrl,
        downloadUrl: asString(item.download_url) || (synthesizedUrl ? `${synthesizedUrl}/download` : ''),
      };
    });
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function asNumber(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function logCaptureDebug(label: string, payload: unknown): void {
  console.info(`[capture-debug] ${label}`, payload);
}

function withoutUndefinedCapturePatch(value: Partial<CaptureResult>): Partial<CaptureResult> {
  const result: Partial<CaptureResult> = {};
  for (const key of Object.keys(value) as Array<keyof CaptureResult>) {
    if (value[key] !== undefined) {
      result[key] = value[key] as never;
    }
  }
  return result;
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => {
    window.setTimeout(resolve, ms);
  });
}

function timeLabel(): string {
  return new Date().toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });
}
</script>

<style scoped>
.control-button {
  cursor: pointer;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.75rem;
  background: rgb(255 255 255);
  padding: 0.5rem 0.75rem;
  color: rgb(51 65 85);
  font-size: 0.875rem;
  font-weight: 600;
  transition:
    border-color 160ms ease,
    color 160ms ease,
    opacity 160ms ease;
}

.control-button:hover:not(:disabled) {
  border-color: rgb(103 232 249);
  color: rgb(14 116 144);
}

.control-button:disabled {
  cursor: not-allowed;
  opacity: 0.5;
}

.option-button {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 0.125rem;
  border: 1px solid rgb(226 232 240);
  border-radius: 0.75rem;
  background: rgb(255 255 255);
  padding: 0.5rem 0.75rem;
  color: rgb(51 65 85);
  text-align: left;
  font-size: 0.875rem;
  transition:
    background-color 160ms ease,
    border-color 160ms ease,
    box-shadow 160ms ease;
}

.option-button:hover {
  border-color: rgb(165 243 252);
  background: rgb(236 254 255 / 0.4);
}

.option-button-active {
  border-color: rgb(34 211 238);
  background: rgb(236 254 255);
  color: rgb(22 78 99);
  box-shadow: 0 1px 2px rgb(15 23 42 / 0.08);
}
</style>
