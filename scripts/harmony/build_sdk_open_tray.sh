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

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--clean] [--release]

Build the SDK Open tray HAP. The hidden tray UIAbility owns the native runtime
and USB Host lifecycle; the package does not require an AppService feature HAP.

Environment:
  DEVECO_SDK_HOME  DevEco SDK root (default: ${SDK_HOME})
  JAVA_HOME        DevEco bundled JDK (default: ${JAVA_HOME})
  NODE_BIN         DevEco bundled Node directory (default: ${NODE_BIN})
  HVIGOR           hvigorw executable (default: ${HVIGOR})
  OHPM             ohpm executable (default: ${OHPM})
  SDK_OPEN_HARMONY_BUILD_MODE  debug or release (default: ${BUILD_MODE})
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
        "${PROJECT_DIR}/sdkopen_tray/src/main/resources/rawfile/sdkopen_device_config.conf"
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

cd "${PROJECT_DIR}"
PATH="${NODE_BIN}:${PATH}" "${OHPM}" install
PATH="${JAVA_HOME}/bin:${NODE_BIN}:${PATH}" \
DEVECO_SDK_HOME="${SDK_HOME}" \
JAVA_HOME="${JAVA_HOME}" \
"${HVIGOR}" --mode module -p module=sdkopen_tray@default -p product=default \
  -p "buildMode=${BUILD_MODE}" assembleHap --analyze=normal --parallel --incremental --no-daemon
python3 "${SDK_OPEN_SOURCE_ROOT}/scripts/harmony/verify_sdk_open_native.py" \
  "${PROJECT_DIR}/sdkopen_tray/build/default/outputs/default/sdkopen_tray-default-unsigned.hap" --sdk "${SDK_HOME}"
