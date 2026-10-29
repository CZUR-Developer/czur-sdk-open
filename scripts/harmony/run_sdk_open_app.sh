#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/sdk-paths.sh"
ROOT_DIR="${SDK_OPEN_HOST_SOURCE_ROOT}"
SDK_HOME="${DEVECO_SDK_HOME:-/Applications/DevEco-Studio.app/Contents/sdk}"
HDC="${HDC:-${SDK_HOME}/default/openharmony/toolchains/hdc}"
APP_PATH="${SDK_OPEN_HARMONY_APP:-${SDK_OPEN_SOURCE_ROOT}/platform/harmony/build/outputs/default/harmony-default-unsigned.app}"
TARGET="${SDK_OPEN_HARMONY_TARGET:-}"
BUNDLE="${SDK_OPEN_HARMONY_BUNDLE:-com.czur.sdkopen}"
MODULE="${SDK_OPEN_HARMONY_MODULE:-sdkopen_tray}"
ABILITY="${SDK_OPEN_HARMONY_ABILITY:-SdkOpenLauncherAbility}"
SKIP_INSTALL=0
RUN_SMOKE=0
LAUNCH_TRAY=1

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--app <signed.app>] [--target <device>] [--skip-install] [--no-launch] [--smoke]

Install the SDK Open HarmonyOS application containing the tray entry HAP and
the sdkopen service feature HAP. By default the script opens the tray controller;
the user starts/stops the service from that controller. With --smoke, it verifies
a service that has already been started from the tray.
The default build output is unsigned; pass a signed .app for normal devices.

Environment:
  DEVECO_SDK_HOME       DevEco SDK root (default: ${SDK_HOME})
  HDC                   hdc executable (default: ${HDC})
  SDK_OPEN_HARMONY_APP  application package path
  SDK_OPEN_HARMONY_TARGET hdc target serial
  SDK_OPEN_HARMONY_HDC_TIMEOUT hdc timeout in seconds (default: 15)
  SDK_OPEN_HARMONY_ADMIN_FORWARD local admin port (default: 27080)
  SDK_OPEN_HARMONY_DEMO_FORWARD local demo port (default: 27081)
  SDK_OPEN_HARMONY_ASSET_FORWARD local asset port (default: 27082)
USAGE
}

fail() {
  printf '[sdk-open-harmony] FAIL: %s\n' "$*" >&2
  exit 1
}

hdc_cmd() {
  local timeout_seconds="${SDK_OPEN_HARMONY_HDC_TIMEOUT:-15}"
  local pid
  local elapsed=0
  "${HDC}" "$@" &
  pid=$!
  while kill -0 "${pid}" 2>/dev/null; do
    if (( elapsed >= timeout_seconds )); then
      kill "${pid}" 2>/dev/null || true
      sleep 1
      kill -9 "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
      printf '[sdk-open-harmony] hdc timed out after %ss: %s\n' "${timeout_seconds}" "$*" >&2
      return 124
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  wait "${pid}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --app)
      [[ $# -ge 2 ]] || fail "$1 requires a path"
      APP_PATH="$2"
      shift 2
      ;;
    --target)
      [[ $# -ge 2 ]] || fail "--target requires a device serial"
      TARGET="$2"
      shift 2
      ;;
    --skip-install)
      SKIP_INSTALL=1
      shift
      ;;
    --no-launch)
      LAUNCH_TRAY=0
      shift
      ;;
    --smoke)
      RUN_SMOKE=1
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

[[ -x "${HDC}" ]] || fail "hdc not executable: ${HDC}"

if [[ -z "${TARGET}" ]]; then
  TARGETS="$(hdc_cmd list targets -v | tr -d '\r' | sed '/^$/d; /^\[Empty\]$/d')"
  TARGET_COUNT="$(printf '%s\n' "${TARGETS}" | sed '/^$/d' | wc -l | tr -d ' ')"
  [[ "${TARGET_COUNT}" != "0" ]] || fail "no HarmonyOS target connected"
  [[ "${TARGET_COUNT}" == "1" ]] || fail "multiple targets connected; pass --target <device>"
  TARGET="$(printf '%s\n' "${TARGETS}" | awk '{print $1}')"
fi

HDC_ARGS=(-t "${TARGET}")
if [[ "${SKIP_INSTALL}" == "0" ]]; then
  [[ -f "${APP_PATH}" ]] || fail "application package not found: ${APP_PATH}"
  command -v unzip >/dev/null 2>&1 || fail "unzip is required to unpack the .app"
  PACKAGE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/sdkopen-install.XXXXXXXX")"
  DEVICE_PACKAGE_DIR="/data/local/tmp/$(basename "${PACKAGE_DIR}")"
  cleanup_install() {
    rm -rf "${PACKAGE_DIR}"
    hdc_cmd "${HDC_ARGS[@]}" shell "rm -rf ${DEVICE_PACKAGE_DIR}" >/dev/null 2>&1 || true
  }
  trap cleanup_install EXIT
  # bm accepts a directory of HAPs, not the outer .app archive. Install the
  # entry and feature together; separate hdc installs cannot migrate modules.
  unzip -q -j "${APP_PATH}" '*.hap' -d "${PACKAGE_DIR}" || fail "cannot extract application HAPs"
  HAP_FILES=("${PACKAGE_DIR}"/*.hap)
  [[ -f "${HAP_FILES[0]}" ]] || fail "no HAP found in ${APP_PATH}"
  hdc_cmd "${HDC_ARGS[@]}" shell "mkdir -p ${DEVICE_PACKAGE_DIR}" >/dev/null
  for HAP_FILE in "${HAP_FILES[@]}"; do
    TRANSFER_OUTPUT="$(hdc_cmd "${HDC_ARGS[@]}" file send "${HAP_FILE}" \
      "${DEVICE_PACKAGE_DIR}/$(basename "${HAP_FILE}")" 2>&1)" || fail "HAP transfer failed: ${TRANSFER_OUTPUT}"
    [[ "${TRANSFER_OUTPUT}" == *'FileTransfer finish'* ]] || fail "HAP transfer failed: ${TRANSFER_OUTPUT}"
  done
  printf '[sdk-open-harmony] install %s on %s\n' "${APP_PATH}" "${TARGET}"
  INSTALL_OUTPUT="$(SDK_OPEN_HARMONY_HDC_TIMEOUT=120 hdc_cmd "${HDC_ARGS[@]}" shell \
    "bm install -r -p ${DEVICE_PACKAGE_DIR}" 2>&1)" || \
    fail "hdc install command failed: ${INSTALL_OUTPUT}"
  printf '%s\n' "${INSTALL_OUTPUT}"
  # hdc may return exit code 0 even when the daemon reports an installation
  # error (for example, insufficient space on the device). Do not report a
  # package as installed unless the daemon response is successful.
  if [[ "${INSTALL_OUTPUT}" != *'install bundle successfully.'* ]]; then
    fail "HAP installation was rejected: ${INSTALL_OUTPUT}. For legacy entry-to-feature migration, see src/sdk_open/platform/harmony/README.md; this script does not uninstall application data."
  fi
  PACKAGE_INFO="$(hdc_cmd "${HDC_ARGS[@]}" shell "bm dump -n ${BUNDLE}" 2>&1 || true)"
  if ! printf '%s\n' "${PACKAGE_INFO}" | grep -q "\"bundleName\": \"${BUNDLE}\""; then
    fail "HAP installation could not be verified for ${BUNDLE}: ${PACKAGE_INFO}"
  fi
  cleanup_install
  trap - EXIT
fi

if [[ "${LAUNCH_TRAY}" == "1" ]]; then
  START_OUTPUT="$(hdc_cmd "${HDC_ARGS[@]}" shell \
    "aa start -b ${BUNDLE} -m ${MODULE} -a ${ABILITY}" 2>&1)" || \
    fail "tray controller start failed: ${START_OUTPUT}"
  printf '%s\n' "${START_OUTPUT}"
  [[ "${START_OUTPUT}" == *'start ability successfully.'* ]] || fail "controller start rejected: ${START_OUTPUT}"
fi

if [[ "${RUN_SMOKE}" == "0" ]]; then
  printf '[sdk-open-harmony] tray controller ready: %s/%s/%s\n' "${BUNDLE}" "${MODULE}" "${ABILITY}"
  printf '[sdk-open-harmony] use the tray controller to start or stop the sdkopen service\n'
  exit 0
fi

PID="$(hdc_cmd "${HDC_ARGS[@]}" shell "pidof ${BUNDLE}:appService" 2>/dev/null | tr -d '\r' || true)"
if [[ -z "${PID}" ]]; then
  fail "service process is not running; start the service from the controller before --smoke"
fi

printf '[sdk-open-harmony] service running pid=%s admin=http://127.0.0.1:17080/\n' "${PID}"
printf '[sdk-open-harmony] logs: %s -t %s shell hilog -x | rg -i '\''SdkOpenService|sdk_open'\''\n' "${HDC}" "${TARGET}"

if [[ "${RUN_SMOKE}" == "1" ]]; then
  ADMIN_FORWARD="${SDK_OPEN_HARMONY_ADMIN_FORWARD:-27080}"
  DEMO_FORWARD="${SDK_OPEN_HARMONY_DEMO_FORWARD:-27081}"
  ASSET_FORWARD="${SDK_OPEN_HARMONY_ASSET_FORWARD:-27082}"

  command -v curl >/dev/null 2>&1 || fail "curl is required for --smoke"
  hdc_cmd "${HDC_ARGS[@]}" fport "tcp:${ADMIN_FORWARD}" tcp:17080 >/dev/null
  hdc_cmd "${HDC_ARGS[@]}" fport "tcp:${DEMO_FORWARD}" tcp:17081 >/dev/null
  hdc_cmd "${HDC_ARGS[@]}" fport "tcp:${ASSET_FORWARD}" tcp:17082 >/dev/null
  FORWARD_LIST="$(hdc_cmd "${HDC_ARGS[@]}" fport ls)"
  [[ "${FORWARD_LIST}" == *"tcp:${ADMIN_FORWARD} tcp:17080"* ]] || fail "admin port forward missing"
  [[ "${FORWARD_LIST}" == *"tcp:${DEMO_FORWARD} tcp:17081"* ]] || fail "demo port forward missing"
  [[ "${FORWARD_LIST}" == *"tcp:${ASSET_FORWARD} tcp:17082"* ]] || fail "asset port forward missing"

  HEALTH_JSON=""
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    HEALTH_JSON="$(curl --fail --silent --show-error --max-time 3 \
      "http://127.0.0.1:${ADMIN_FORWARD}/healthz" 2>/dev/null || true)"
    if [[ "${HEALTH_JSON}" == *'"ok":true'* ]]; then
      break
    fi
    sleep 1
  done
  [[ "${HEALTH_JSON}" == *'"ok":true'* ]] || fail "admin healthz check failed"

  STATUS_JSON=""
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    STATUS_JSON="$(curl --fail --silent --show-error --max-time 3 \
      "http://127.0.0.1:${ADMIN_FORWARD}/api/status" 2>/dev/null || true)"
    if [[ "${STATUS_JSON}" == *'"running":true'* ]]; then
      break
    fi
    sleep 1
  done
  [[ "${STATUS_JSON}" == *'"running":true'* ]] || fail "runtime status is not running"

  DEMO_HTML="$(curl --fail --silent --show-error --max-time 5 \
    "http://127.0.0.1:${DEMO_FORWARD}/")"
  [[ "${DEMO_HTML}" == *'<title>SDK Demo Site</title>'* ]] || fail "demo HTML check failed"

  ASSET_HEALTH="$(curl --fail --silent --show-error --max-time 5 \
    "http://127.0.0.1:${ASSET_FORWARD}/healthz")"
  [[ "${ASSET_HEALTH}" == *'"ok":true'* ]] || fail "asset healthz check failed"

  SERVICE_LOGS=""
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    SERVICE_LOGS="$(hdc_cmd "${HDC_ARGS[@]}" shell \
      "hilog -x | grep -E 'SdkOpenService.*service runtime started'" 2>/dev/null || true)"
    if [[ "${SERVICE_LOGS}" == *'SDK Open service runtime started'* ]]; then
      break
    fi
    sleep 1
  done
  [[ "${SERVICE_LOGS}" == *'SDK Open service runtime started'* ]] || fail "service-started log not found"

  printf '[sdk-open-harmony] PASS smoke service/health/status/demo/asset\n'
fi
