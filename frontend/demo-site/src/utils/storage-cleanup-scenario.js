const OCR_TERMINAL_STATUSES = new Set(['completed', 'failed', 'cancelled']);
const OCR_BATCH_PRESETS = [4, 8, 16, 30];

export function snapshotCommandRequest(request) {
  // 请求发送后立即生成独立快照，确保页面展示的 request_id、trace_id 等字段与实际指令完全一致。
  return JSON.parse(JSON.stringify(request));
}

export function isOcrTerminalStatus(status) {
  return OCR_TERMINAL_STATUSES.has(normalizeStatus(status));
}

export function nextOcrBatchSize(current) {
  const currentIndex = OCR_BATCH_PRESETS.indexOf(Number(current));
  if (currentIndex < 0) {
    return OCR_BATCH_PRESETS[0];
  }
  return OCR_BATCH_PRESETS[currentIndex + 1] ?? null;
}

export function classifyBusyCleanup(response) {
  const code = Number(response?.code);
  const activeOcr = Number(response?.data?.active?.ocr ?? 0);
  if (code === 1800) {
    return activeOcr > 0
      ? { outcome: 'passed', reason: 'ocr-active' }
      : { outcome: 'failed', reason: 'busy-without-ocr' };
  }
  if (code === 0) {
    // OCR 已在清理拿到生命周期锁前结束，这属于未命中窗口，不应误报为后端失败。
    return { outcome: 'missed-window', reason: 'cleanup-succeeded' };
  }
  return { outcome: 'failed', reason: 'unexpected-code' };
}

export function classifyPostCleanupQuery(response) {
  const message = typeof response?.message === 'string' ? response.message.toLowerCase() : '';
  return Number(response?.code) === 1002 && message.includes('ocr task not found');
}

export function classifyIdempotentCleanup(response) {
  return Number(response?.code) === 0 && Number(response?.data?.cleared_task_count ?? -1) === 0;
}

function normalizeStatus(status) {
  return typeof status === 'string' ? status.trim().toLowerCase() : '';
}
