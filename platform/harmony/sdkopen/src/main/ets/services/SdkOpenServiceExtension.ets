import { AppServiceExtensionAbility, Want } from '@kit.AbilityKit';
import { fileIo } from '@kit.CoreFileKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import resourceManager from '@ohos.resourceManager';
import rpc from '@ohos.rpc';
import sdkOpen from 'libsdkopen_harmony.so';

const DOMAIN = 0x0000;
const TAG = 'SdkOpenService';
const DESCRIPTOR = 'com.czur.sdkopen.Service';
const REQUEST_STATUS = 1;
const RAW_WEB_ROOT = 'sdkopen_web';
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

class SdkOpenRemoteObject extends rpc.RemoteObject {
  constructor() {
    super(DESCRIPTOR);
  }

  onRemoteMessageRequest(code: number, data: rpc.MessageSequence,
    reply: rpc.MessageSequence, options: rpc.MessageOption): boolean {
    if (code !== REQUEST_STATUS) {
      return false;
    }
    reply.writeString(String(sdkOpen.status()));
    return true;
  }
}

export default class SdkOpenServiceExtension extends AppServiceExtensionAbility {
  private remoteObject: SdkOpenRemoteObject = new SdkOpenRemoteObject();

  onCreate(want: Want): void {
    try {
      const webRoot = `${this.context.filesDir}/sdk-open/web`;
      const workDir = `${this.context.filesDir}/sdk-open/runtime`;
      copyRawTree(this.context.resourceManager, RAW_WEB_ROOT, webRoot);
      ensureDirectory(workDir);
      stageCzcvModels(this.context.resourceManager, workDir);
      stageCzocrModels(this.context.resourceManager, workDir);
      const result = String(sdkOpen.start(workDir, webRoot));
      if (!result.includes('"success":true')) {
        throw new Error(`sdk_open start failed: ${result}`);
      }
      hilog.info(DOMAIN, TAG,
        'SDK Open service runtime started: admin=17080 demo=17081 asset=17082 command=17090 video=17091');
    } catch (error) {
      hilog.error(DOMAIN, TAG, 'SDK Open service startup failed: %{public}s', String(error));
    }
  }

  onDestroy(): void {
    try {
      sdkOpen.stop();
      hilog.info(DOMAIN, TAG, 'SDK Open service runtime stopped');
    } catch (error) {
      hilog.warn(DOMAIN, TAG, 'SDK Open service stop failed: %{public}s', String(error));
    }
  }

  onConnect(want: Want): rpc.RemoteObject {
    hilog.info(DOMAIN, TAG, 'SDK Open service connected');
    return this.remoteObject;
  }

  onDisconnect(want: Want): void {
    hilog.info(DOMAIN, TAG, 'SDK Open service disconnected');
  }
}
