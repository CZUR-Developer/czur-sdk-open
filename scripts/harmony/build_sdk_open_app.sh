#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/sdk-paths.sh"
ROOT_DIR="${SDK_OPEN_HOST_SOURCE_ROOT}"
PROJECT_DIR="${SDK_OPEN_SOURCE_ROOT}/platform/harmony"
SDK_HOME="${DEVECO_SDK_HOME:-/Applications/DevEco-Studio.app/Contents/sdk}"
JAVA_HOME="${JAVA_HOME:-/Applications/DevEco-Studio.app/Contents/jbr/Contents/Home}"
NODE_BIN="${NODE_BIN:-/Applications/DevEco-Studio.app/Contents/tools/node/bin}"
HVIGOR="${HVIGOR:-/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw}"
OHPM="${OHPM:-/Applications/DevEco-Studio.app/Contents/tools/ohpm/bin/ohpm}"
BUILD_MODE="${SDK_OPEN_HARMONY_BUILD_MODE:-debug}"
APP_OUTPUT_DIR="${PROJECT_DIR}/build/outputs/default"
HAP_OUTPUT_DIR="${PROJECT_DIR}/sdkopen_tray/build/default/outputs/default"
UNSIGNED_APP="${APP_OUTPUT_DIR}/harmony-default-unsigned.app"
SIGNED_APP="${APP_OUTPUT_DIR}/harmony-default-signed.app"
UNSIGNED_HAP="${HAP_OUTPUT_DIR}/sdkopen_tray-default-unsigned.hap"
SIGNED_HAP="${HAP_OUTPUT_DIR}/sdkopen_tray-default-signed.hap"
APP_HAP_MEMBER="sdkopen_tray-default.hap"
SIGN_TOOL="${HAP_SIGN_TOOL:-${SDK_HOME}/default/openharmony/toolchains/lib/hap-sign-tool.jar}"
REPACK_TOOL="${SDK_OPEN_SOURCE_ROOT}/scripts/harmony/repack_signed_app.py"

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--clean] [--build-web] [--release]

Build the SDK Open HarmonyOS tray application package. The sdkopen_tray entry
HAP owns the native runtime and USB Host lifecycle; no feature HAP or
AppServiceExtensionAbility is required. This project is not listed in the
CapEditor root build-profile.json5 and does not change desktop build.sh or
the main repository desktop SDK Open packaging flow.

Environment:
  DEVECO_SDK_HOME  DevEco SDK root (default: ${SDK_HOME})
  JAVA_HOME        DevEco bundled JDK (default: ${JAVA_HOME})
  NODE_BIN         DevEco bundled Node directory (default: ${NODE_BIN})
  HVIGOR           hvigorw executable (default: ${HVIGOR})
  OHPM             ohpm executable (default: ${OHPM})
  SDK_OPEN_HARMONY_BUILD_MODE  debug or release (default: ${BUILD_MODE})
  HAP_SIGN_TOOL    hap-sign-tool.jar used to verify signed output
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean)
      rm -rf \
        "${PROJECT_DIR}/.hvigor" \
        "${PROJECT_DIR}/sdkopen_tray/.cxx" \
        "${PROJECT_DIR}/sdkopen_tray/build" \
        "${PROJECT_DIR}/sdkopen_tray/src/main/resources/rawfile/sdkopen_web" \
        "${PROJECT_DIR}/sdkopen_tray/src/main/resources/rawfile/sdkopen_czcv_model" \
        "${PROJECT_DIR}/sdkopen_tray/src/main/resources/rawfile/sdkopen_czocr_model" \
        "${PROJECT_DIR}/sdkopen_tray/src/main/resources/rawfile/sdkopen_ocr_fonts" \
        "${PROJECT_DIR}/sdkopen_tray/src/main/resources/rawfile/sdkopen_czocr_model_version.txt" \
        "${PROJECT_DIR}/sdkopen_tray/src/main/resources/rawfile/sdkopen_device_config.conf" \
        "${PROJECT_DIR}/build" \
        "${PROJECT_DIR}/outputs"
      shift
      ;;
    --build-web)
      (
        cd "${SDK_OPEN_SOURCE_ROOT}/frontend/demo-site"
        pnpm install --frozen-lockfile
        pnpm run build
      )
      (
        cd "${SDK_OPEN_SOURCE_ROOT}/frontend/admin-site"
        pnpm install --frozen-lockfile
        pnpm run build
      )
      rm -rf "${SDK_OPEN_SOURCE_ROOT}/web/demo" "${SDK_OPEN_SOURCE_ROOT}/web/admin"
      mkdir -p "${SDK_OPEN_SOURCE_ROOT}/web"
      cp -R "${SDK_OPEN_SOURCE_ROOT}/frontend/demo-site/dist" "${SDK_OPEN_SOURCE_ROOT}/web/demo"
      cp -R "${SDK_OPEN_SOURCE_ROOT}/frontend/admin-site/dist" "${SDK_OPEN_SOURCE_ROOT}/web/admin"
      shift
      ;;
    --release)
      BUILD_MODE=release
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done

[[ -x "${HVIGOR}" ]] || { echo "hvigor not executable: ${HVIGOR}" >&2; exit 1; }
[[ -x "${OHPM}" ]] || { echo "ohpm not executable: ${OHPM}" >&2; exit 1; }
[[ -d "${SDK_HOME}" ]] || { echo "DevEco SDK not found: ${SDK_HOME}" >&2; exit 1; }

# A previous signed HAP must never be mistaken for output from this invocation.
# Hvigor may also create a *-signed.app whose embedded HAP is still unsigned.
python3 "${REPACK_TOOL}" --prepare \
  --unsigned-app "${UNSIGNED_APP}" \
  --signed-app "${SIGNED_APP}" \
  --signed-hap "${SIGNED_HAP}" \
  --member "${APP_HAP_MEMBER}"

cd "${PROJECT_DIR}"
# Resolve local native type packages before ArkTS compilation. Without this,
# hvigor can package a HAP whose native imports fail only at runtime.
PATH="${NODE_BIN}:${PATH}" "${OHPM}" install
PATH="${JAVA_HOME}/bin:${NODE_BIN}:${PATH}" \
DEVECO_SDK_HOME="${SDK_HOME}" \
JAVA_HOME="${JAVA_HOME}" \
"${HVIGOR}" --mode project -p product=default -p "buildMode=${BUILD_MODE}" \
  assembleApp --analyze=normal --parallel --incremental --no-daemon
python3 "${SDK_OPEN_SOURCE_ROOT}/scripts/harmony/verify_sdk_open_native.py" \
  "${UNSIGNED_HAP}" --sdk "${SDK_HOME}"
python3 "${REPACK_TOOL}" \
  --unsigned-app "${UNSIGNED_APP}" \
  --signed-app "${SIGNED_APP}" \
  --signed-hap "${SIGNED_HAP}" \
  --member "${APP_HAP_MEMBER}" \
  --java "${JAVA_HOME}/bin/java" \
  --sign-tool "${SIGN_TOOL}"
