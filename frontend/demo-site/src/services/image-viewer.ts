import { reactive } from 'vue';

export interface ImageViewerPayload {
  src: string;
  title?: string;
  subtitle?: string;
  alt?: string;
}

interface ImageViewerState extends Required<ImageViewerPayload> {
  open: boolean;
}

export const imageViewerState = reactive<ImageViewerState>({
  open: false,
  src: '',
  title: '',
  subtitle: '',
  alt: '',
});

export function openImageViewer(payload: ImageViewerPayload): void {
  if (!payload.src) {
    return;
  }
  imageViewerState.open = true;
  imageViewerState.src = payload.src;
  imageViewerState.title = payload.title ?? '';
  imageViewerState.subtitle = payload.subtitle ?? '';
  imageViewerState.alt = payload.alt ?? payload.title ?? '';
}

export function closeImageViewer(): void {
  imageViewerState.open = false;
}
