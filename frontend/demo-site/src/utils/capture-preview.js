export function isBrowserRenderableImage(asset) {
  const contentType = asset.contentType.toLowerCase().split(';')[0].trim();
  if (!contentType) {
    return asset.kind.includes('thumbnail') || asset.kind === 'original' || asset.kind.startsWith('page_processed') || asset.kind.startsWith('color_processed');
  }
  return ['image/jpeg', 'image/jpg', 'image/png', 'image/webp', 'image/gif', 'image/bmp'].includes(contentType);
}

export function selectCaptureViewerAsset(assets, previewAssetId) {
  const previewAsset = assets.find((asset) => asset.assetId === previewAssetId);
  if (!previewAsset) {
    return undefined;
  }
  const canDisplay = (asset) => Boolean(asset.url || asset.downloadUrl) && isBrowserRenderableImage(asset);
  const thumbnail = /^(original|page_processed|color_processed|final)_thumbnail(?=_|$)/.exec(previewAsset.kind);
  if (!thumbnail) {
    return canDisplay(previewAsset) ? previewAsset : undefined;
  }

  const stage = thumbnail[1];
  const pageSuffix = previewAsset.kind.slice(thumbnail[0].length);
  // TIFF final outputs cannot be displayed by the browser. Keep the same page
  // when falling back to a full-size image from an earlier processing stage.
  const stages = stage === 'final' ? ['final', 'color_processed', 'page_processed'] : [stage];
  for (const kind of stages) {
    const asset = assets.find((item) => item.kind === `${kind}${pageSuffix}` && canDisplay(item));
    if (asset) {
      return asset;
    }
  }
  return undefined;
}
