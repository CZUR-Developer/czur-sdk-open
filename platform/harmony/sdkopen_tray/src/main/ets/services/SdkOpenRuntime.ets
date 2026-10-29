import { common } from '@kit.AbilityKit';
import { fileIo } from '@kit.CoreFileKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import resourceManager from '@ohos.resourceManager';
import sdkOpen from 'libsdkopen_harmony.so';
import { UsbHostTransport } from './UsbHostTransport';
import { TrayRuntimeControl } from '../common/TrayServiceControl';

const TAG = 'SdkOpenRuntime';
const RAW_WEB_ROOT = 'sdkopen_web';
const RAW_DEVICE_CONFIG = 'sdkopen_device_config.conf';
const RAW_CZCV_MODEL_ROOT = 'sdkopen_czcv_model';
const CZCV_MODEL_STAMP = '.sdkopen-czcv-model-v1';
const RAW_CZOCR_MODEL_ROOT = 'sdkopen_czocr_model';
const CZOCR_MODEL_STAMP = '.sdkopen-czocr-model-20261029B';

async function ensureDirectory(path: string): Promise<void> {
  try {
    await fileIo.mkdir(path, true);
  } catch (error) {
    if (!String(error).includes('File exists')) {
      throw new Error(String(error));
    }
  }
}

async function writeBytes(path: string, bytes: Uint8Array, checkStartup: () => void): Promise<void> {
  checkStartup();
  const file = await fileIo.open(path,
    fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
  try {
    let offset = 0;
    while (offset < bytes.length) {
      checkStartup();
      const end = Math.min(bytes.length, offset + 1024 * 1024);
      const output = bytes.buffer.slice(bytes.byteOffset + offset, bytes.byteOffset + end);
      const written = await fileIo.write(file.fd, output, { offset });
      if (written <= 0 || written > output.byteLength) {
        throw new Error(`Cannot finish writing runtime resource: ${path}`);
      }
      offset += written;
    }
  } finally {
    await fileIo.close(file);
  }
}

async function copyRawTree(manager: resourceManager.ResourceManager,
  rawPath: string, targetPath: string, checkStartup: () => void): Promise<void> {
  checkStartup();
  let children: Array<string>;
  try {
    children = await manager.getRawFileList(rawPath);
  } catch (_) {
    await writeBytes(targetPath, await manager.getRawFileContent(rawPath), checkStartup);
    return;
  }

  checkStartup();
  await ensureDirectory(targetPath);
  for (let index = 0; index < children.length; index++) {
    const name = children[index];
    await copyRawTree(manager, `${rawPath}/${name}`, `${targetPath}/${name}`, checkStartup);
  }
}

async function fileExists(path: string): Promise<boolean> {
  try {
    await fileIo.stat(path);
    return true;
  } catch (_) {
    return false;
  }
}

async function stageCzcvModels(manager: resourceManager.ResourceManager,
  workDir: string, checkStartup: () => void): Promise<void> {
  const modelDir = `${workDir}/czcv_model`;
  const stampPath = `${modelDir}/${CZCV_MODEL_STAMP}`;
  if (!await fileExists(stampPath)) {
    await copyRawTree(manager, RAW_CZCV_MODEL_ROOT, modelDir, checkStartup);
    await writeBytes(stampPath, new Uint8Array([1]), checkStartup);
  }
}

async function stageCzocrModels(manager: resourceManager.ResourceManager,
  workDir: string, checkStartup: () => void): Promise<void> {
  const modelDir = `${workDir}/models`;
  const stampPath = `${modelDir}/${CZOCR_MODEL_STAMP}`;
  if (await fileExists(stampPath)) {
    return;
  }
  const naiveLib = `${modelDir}/libczcv_ocr_naive.so`;
  if (await fileExists(naiveLib)) {
    await fileIo.unlink(naiveLib);
  }
  await copyRawTree(manager, RAW_CZOCR_MODEL_ROOT, modelDir, checkStartup);
  await writeBytes(stampPath, new Uint8Array([1]), checkStartup);
}

// Only the resident tray UIAbility imports this module. Keep one USB dispatcher
// for its N-API environment across runtime stop/start cycles.
export class SdkOpenRuntime implements TrayRuntimeControl {
  private context: common.UIAbilityContext | null = null;
  private readonly usbHost: UsbHostTransport = new UsbHostTransport();
  private lifecycle: Promise<void> = Promise.resolve();
  private shutdown?: Promise<void>;
  private exiting: boolean = false;
  private running: boolean = false;
  private cleanupNeeded: boolean = false;

  initialize(context: common.UIAbilityContext): void {
    this.context = context;
  }

  private serializeLifecycle(action: () => Promise<void>): Promise<void> {
    const result = this.lifecycle.then(action);
    this.lifecycle = result.catch((): void => {});
    return result;
  }

  start(): Promise<void> {
    return this.serializeLifecycle(async (): Promise<void> => {
      if (this.exiting) {
        throw new Error('The tray runtime is exiting');
      }
      if (this.running) {
        return;
      }
      // A failed stop must be retried before acquiring another USB session.
      if (this.cleanupNeeded) {
        await this.stopRuntime();
      }
      await this.startRuntime();
    });
  }

  stop(): Promise<void> {
    return this.serializeLifecycle((): Promise<void> => this.stopRuntime());
  }

  stopForExit(): Promise<void> {
    if (this.shutdown) {
      return this.shutdown;
    }
    this.exiting = true;
    this.shutdown = this.stop().catch((error: Object): void => {
      // AbilityStage may cancel a normal exit on failure. Allow cleanup retry.
      this.exiting = false;
      this.shutdown = undefined;
      throw new Error(String(error));
    });
    return this.shutdown;
  }

  private async startRuntime(): Promise<void> {
    const context = this.context;
    if (!context) {
      throw new Error('The resident tray UIAbility is not initialized');
    }
    try {
      const checkStartup = (): void => {
        if (this.exiting) {
          throw new Error('The tray runtime is exiting');
        }
      };
      const webRoot = `${context.filesDir}/sdk-open/web`;
      const workDir = `${context.filesDir}/sdk-open/runtime`;
      await copyRawTree(context.resourceManager, RAW_WEB_ROOT, webRoot, checkStartup);
      await ensureDirectory(workDir);
      await ensureDirectory(`${workDir}/configs`);
      await copyRawTree(context.resourceManager, RAW_DEVICE_CONFIG,
        `${workDir}/configs/device_config_compact.conf`, checkStartup);
      await stageCzcvModels(context.resourceManager, workDir, checkStartup);
      await stageCzocrModels(context.resourceManager, workDir, checkStartup);
      checkStartup();
      this.cleanupNeeded = true;
      if (!this.usbHost.attach()) {
        throw new Error('Cannot install the USB Host transport');
      }
      const result = String(await sdkOpen.start(workDir, webRoot));
      if (!result.includes('"success":true')) {
        throw new Error(`sdk_open start failed: ${result}`);
      }
      this.running = true;
      hilog.info(0, TAG,
        'SDK Open tray runtime started: admin=17080 demo=17081 asset=17082 command=17090 video=17091');
    } catch (error) {
      try {
        await this.stopRuntime();
      } catch (cleanupError) {
        throw new Error(`${String(error)}; startup cleanup failed: ${String(cleanupError)}`);
      }
      throw new Error(String(error));
    }
  }

  private async stopRuntime(): Promise<void> {
    if (!this.cleanupNeeded) {
      return;
    }
    this.running = false;
    // Keep the ArkTS dispatcher alive while native shutdown releases devices.
    const result = String(await sdkOpen.stop());
    if (!result.includes('"success":true')) {
      throw new Error(`sdk_open stop failed: ${result}`);
    }
    await this.usbHost.shutdownAfterNativeStop();
    this.cleanupNeeded = false;
    hilog.info(0, TAG, 'SDK Open tray runtime stopped');
  }
}

export const sdkOpenRuntime: SdkOpenRuntime = new SdkOpenRuntime();
