import { AppServiceExtensionAbility, Want } from '@kit.AbilityKit';
import { fileIo } from '@kit.CoreFileKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import resourceManager from '@ohos.resourceManager';
import rpc from '@ohos.rpc';
import process from '@ohos.process';
import sdkOpen from 'libsdkopen_harmony.so';
import { UsbHostTransport } from './UsbHostTransport';

const DOMAIN = 0x0000;
const TAG = 'SdkOpenService';
const DESCRIPTOR = 'com.czur.sdkopen.Service';
const REQUEST_STATUS = 1;
const REQUEST_PREPARE_STOP = 2;
const RAW_WEB_ROOT = 'sdkopen_web';
const RAW_DEVICE_CONFIG = 'sdkopen_device_config.conf';
const RAW_CZCV_MODEL_ROOT = 'sdkopen_czcv_model';
const CZCV_MODEL_STAMP = '.sdkopen-czcv-model-v1';
const RAW_CZOCR_MODEL_ROOT = 'sdkopen_czocr_model';
const CZOCR_MODEL_STAMP = '.sdkopen-czocr-model-20261029B';

function ensureDirectory(path: string): void {
  try {
    fileIo.mkdirSync(path, true);
  } catch (error) {
    if (!String(error).includes('File exists')) {
      throw new Error(String(error));
    }
  }
}

function writeBytes(path: string, bytes: Uint8Array): void {
  const fd = fileIo.openSync(path,
    fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC).fd;
  try {
    const output = new Uint8Array(new ArrayBuffer(bytes.length));
    for (let index = 0; index < bytes.length; index++) {
      output[index] = bytes[index];
    }
    fileIo.writeSync(fd, output.buffer);
  } finally {
    fileIo.closeSync(fd);
  }
}

function copyRawTree(manager: resourceManager.ResourceManager, rawPath: string, targetPath: string): void {
  let children: Array<string>;
  try {
    children = manager.getRawFileListSync(rawPath);
  } catch (_) {
    writeBytes(targetPath, manager.getRawFileContentSync(rawPath));
    return;
  }

  ensureDirectory(targetPath);
  for (let index = 0; index < children.length; index++) {
    const name = children[index];
    copyRawTree(manager, `${rawPath}/${name}`, `${targetPath}/${name}`);
  }
}

function fileExists(path: string): boolean {
  try {
    fileIo.statSync(path);
    return true;
  } catch (_) {
    return false;
  }
}

function stageCzcvModels(manager: resourceManager.ResourceManager, workDir: string): void {
  const modelDir = `${workDir}/czcv_model`;
  const stampPath = `${modelDir}/${CZCV_MODEL_STAMP}`;
  if (fileExists(stampPath)) {
    return;
  }
  copyRawTree(manager, RAW_CZCV_MODEL_ROOT, modelDir);
  writeBytes(stampPath, new Uint8Array([1]));
}

function stageCzocrModels(manager: resourceManager.ResourceManager, workDir: string): void {
  const modelDir = `${workDir}/models`;
  const stampPath = `${modelDir}/${CZOCR_MODEL_STAMP}`;
  if (fileExists(stampPath)) {
    return;
  }
  const naiveLib = `${modelDir}/libczcv_ocr_naive.so`;
  if (fileExists(naiveLib)) {
    fileIo.unlinkSync(naiveLib);
  }
  copyRawTree(manager, RAW_CZOCR_MODEL_ROOT, modelDir);
  writeBytes(stampPath, new Uint8Array([1]));
}

// The N-API USB bridge accepts one dispatcher per env. AppService ability
// instances can be recreated in the same process, so keep its ArkTS owner at
// module scope and reuse it across service restarts.
const sharedUsbHost: UsbHostTransport = new UsbHostTransport();

class SdkOpenRemoteObject extends rpc.RemoteObject {
  private readonly prepareStop: () => Promise<void>;

  constructor(prepareStop: () => Promise<void>) {
    super(DESCRIPTOR);
    this.prepareStop = prepareStop;
  }

  onRemoteMessageRequest(code: number, data: rpc.MessageSequence,
    reply: rpc.MessageSequence, options: rpc.MessageOption): boolean | Promise<boolean> {
    if (code === REQUEST_STATUS) {
      reply.writeString(String(sdkOpen.status()));
      return true;
    }
    if (code === REQUEST_PREPARE_STOP) {
      // Capture and validate the caller inside the IPC context, before the
      // first asynchronous boundary. The exported service must not expose a
      // stop primitive to another application.
      if (rpc.IPCSkeleton.getCallingUid() !== process.uid) {
        reply.writeString('{"success":false,"error":"unauthorized caller"}');
        return true;
      }
      return this.prepareStop().then((): boolean => {
        reply.writeString('{"success":true}');
        return true;
      }).catch((error: Object): boolean => {
        reply.writeString(JSON.stringify({ success: false, error: String(error) }));
        return true;
      });
    }
    return false;
  }
}

export default class SdkOpenServiceExtension extends AppServiceExtensionAbility {
  private readonly usbHost: UsbHostTransport = sharedUsbHost;
  private readonly remoteObject: SdkOpenRemoteObject =
    new SdkOpenRemoteObject((): Promise<void> => this.prepareStop());
  private startup: Promise<void> = Promise.resolve();
  private shutdown?: Promise<void>;

  onCreate(want: Want): void {
    this.startup = this.startRuntime().catch((error: Object): void => {
      hilog.error(DOMAIN, TAG, 'SDK Open service startup failed: %{public}s', String(error));
    });
  }

  private async startRuntime(): Promise<void> {
    try {
      const webRoot = `${this.context.filesDir}/sdk-open/web`;
      const workDir = `${this.context.filesDir}/sdk-open/runtime`;
      copyRawTree(this.context.resourceManager, RAW_WEB_ROOT, webRoot);
      ensureDirectory(workDir);
      ensureDirectory(`${workDir}/configs`);
      copyRawTree(this.context.resourceManager, RAW_DEVICE_CONFIG,
        `${workDir}/configs/device_config_compact.conf`);
      stageCzcvModels(this.context.resourceManager, workDir);
      stageCzocrModels(this.context.resourceManager, workDir);
      if (!this.usbHost.attach()) {
        throw new Error('Cannot install the USB Host transport');
      }
      const result = String(await sdkOpen.start(workDir, webRoot));
      if (!result.includes('"success":true')) {
        throw new Error(`sdk_open start failed: ${result}`);
      }
      hilog.info(DOMAIN, TAG,
        'SDK Open service runtime started: admin=17080 demo=17081 asset=17082 command=17090 video=17091');
    } catch (error) {
      try {
        await sdkOpen.stop();
        await this.usbHost.shutdownAfterNativeStop();
      } catch (cleanupError) {
        hilog.warn(DOMAIN, TAG, 'SDK Open startup cleanup failed: %{public}s', String(cleanupError));
      }
      throw new Error(String(error));
    }
  }

  async prepareStop(): Promise<void> {
    if (this.shutdown) {
      return this.shutdown;
    }
    this.shutdown = this.stopRuntime();
    return this.shutdown;
  }

  private async stopRuntime(): Promise<void> {
    await this.startup.catch((): void => {});
    const result = String(await sdkOpen.stop());
    if (!result.includes('"success":true')) {
      throw new Error(`sdk_open stop failed: ${result}`);
    }
    await this.usbHost.shutdownAfterNativeStop();
    hilog.info(DOMAIN, TAG, 'SDK Open service runtime stopped');
  }

  onDestroy(): void {
    // Normal tray shutdown calls REQUEST_PREPARE_STOP and awaits completion
    // before stopping the ability. This is a best-effort fallback for system
    // teardown; the native module cleanup hook also drains ISO ownership.
    this.prepareStop().catch((error: Object): void => {
      sdkOpen.stop().catch((): void => {});
      hilog.warn(DOMAIN, TAG, 'SDK Open service stop failed: %{public}s', String(error));
    });
  }

  onConnect(want: Want): rpc.RemoteObject {
    hilog.info(DOMAIN, TAG, 'SDK Open service connected');
    return this.remoteObject;
  }

  onDisconnect(want: Want): void {
    hilog.info(DOMAIN, TAG, 'SDK Open service disconnected');
  }
}
