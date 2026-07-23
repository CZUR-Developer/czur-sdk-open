const WINDOWS_DEFAULT_OUTPUT_DIR = 'C:\\Users\\Public\\Documents';
const POSIX_DEFAULT_OUTPUT_DIR = '/tmp';

export function resolveOcrOutputPlatform(platform) {
  return typeof platform === 'string' && /win/i.test(platform) ? 'windows' : 'posix';
}

export function currentBrowserPlatform() {
  if (typeof navigator === 'undefined') {
    return '';
  }
  return navigator.userAgentData?.platform || navigator.platform || '';
}

export function buildOcrDefaultOutputPath(format, options = {}) {
  const extension = typeof format === 'string' && format ? format : 'txt';
  return `${buildOcrDefaultOutputBase(options)}.${extension}`;
}

export function buildOcrDefaultOutputDir(options = {}) {
  return buildOcrDefaultOutputBase(options);
}

function buildOcrDefaultOutputBase(options) {
  const platform = resolveOcrOutputPlatform(options.platform ?? currentBrowserPlatform());
  const nowMs = typeof options.nowMs === 'number' ? options.nowMs : Date.now();
  const stamp = nowMs.toString(36);
  const root = platform === 'windows' ? WINDOWS_DEFAULT_OUTPUT_DIR : POSIX_DEFAULT_OUTPUT_DIR;
  const separator = platform === 'windows' ? '\\' : '/';
  return `${root}${separator}czur-sdk-ocr-demo-${stamp}`;
}
