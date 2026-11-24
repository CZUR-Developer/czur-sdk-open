import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  SDK_OCR_LANGUAGES,
  buildOcrExtractTextParams,
  buildOcrRecognizeParams,
} from '../src/utils/ocr-language.js';

const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const demoDirectory = path.resolve(testDirectory, '..');

test('OCR request builders always include the selected language', () => {
  assert.deepEqual(buildOcrExtractTextParams('upload-1', 'en'), {
    input_upload_id: 'upload-1',
    language: 'en',
  });
  assert.deepEqual(buildOcrRecognizeParams({
    inputUploadIds: ['upload-1', 'upload-2'],
    exportType: 'multi-page',
    outputPath: '/tmp/result.txt',
    outputDir: '/tmp/results',
    format: 'txt',
    encoding: 'utf-8',
    quality: 90,
    language: 'chinese_cht',
  }), {
    input_upload_ids: ['upload-1', 'upload-2'],
    output_path: '/tmp/result.txt',
    format: 'txt',
    exportType: 'multi-page',
    encoding: 'utf-8',
    quality: 90,
    language: 'chinese_cht',
  });
  assert.equal(buildOcrExtractTextParams('upload-1').language, 'auto');
});

test('demo languages match the public SDK OCR language header', () => {
  const headerPath = path.resolve(demoDirectory, '../../interfaces/sdk_ocr_language.h');
  const header = fs.readFileSync(headerPath, 'utf8');
  const arrayBody = header.match(/kSupportedSdkOcrLanguages\[\]\s*=\s*\{([\s\S]*?)\};/)?.[1];
  assert.ok(arrayBody, 'kSupportedSdkOcrLanguages was not found in sdk_ocr_language.h');
  const headerLanguages = [...arrayBody.matchAll(/"([^"]+)"/g)].map((match) => match[1]);
  assert.deepEqual(SDK_OCR_LANGUAGES, headerLanguages);
});

test('demo languages match the parent client enabled set when checked out in CapEditor', (context) => {
  const clientPath = path.resolve(demoDirectory, '../../../../views/src/features/ocr/czOcrLanguages.ts');
  if (!fs.existsSync(clientPath)) {
    context.skip('parent CapEditor client source is not present in this checkout');
    return;
  }

  const clientSource = fs.readFileSync(clientPath, 'utf8');
  const definitions = [...clientSource.matchAll(/\{ value: '([^']+)', labelKey:/g)].map((match) => match[1]);
  const disabledBlock = clientSource.match(/CZOCR_DISABLED_LANGUAGE_CODES\s*=\s*\[([\s\S]*?)\]\s*as const/)?.[1];
  assert.ok(disabledBlock, 'CZOCR_DISABLED_LANGUAGE_CODES was not found in czOcrLanguages.ts');
  const disabled = new Set([...disabledBlock.matchAll(/'([^']+)'/g)].map((match) => match[1]));
  const enabled = definitions.filter((language) => !disabled.has(language));
  assert.deepEqual(new Set(SDK_OCR_LANGUAGES), new Set(enabled));
  assert.equal(SDK_OCR_LANGUAGES.length, 90);
});
