import type {
  CapabilityItem,
  EndpointCardItem,
  ErrorItem,
  ExecutionState,
  InfoCardItem,
  JsonSnippet,
  MetricCardItem,
  NavigationItem,
  PreviewMetric,
  QuickCheckItem,
  RequestHistoryItem,
  ShellStatusItem,
  TableRow,
  TimelineItem,
} from '../types/demo';

export const navigationItems: NavigationItem[] = [
  { key: 'quick-start', labelKey: 'nav.quickStart', to: '/quick-start' },
  { key: 'connection-auth', labelKey: 'nav.connectionAuth', to: '/connection-auth' },
  { key: 'device-video', labelKey: 'nav.deviceVideo', to: '/device-video' },
  { key: 'capture-acquisition', labelKey: 'nav.captureAcquisition', to: '/capture-acquisition' },
  { key: 'image-processing', labelKey: 'nav.imageProcessing', to: '/image-processing' },
  { key: 'ocr-recognition', labelKey: 'nav.ocrRecognition', to: '/ocr-recognition' },
  { key: 'file-conversion', labelKey: 'nav.fileConversion', to: '/file-conversion' },
  { key: 'sane-scanning', labelKey: 'nav.saneScanning', to: '/sane-scanning' },
  { key: 'protocol-debugger', labelKey: 'nav.protocolDebugger', to: '/protocol-debugger' },
  { key: 'events-results', labelKey: 'nav.eventsResults', to: '/events-results' },
];

export const shellStatusItems: ShellStatusItem[] = [
  { id: 'command', labelKey: 'header.command', value: '127.0.0.1:17090', tone: 'success' },
  { id: 'auth', labelKey: 'header.auth', value: 'validated · sdk_demo_basic', tone: 'primary' },
  { id: 'device', labelKey: 'header.device', value: 'ET18 Pro / dev-01', tone: 'info' },
  { id: 'provider', labelKey: 'header.provider', value: 'hybrid-runtime', tone: 'warning' },
  { id: 'trace', labelKey: 'header.trace', value: 'trc-demo-20260407-1842', tone: 'neutral' },
];

export const globalRequestHistory: RequestHistoryItem[] = [
  {
    id: 'req-1',
    method: 'system.capabilities',
    requestId: 'req-cap-001',
    code: '0',
    traceId: 'trc-demo-20260407-1801',
    duration: '22ms',
    state: 'success',
  },
  {
    id: 'req-2',
    method: 'auth.create_session',
    requestId: 'req-auth-017',
    code: '0',
    traceId: 'trc-demo-20260407-1818',
    duration: '38ms',
    state: 'success',
  },
  {
    id: 'req-3',
    method: 'capture.take',
    requestId: 'req-cap-064',
    code: '1902',
    traceId: 'trc-demo-20260407-1832',
    duration: '142ms',
    state: 'error',
  },
  {
    id: 'req-4',
    method: 'recognition.set_realtime_barcode',
    requestId: 'req-rec-011',
    code: '0',
    traceId: 'trc-demo-20260407-1838',
    duration: '19ms',
    state: 'running',
  },
];

export const globalEvents: TimelineItem[] = [
  {
    id: 'evt-1',
    title: 'device.online',
    detail: 'dev-01 · ET18 Pro is now visible to device.list',
    meta: '18:20:14',
    tone: 'success',
  },
  {
    id: 'evt-2',
    title: 'stream.frame_meta',
    detail: 'frame=4289 · 1920x1080 · exposure=auto',
    meta: '18:21:02',
    tone: 'primary',
  },
  {
    id: 'evt-3',
    title: 'capture.turn_detected',
    detail: 'sheet=page-2 · angle=6.4deg · confidence=0.91',
    meta: '18:21:46',
    tone: 'warning',
  },
  {
    id: 'evt-4',
    title: 'recognition.barcode_detected',
    detail: 'QR_CODE · 5W0D-2A4K-8891',
    meta: '18:22:11',
    tone: 'info',
  },
];

export const globalErrors: ErrorItem[] = [
  {
    id: 'err-1',
    code: '1100',
    name: 'AUTH_REQUIRED',
    method: 'device.open',
    message: 'No credential material was attached to the command channel.',
    traceId: 'trc-demo-20260407-1758',
    occurredAt: '17:58:04',
  },
  {
    id: 'err-2',
    code: '1700',
    name: 'SANE_NOT_AVAILABLE',
    method: 'sane.list',
    message: 'Host runtime started without SANE support and switched to graceful fallback.',
    traceId: 'trc-demo-20260407-1820',
    occurredAt: '18:20:54',
  },
  {
    id: 'err-3',
    code: '1902',
    name: 'PROVIDER_CALL_FAILED',
    method: 'capture.take',
    message: 'Capture provider rejected the request while a previous frame was still draining.',
    traceId: 'trc-demo-20260407-1832',
    occurredAt: '18:32:17',
  },
];

export const quickStartMetrics: MetricCardItem[] = [
  {
    id: 'routes',
    value: '34',
    labelKey: 'common.primaryFlow',
    detail: 'Connected lanes exposed by the mock demo runtime',
    trend: '+6 planned',
    tone: 'primary',
  },
  {
    id: 'coverage',
    value: '9 / 10',
    labelKey: 'common.mockMode',
    detail: 'Capability domains with GA or beta walkthroughs ready',
    trend: '1 planned-only lane',
    tone: 'success',
  },
  {
    id: 'recentTrace',
    value: '3.8k',
    labelKey: 'common.recentRequests',
    detail: 'Synthetic request samples loaded into the global drawer',
    trend: 'last 24h snapshot',
    tone: 'warning',
  },
];

export const quickStartEndpoints: EndpointCardItem[] = [
  {
    id: 'admin-http',
    title: 'Admin HTTP',
    url: 'http://127.0.0.1:17080',
    protocol: 'HTTP',
    note: 'Health endpoint and runtime console',
    state: 'success',
  },
  {
    id: 'demo-http',
    title: 'Demo HTTP',
    url: 'http://127.0.0.1:17081',
    protocol: 'HTTP',
    note: 'Demo surface with mock capability flows',
    state: 'success',
  },
  {
    id: 'command-ws',
    title: 'Command WS',
    url: 'ws://127.0.0.1:17090',
    protocol: 'WS',
    note: 'Main JSON-RPC command lane',
    state: 'success',
  },
  {
    id: 'video-ws',
    title: 'Video WS',
    url: 'ws://127.0.0.1:17091',
    protocol: 'WS',
    note: 'Preview stream and frame metadata lane',
    state: 'running',
  },
];

export const quickStartChecks: QuickCheckItem[] = [
  {
    id: 'ping',
    method: 'system.ping',
    latency: '14ms',
    note: 'Base heartbeat returned pong=true',
    state: 'success',
    payload: `{"method":"system.ping","result":{"pong":true,"ts":"2026-04-07T18:17:14Z"}}`,
  },
  {
    id: 'info',
    method: 'system.info',
    latency: '27ms',
    note: 'Runtime build and provider posture resolved',
    state: 'success',
    payload: `{"method":"system.info","result":{"version":"0.1.0","provider_mode":"hybrid-runtime","platform":"linux"}}`,
  },
  {
    id: 'capabilities',
    method: 'system.capabilities',
    latency: '22ms',
    note: 'Capability matrix loaded for 9 operational domains',
    state: 'success',
    payload: `{"method":"system.capabilities","result":{"ga":24,"beta":8,"planned":4}}`,
  },
];

export const quickStartCapabilities: CapabilityItem[] = [
  { id: 'system', domain: 'system', coverage: '3 / 3 routes', status: 'ga', tags: [] },
  { id: 'auth', domain: 'auth', coverage: '3 / 4 routes', status: 'ga', tags: [] },
  { id: 'device', domain: 'device', coverage: '4 / 4 routes', status: 'ga', tags: ['device'] },
  { id: 'stream', domain: 'stream / event', coverage: '3 / 4 routes', status: 'beta', tags: ['device'] },
  { id: 'capture', domain: 'capture', coverage: '5 / 5 routes', status: 'ga', tags: ['device'] },
  { id: 'image', domain: 'image', coverage: '8 / 10 routes', status: 'beta', tags: ['provider'] },
  { id: 'ocr', domain: 'ocr / recognition', coverage: '4 / 5 routes', status: 'ga', tags: ['account'] },
  { id: 'file', domain: 'file', coverage: '5 / 8 routes', status: 'beta', tags: [] },
  { id: 'sane', domain: 'sane', coverage: '2 / 4 routes', status: 'planned', tags: ['provider'] },
];

export const connectionEndpoints: EndpointCardItem[] = [
  {
    id: 'command-lane',
    title: 'Command Lane',
    url: 'ws://127.0.0.1:17090',
    protocol: 'WS',
    note: 'Connected with demo bearer and live trace propagation',
    state: 'success',
  },
  {
    id: 'video-lane',
    title: 'Video Lane',
    url: 'ws://127.0.0.1:17091',
    protocol: 'WS',
    note: 'Connected but waiting for active stream subscription',
    state: 'running',
  },
];

export const authActions: InfoCardItem[] = [
  {
    id: 'validate',
    eyebrow: 'auth.create_session',
    title: 'Primary credential check',
    description: 'token -> session_token -> bound command session',
    meta: 'next trace: trc-demo-20260407-1844',
    tone: 'primary',
    state: 'success',
  },
  {
    id: 'refresh',
    eyebrow: 'auth.refresh_session',
    title: 'Session refresh posture',
    description: 'refresh interval=25m · soft expiry=30m',
    meta: 'recommended when idle > 15m',
    tone: 'info',
    state: 'idle',
  },
  {
    id: 'context',
    eyebrow: 'auth.get_context',
    title: 'Context snapshot',
    description: 'Load scopes, account profile, and capability grants',
    meta: 'read-only helper action',
    tone: 'success',
    state: 'success',
  },
];

export const authContextFields = {
  isValid: 'true',
  accountType: 'sdk_demo_basic',
  licenseMode: 'enterprise_trial',
  deviceScope: 'dev-01, dev-02',
  expiresAt: '2026-04-07T20:48:00Z',
  capabilities: 'capture.*, image.process, image.process_page, image.apply_color_mode, ocr.recognize, file.convert',
};

export const authFailureItems: ErrorItem[] = [
  {
    id: 'auth-1',
    code: '1100',
    name: 'AUTH_REQUIRED',
    method: 'auth.create_session',
    message: 'The bound session was not created before the business request was sent.',
    traceId: 'trc-demo-20260407-1710',
    occurredAt: '17:10:22',
  },
  {
    id: 'auth-2',
    code: '1103',
    name: 'SESSION_TOKEN_INVALID',
    method: 'auth.refresh_session',
    message: 'Session token expired before refresh window and must be reissued.',
    traceId: 'trc-demo-20260407-1735',
    occurredAt: '17:35:09',
  },
  {
    id: 'auth-3',
    code: '1105',
    name: 'DEVICE_NOT_IN_AUTH_SCOPE',
    method: 'device.open',
    message: 'Requested device is outside the current scope grant.',
    traceId: 'trc-demo-20260407-1744',
    occurredAt: '17:44:58',
  },
];

export const capturePresetCards: InfoCardItem[] = [
  {
    id: 'take-file',
    eyebrow: 'capture.take',
    title: 'File output preset',
    description: 'jpeg · 300dpi · auto_crop=true · auto_deskew=true',
    meta: '/tmp/sdk-demo/capture-042.jpg',
    tone: 'success',
    state: 'success',
    tags: ['device'],
  },
  {
    id: 'take-base64',
    eyebrow: 'capture.take_base64',
    title: 'Base64 return preset',
    description: 'png · 200dpi · for browser-only pipeline handoff',
    meta: 'used by image.process_page / ocr.recognize',
    tone: 'primary',
    state: 'idle',
    tags: ['device'],
  },
  {
    id: 'rerun',
    eyebrow: 'capture.take',
    title: 'Recommended rerun policy',
    description: 'preserve scan area + page fill + turn detect between retries',
    meta: 'quick re-execute after parameter tweak',
    tone: 'info',
    state: 'running',
    tags: ['device'],
  },
];

export const captureStrategyCards: InfoCardItem[] = [
  {
    id: 'turn-detect',
    eyebrow: 'capture.set_turn_detect',
    title: 'Turn detection',
    description: 'sensitivity=balanced · cooldown=300ms',
    meta: 'helps desk scanning flows',
    tone: 'warning',
    state: 'success',
  },
  {
    id: 'scan-area',
    eyebrow: 'capture.set_scan_area',
    title: 'Scan area',
    description: 'x=92 y=64 w=1820 h=982',
    meta: 'reused by OCR and conversion demo lanes',
    tone: 'primary',
    state: 'idle',
  },
  {
    id: 'page-fill',
    eyebrow: 'capture.set_page_fill',
    title: 'Page fill and repair',
    description: 'page_fill=paper-white · edge_repair=soft',
    meta: 'recommended for wrinkled pages',
    tone: 'success',
    state: 'success',
  },
];

export const capturePreviewMetrics: PreviewMetric[] = [
  { label: 'Output', value: 'capture-042.jpg', monospace: true },
  { label: 'Resolution', value: '2480 x 3508' },
  { label: 'Rectangles', value: '2 detected' },
  { label: 'Barcode', value: 'QR_CODE · 5W0D-2A4K-8891' },
];

export const captureEvents: TimelineItem[] = [
  {
    id: 'capture-event-1',
    title: 'capture.detected_rectangle',
    detail: 'page-1 · x=118 y=96 w=2028 h=2870',
    meta: '18:31:55',
    tone: 'success',
  },
  {
    id: 'capture-event-2',
    title: 'capture.turn_detected',
    detail: 'page-2 · angle=6.4deg · confidence=0.91',
    meta: '18:32:04',
    tone: 'warning',
  },
];

export const imageInputCards: InfoCardItem[] = [
  {
    id: 'path',
    eyebrow: 'local path',
    title: 'File path import',
    description: '/tmp/sdk-demo/capture-042.jpg',
    meta: 'manual source path',
    tone: 'success',
  },
  {
    id: 'capture',
    eyebrow: 'last capture',
    title: 'Import last capture result',
    description: 'capture-042.jpg · page fill applied',
    meta: 'one-click handoff',
    tone: 'primary',
  },
  {
    id: 'previous',
    eyebrow: 'previous output',
    title: 'Re-process last output',
    description: 'image-process-017.png · normalized + watermark',
    meta: 'supports chaining',
    tone: 'info',
  },
];

export const imageOperationCards: InfoCardItem[] = [
  {
    id: 'normalize',
    eyebrow: 'normalize',
    title: 'Normalize',
    description: 'auto contrast + paper cleanup + flatten shadows',
    tone: 'success',
    status: 'ga',
  },
  {
    id: 'watermark',
    eyebrow: 'add_watermark',
    title: 'Watermark',
    description: 'text watermark · bottom-right · 24% opacity',
    tone: 'primary',
    status: 'ga',
  },
  {
    id: 'merge',
    eyebrow: 'merge',
    title: 'Merge',
    description: 'vertical stack · gap=24px · paper=A4',
    tone: 'warning',
    status: 'beta',
  },
  {
    id: 'erase-handwriting',
    eyebrow: 'erase_handwriting',
    title: 'Erase handwriting',
    description: 'needs provider entitlement before submit is allowed',
    tone: 'neutral',
    status: 'planned',
    tags: ['provider', 'account'],
  },
];

export const imageComparisonMetrics: PreviewMetric[] = [
  { label: 'Input', value: 'capture-042.jpg', monospace: true },
  { label: 'Output', value: 'image-process-017.png', monospace: true },
  { label: 'Pipeline', value: 'normalize -> watermark -> merge' },
  { label: 'Output path', value: '/tmp/sdk-demo/image-process-017.png', monospace: true },
];

export const ocrWorkbenchCards: InfoCardItem[] = [
  {
    id: 'recognize',
    eyebrow: 'ocr.recognize',
    title: 'Single file OCR',
    description: 'language=zh-CN,en · source=capture-042.jpg',
    meta: 'active template',
    tone: 'success',
    state: 'success',
  },
  {
    id: 'recognize-batch',
    eyebrow: 'ocr.recognize_batch',
    title: 'Batch OCR',
    description: '3 source files · ordered output by page index',
    meta: 'queued batch sample',
    tone: 'primary',
    state: 'running',
  },
  {
    id: 'barcode',
    eyebrow: 'recognition.barcode',
    title: 'Barcode recognition',
    description: 'formats=QR_CODE,CODE128 · overlay points enabled',
    meta: 'uses last capture result',
    tone: 'warning',
    state: 'success',
  },
];

export const ocrBlockRows: TableRow[] = [
  {
    id: 'ocr-block-1',
    cells: {
      type: 'paragraph',
      value: 'SDK Demo Site Validation Flow',
      confidence: '0.99',
      language: 'en',
    },
  },
  {
    id: 'ocr-block-2',
    cells: {
      type: 'paragraph',
      value: '授权状态：已通过 / 设备范围：dev-01',
      confidence: '0.96',
      language: 'zh-CN',
    },
  },
  {
    id: 'ocr-block-3',
    cells: {
      type: 'barcode',
      value: '5W0D-2A4K-8891',
      confidence: '0.94',
      language: 'binary',
    },
  },
];

export const ocrEvents: TimelineItem[] = [
  {
    id: 'ocr-event-1',
    title: 'recognition.barcode_detected',
    detail: 'QR_CODE · 5W0D-2A4K-8891 · points=4',
    meta: '18:35:19',
    tone: 'success',
  },
  {
    id: 'ocr-event-2',
    title: 'recognition.set_realtime_barcode',
    detail: 'enabled=true · debounce=180ms',
    meta: '18:35:44',
    tone: 'primary',
  },
];

export const fileTemplateCards: InfoCardItem[] = [
  {
    id: 'image-pdf',
    eyebrow: 'jpg -> png',
    title: 'Image format conversion',
    description: 'file.convert handles image format changes outside paper/color processing',
    tone: 'success',
    status: 'ga',
  },
  {
    id: 'images-pdf',
    eyebrow: 'images -> pdf',
    title: 'Multi image bundle',
    description: '3 sources · preserve ordering · append bookmarks',
    tone: 'primary',
    status: 'ga',
  },
  {
    id: 'image-ofd',
    eyebrow: 'image -> ofd',
    title: 'OFD export',
    description: 'enterprise showcase target with metadata placeholder',
    tone: 'warning',
    status: 'beta',
  },
  {
    id: 'upload-planned',
    eyebrow: 'file.upload',
    title: 'Upload / print / metadata',
    description: 'visible as protocol reserve with disabled submit path',
    tone: 'neutral',
    status: 'planned',
  },
];

export const fileWorkspaceCards: InfoCardItem[] = [
  {
    id: 'file-info',
    eyebrow: 'file.info',
    title: 'Inspect output file',
    description: '/tmp/sdk-demo/demo-bundle.pdf · size=12.4MB · pages=3',
    tone: 'success',
    state: 'success',
  },
  {
    id: 'file-rename',
    eyebrow: 'file.rename',
    title: 'Rename in place',
    description: 'demo-bundle.pdf -> demo-bundle-final.pdf',
    tone: 'info',
    state: 'idle',
  },
  {
    id: 'file-mkdir',
    eyebrow: 'file.mkdir',
    title: 'Prepare export directory',
    description: '/tmp/sdk-demo/exports/ofd',
    tone: 'primary',
    state: 'success',
  },
];

export const saneAvailabilityCards: InfoCardItem[] = [
  {
    id: 'availability',
    eyebrow: '1700 SANE_NOT_AVAILABLE',
    title: 'Graceful fallback active',
    description: 'Current host started without libSANE bindings, so submit actions stay visible but blocked.',
    meta: 'UI remains non-destructive',
    tone: 'warning',
    state: 'blocked',
  },
  {
    id: 'provider',
    eyebrow: 'sane.list',
    title: 'Provider bridge',
    description: 'third-party scanner bridge remains decoupled from built-in CZUR devices',
    meta: 'separate capability domain',
    tone: 'info',
    state: 'planned',
    tags: ['provider'],
  },
];

export const saneDeviceRows: TableRow[] = [
  {
    id: 'sane-device-1',
    cells: {
      name: 'epson2:net:192.168.1.40',
      model: 'Epson DS-530',
      type: 'sheetfed scanner',
      status: 'unavailable',
    },
  },
  {
    id: 'sane-device-2',
    cells: {
      name: 'airscan:eSCL:HP OfficeJet 8020',
      model: 'HP OfficeJet 8020',
      type: 'network scanner',
      status: 'planned',
    },
  },
];

export const saneOptionRows: TableRow[] = [
  {
    id: 'sane-opt-1',
    cells: {
      option: 'mode',
      value: 'Color',
      type: 'string',
      status: 'configurable',
    },
  },
  {
    id: 'sane-opt-2',
    cells: {
      option: 'resolution',
      value: '300',
      type: 'int',
      status: 'configurable',
    },
  },
  {
    id: 'sane-opt-3',
    cells: {
      option: 'source',
      value: 'ADF Duplex',
      type: 'string',
      status: 'provider-dependent',
    },
  },
];

export const saneEvents: TimelineItem[] = [
  {
    id: 'sane-event-1',
    title: 'sane.scan_status',
    detail: 'blocked=true · code=1700 · waiting for host support',
    meta: '18:36:08',
    tone: 'warning',
  },
  {
    id: 'sane-event-2',
    title: 'sane.scan_file',
    detail: 'planned placeholder retains output contract preview only',
    meta: '18:36:22',
    tone: 'info',
  },
];

export const protocolTemplateCards: InfoCardItem[] = [
  {
    id: 'tpl-ping',
    eyebrow: 'system.ping',
    title: 'Transport sanity',
    description: 'smallest request envelope used for anonymous command-channel verification',
    tone: 'success',
  },
  {
    id: 'tpl-device',
    eyebrow: 'device.list',
    title: 'Device inventory',
    description: 'loads mock table model and device scope validation',
    tone: 'primary',
  },
  {
    id: 'tpl-capture',
    eyebrow: 'capture.take',
    title: 'Capture walkthrough',
    description: 'full payload with file output parameters and scan heuristics',
    tone: 'warning',
  },
  {
    id: 'tpl-convert',
    eyebrow: 'file.convert',
    title: 'Conversion contract',
    description: 'maps source bundle to PDF target in one request envelope',
    tone: 'info',
  },
];

export const protocolRequestSnippet: JsonSnippet = {
  id: 'protocol-request',
  title: 'capture.take request',
  caption: 'Editable payload surface kept in mock mode for v1.',
  payload: `{
  "request_id": "req-cap-064",
  "method": "capture.take",
  "params": {
    "device_id": "dev-01",
    "output_mode": "file",
    "target_path": "/tmp/sdk-demo/capture-042.jpg",
    "format": "jpeg",
    "dpi": 300,
    "auto_crop": true,
    "auto_deskew": true,
    "detect_barcode": true
  },
  "client": {
    "name": "sdk-demo-site",
    "version": "2.0.0"
  }
}`,
};

export const protocolResponseSnippet: JsonSnippet = {
  id: 'protocol-response',
  title: 'capture.take response',
  caption: 'Friendly panels and raw JSON remain visible at the same time.',
  payload: `{
  "request_id": "req-cap-064",
  "code": 1902,
  "message": "PROVIDER_CALL_FAILED",
  "trace_id": "trc-demo-20260407-1832",
  "data": {
    "retry_after_ms": 300,
    "active_lane": "preview"
  },
  "ts": "2026-04-07T18:32:17Z"
}`,
};

export const protocolHistory: RequestHistoryItem[] = [
  {
    id: 'proto-h-1',
    method: 'system.ping',
    requestId: 'req-system-001',
    code: '0',
    traceId: 'trc-demo-20260407-1801',
    duration: '14ms',
    state: 'success',
  },
  {
    id: 'proto-h-2',
    method: 'device.list',
    requestId: 'req-device-022',
    code: '0',
    traceId: 'trc-demo-20260407-1824',
    duration: '33ms',
    state: 'success',
  },
  {
    id: 'proto-h-3',
    method: 'capture.take',
    requestId: 'req-cap-064',
    code: '1902',
    traceId: 'trc-demo-20260407-1832',
    duration: '142ms',
    state: 'error',
  },
];

export const resultCenterMetrics: MetricCardItem[] = [
  {
    id: 'requests',
    value: '184',
    labelKey: 'common.recentRequests',
    detail: 'Session-local request cache retained by the drawer and center page',
    trend: '14 failed',
    tone: 'primary',
  },
  {
    id: 'events',
    value: '612',
    labelKey: 'common.recentEvents',
    detail: 'Realtime events aggregated across device, stream, capture, OCR, and SANE lanes',
    trend: '18 filtered',
    tone: 'success',
  },
  {
    id: 'errors',
    value: '14',
    labelKey: 'common.recentErrors',
    detail: 'Distinct errors worth carrying into QA summary export',
    trend: '3 blocker-class',
    tone: 'warning',
  },
];

export const sharedJsonSnippets: Record<string, JsonSnippet> = {
  quickStart: {
    id: 'quick-start-json',
    title: 'system.capabilities sample',
    caption: 'Quick start keeps the friendly cards and raw JSON side by side.',
    payload: `{"ga":["system","auth","capture","ocr"],"beta":["stream","image","file"],"planned":["sane"]}`,
  },
  capture: {
    id: 'capture-json',
    title: 'capture.take result',
    caption: 'Capture preview links directly into OCR and image processing pages.',
    payload: `{"output_path":"/tmp/sdk-demo/capture-042.jpg","width":2480,"height":3508,"barcodes":["5W0D-2A4K-8891"]}`,
  },
  image: {
    id: 'image-json',
    title: 'image.process_page result',
    caption: 'Paper processing and color mode can now be called independently.',
    payload: `{"output_path":"/tmp/sdk-demo/page_processed.jpg","page_processing":"single_page","output_format":"jpg"}`,
  },
  ocr: {
    id: 'ocr-json',
    title: 'ocr.recognize result',
    caption: 'Blocks, language, and confidence are preserved in raw output.',
    payload: `{"text":"SDK Demo Site Validation Flow","language":"en","confidence":0.99,"page_count":1}`,
  },
  file: {
    id: 'file-json',
    title: 'file.convert result',
    caption: 'Format conversion is separated from paper processing and color mode.',
    payload: `{"output_path":"/tmp/sdk-demo/converted.png","output_format":"png","asset":{"asset_id":"asset-converted","content_type":"image/png"}}`,
  },
  sane: {
    id: 'sane-json',
    title: 'sane.list result',
    caption: 'Even blocked flows keep a stable envelope in the UI.',
    payload: `{"code":1700,"name":"SANE_NOT_AVAILABLE","available":false,"devices":[]}`,
  },
};

export function stateValue(state: ExecutionState): string {
  return state;
}
