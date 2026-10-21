export const SDK_OCR_LANGUAGES = Object.freeze([
  'auto',
  'ch', 'hu', 'en', 'rs_latin', 'fr', 'id', 'de', 'oc', 'japan', 'is', 'korean', 'lt',
  'chinese_cht', 'mi', 'af', 'ms', 'it', 'nl', 'es', 'no', 'bs', 'pl', 'pt', 'sk', 'cs',
  'sl', 'cy', 'sq', 'da', 'sv', 'et', 'sw', 'ga', 'tl', 'hr', 'tr', 'uz', 'la', 'ru', 'be',
  'uk', 'th', 'el', 'ku', 'lv', 'mt', 'ro', 'fi', 'eu', 'gl', 'lb', 'rm', 'ca', 'qu', 'te',
  'rs_cyrillic', 'bg', 'mn', 'ady', 'kbd', 'av', 'inh', 'ce', 'lki', 'lez', 'kk', 'ky', 'tg',
  'mk', 'tt', 'ba', 'udm', 'kv', 'bua', 'xal', 'tyv', 'sah', 'kaa', 'ar', 'hi', 'mr', 'ne',
  'bh', 'mai', 'bho', 'mah', 'new', 'gom', 'ta',
]);

const DISPLAY_LANGUAGE_CODES = Object.freeze({
  ch: 'zh-Hans',
  chinese_cht: 'zh-Hant',
  japan: 'ja',
  korean: 'ko',
  rs_cyrillic: 'sr-Cyrl',
  rs_latin: 'sr-Latn',
});

export function getOcrLanguageLabel(language, locale, autoLabel) {
  if (language === 'auto') {
    return `${autoLabel} (auto)`;
  }
  const displayCode = DISPLAY_LANGUAGE_CODES[language] ?? language;
  try {
    const displayName = new Intl.DisplayNames([locale], { type: 'language', fallback: 'none' }).of(displayCode);
    if (displayName && displayName.toLowerCase() !== displayCode.toLowerCase()) {
      return `${displayName} (${language})`;
    }
  } catch {
    // Keep every engine code selectable even if the browser cannot localize a rare language code.
  }
  return language;
}

export function buildOcrExtractTextParams(inputUploadId, language = 'auto') {
  return {
    input_upload_id: inputUploadId,
    language,
  };
}

export function buildOcrRecognizeParams({
  inputUploadIds,
  exportType,
  outputPath,
  outputDir,
  format,
  encoding,
  quality,
  language = 'auto',
}) {
  return {
    input_upload_ids: inputUploadIds,
    ...(exportType === 'single-page' ? { output_dir: outputDir } : { output_path: outputPath }),
    format,
    exportType,
    encoding,
    quality,
    language,
  };
}
