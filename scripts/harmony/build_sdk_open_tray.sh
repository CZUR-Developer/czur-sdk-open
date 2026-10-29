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

Build the SDK Open tray controller HAP. The tray controller starts and stops
the sdkopen AppServiceExtensionAbility; it does not contain sdk_open runtime.

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
      rm -rf "${PROJECT_DIR}/.hvigor" "${PROJECT_DIR}/sdkopen_tray/build"
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
