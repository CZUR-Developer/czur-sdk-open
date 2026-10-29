import { hapTasks } from '@ohos/hvigor-ohos-plugin';
import fs from 'fs';
import path from 'path';

const sdkRoot = path.resolve(__dirname, '../../..');
const repoRoot = process.env.SDK_OPEN_HOST_SOURCE_ROOT || path.resolve(sdkRoot, '../..');
const thirdPartyRoot = process.env.SDK_OPEN_THIRD_PARTY_ROOT || path.join(repoRoot, 'third_party_repo');
const sourceRoot = path.join(sdkRoot, 'web');
const targetRoot = path.resolve(__dirname, 'src/main/resources/rawfile/sdkopen_web');
const modelSourceRoot = path.join(thirdPartyRoot, 'czcv/models/arm64');
const modelTargetRoot = path.resolve(__dirname, 'src/main/resources/rawfile/sdkopen_czcv_model');
const ocrModelSourceRoot = path.join(thirdPartyRoot, 'czocr/models/x86-64');
const ocrModelTargetRoot = path.resolve(__dirname, 'src/main/resources/rawfile/sdkopen_czocr_model');
const deviceConfigSource = path.join(repoRoot, 'src/module/devices/device_config_compact.conf');
const deviceConfigTarget = path.resolve(__dirname, 'src/main/resources/rawfile/sdkopen_device_config.conf');

function copyTree(source: string, target: string): void {
  if (!fs.existsSync(source)) {
    throw new Error(`sdk_open web assets are missing: ${source}`);
  }
  fs.rmSync(target, { recursive: true, force: true });
  fs.mkdirSync(target, { recursive: true });
  for (const name of fs.readdirSync(source)) {
    if (source === ocrModelSourceRoot &&
      (name === 'libczcv_ocr.so' || name === 'libczcv_ocr_naive.so')) {
      continue;
    }
    const from = path.join(source, name);
    const to = path.join(target, name);
    if (fs.statSync(from).isDirectory()) {
      copyTree(from, to);
    } else {
      fs.copyFileSync(from, to);
    }
  }
}

function syncSdkOpenWeb(): void {
  copyTree(sourceRoot, targetRoot);
  console.log(`[sdk-open-harmony] synced web assets from ${sourceRoot}`);
}

function syncCzcvModels(): void {
  copyTree(modelSourceRoot, modelTargetRoot);
  console.log(`[sdk-open-harmony] synced CZCV models from ${modelSourceRoot}`);
}

function syncCzocrModels(): void {
  copyTree(ocrModelSourceRoot, ocrModelTargetRoot);
  fs.copyFileSync(
    path.join(thirdPartyRoot, 'czocr/lib/ohos/libczcv_ocr.so'),
    path.join(ocrModelTargetRoot, 'libczcv_ocr.so')
  );
  console.log(`[sdk-open-harmony] synced CZOCR models from ${ocrModelSourceRoot}`);
}

syncSdkOpenWeb();
syncCzcvModels();
syncCzocrModels();
fs.mkdirSync(path.dirname(deviceConfigTarget), { recursive: true });
fs.copyFileSync(deviceConfigSource, deviceConfigTarget);

export default {
  system: hapTasks,
  plugins: []
};
