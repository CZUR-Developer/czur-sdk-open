interface CapturePreviewAsset {
  assetId: string;
  kind: string;
  contentType: string;
  url: string;
  downloadUrl: string;
}

export function isBrowserRenderableImage(asset: CapturePreviewAsset): boolean;
export function selectCaptureViewerAsset<T extends CapturePreviewAsset>(assets: T[], previewAssetId: string): T | undefined;
