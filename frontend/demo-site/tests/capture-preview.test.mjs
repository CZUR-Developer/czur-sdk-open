import assert from 'node:assert/strict';
import test from 'node:test';

import { selectCaptureViewerAsset } from '../src/utils/capture-preview.js';

function asset(kind, overrides = {}) {
  return {
    assetId: `asset-${kind}`,
    kind,
    contentType: 'image/jpeg',
    url: `/api/assets/task/${kind}`,
    downloadUrl: '',
    ...overrides,
  };
}

for (const stage of ['original', 'page_processed', 'color_processed', 'final']) {
  test(`${stage} thumbnail opens its full-size image`, () => {
    const thumbnail = asset(`${stage}_thumbnail`);
    const full = asset(stage);
    assert.equal(selectCaptureViewerAsset([thumbnail, asset('unrelated'), full], thumbnail.assetId), full);
  });
}

test('split pages keep their page suffix even when assets are in a different order', () => {
  const left = asset('final_left');
  const right = asset('final_right');
  const thumbnails = [asset('final_thumbnail_right'), asset('final_thumbnail_left')];
  const assets = [left, right, ...thumbnails];
  assert.equal(selectCaptureViewerAsset(assets, thumbnails[0].assetId), right);
  assert.equal(selectCaptureViewerAsset(assets, thumbnails[1].assetId), left);
});

test('TIFF uses the full-size color result from the same page', () => {
  const thumbnail = asset('final_thumbnail_page-2');
  const color = asset('color_processed_page-2');
  const assets = [
    thumbnail,
    asset('final_page-2', { contentType: 'image/tiff' }),
    asset('color_processed_page-1'),
    asset('original'),
    color,
  ];
  assert.equal(selectCaptureViewerAsset(assets, thumbnail.assetId), color);
});

test('TIFF can use the same-page page-processed image if no color image is accessible', () => {
  const thumbnail = asset('final_thumbnail_right');
  const page = asset('page_processed_right', { url: '', downloadUrl: '/download/right' });
  assert.equal(selectCaptureViewerAsset([
    thumbnail,
    asset('final_right', { contentType: 'image/tiff' }),
    asset('color_processed_right', { url: '' }),
    page,
  ], thumbnail.assetId), page);
});

test('missing full-size image never falls back to a thumbnail or another page', () => {
  const thumbnail = asset('final_thumbnail_right');
  assert.equal(selectCaptureViewerAsset([
    thumbnail,
    asset('final_left'),
    asset('original'),
    asset('page_processed_left'),
  ], thumbnail.assetId), undefined);
  assert.equal(selectCaptureViewerAsset([], thumbnail.assetId), undefined);
});

test('non-thumbnail assets are selected by ID, including multiple enhanced final images', () => {
  const first = asset('final', { assetId: 'asset-enhanced-final-1' });
  const second = asset('final', { assetId: 'asset-enhanced-final-2', contentType: 'image/png; charset=binary' });
  assert.equal(selectCaptureViewerAsset([first, second], second.assetId), second);
});

test('a page ID containing thumbnail is not treated as a thumbnail stage', () => {
  const full = asset('color_processed_page_thumbnail_1');
  assert.equal(selectCaptureViewerAsset([full], full.assetId), full);
});
