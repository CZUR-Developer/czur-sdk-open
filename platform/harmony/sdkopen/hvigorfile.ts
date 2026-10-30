import { hapTasks } from '@ohos/hvigor-ohos-plugin';
import fs from 'fs';
import path from 'path';
import crypto from 'crypto';

const sdkRoot = path.resolve(__dirname, '../../..');
const repoRoot = process.env.SDK_OPEN_HOST_SOURCE_ROOT || path.resolve(sdkRoot, '../..');
const thirdPartyRoot = process.env.SDK_OPEN_THIRD_PARTY_ROOT || path.join(repoRoot, 'third_party_repo');
const sourceRoot = path.join(sdkRoot, 'web');
const targetRoot = path.resolve(__dirname, 'src/main/resources/rawfile/sdkopen_web');
const modelSourceRoot = path.join(thirdPartyRoot, 'czcv/models/arm64');
const modelTargetRoot = path.resolve(__dirname, 'src/main/resources/rawfile/sdkopen_czcv_model');
// Use the supplied x86-64 model data first. The Windows set fills files that
// are still missing from that directory; native code always comes from OHOS.
const ocrModelSourceRoot = path.join(thirdPartyRoot, 'czocr/models/x86-64');
const ocrModelFallbackRoot = path.join(thirdPartyRoot, 'czocr/models/windows64');
const ocrModelTargetRoot = path.resolve(__dirname, 'src/main/resources/rawfile/sdkopen_czocr_model');
const ocrFontSourceRoot = path.join(repoRoot, 'resources/ocr/fonts');
const ocrFontTargetRoot = path.resolve(__dirname, 'src/main/resources/rawfile/sdkopen_ocr_fonts');

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
  copyTree(ocrModelFallbackRoot, ocrModelTargetRoot);
  let overridden = 0;
  for (const name of fs.readdirSync(ocrModelSourceRoot)) {
    const from = path.join(ocrModelSourceRoot, name);
    if (fs.statSync(from).isFile() &&
      (name.endsWith('.model') || name.endsWith('.dat') || name === 'ppocr_keys_v1.txt')) {
      fs.copyFileSync(from, path.join(ocrModelTargetRoot, name));
      overridden++;
    }
  }
  fs.copyFileSync(path.join(thirdPartyRoot, 'czocr/models/loongarch64/license.lic'),
    path.join(ocrModelTargetRoot, 'license.lic'));
  const version = crypto.createHash('sha256');
  for (const name of fs.readdirSync(ocrModelTargetRoot).sort()) {
    version.update(name);
    version.update('\0');
    version.update(fs.readFileSync(path.join(ocrModelTargetRoot, name)));
  }
  fs.writeFileSync(path.join(path.dirname(ocrModelTargetRoot), 'sdkopen_czocr_model_version.txt'),
    version.digest('hex'));
  console.log(`[sdk-open-harmony] synced ${overridden} CZOCR model/data files from ${ocrModelSourceRoot}`);
  console.log(`[sdk-open-harmony] missing CZOCR data supplied by ${ocrModelFallbackRoot}`);
}

syncSdkOpenWeb();
syncCzcvModels();
syncCzocrModels();
copyTree(ocrFontSourceRoot, ocrFontTargetRoot);

export default {
  system: hapTasks,
  plugins: []
};
