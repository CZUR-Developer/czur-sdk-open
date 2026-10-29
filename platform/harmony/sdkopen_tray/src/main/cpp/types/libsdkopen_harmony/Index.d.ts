export const start: (workDir: string, webRoot: string) => Promise<string>;
export const stop: () => Promise<string>;
export const status: () => string;
export const attachUsbHost: (dispatcher: (requestJson: string) => void) => boolean;
export const completeUsbHostRequest: (requestId: number, success: boolean,
  resultJson: string, data?: ArrayBuffer) => boolean;
export const submitUsbHostTransfer: (session: number, sequence: number, data: ArrayBuffer,
  packetLengths: Uint32Array, actualLengths: Uint32Array, statuses: Uint32Array) => boolean;
export const startUsbHostNativeIso: (fd: number, session: number) => Promise<string>;
export const stopUsbHostNativeIso: (session: number) => Promise<string>;
export const releaseUsbHostFd: (fd: number) => boolean;
export const failUsbHost: (session: number, message: string) => boolean;
