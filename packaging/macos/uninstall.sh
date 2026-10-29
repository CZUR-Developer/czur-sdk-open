#!/bin/bash
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
    echo "Please run with sudo: sudo /Library/CZUR/sdk-open/uninstall.sh" >&2
    exit 1
fi

HOSTS_FILE="/etc/hosts"
MANAGED_HOSTS_LINE="127.0.0.1 sdk-runtime.localhost # CZUR SDK Open managed host"
HOSTS_TEMP=""

cleanup_hosts_temp() {
    if [[ -n "${HOSTS_TEMP}" ]]; then
        rm -f "${HOSTS_TEMP}"
    fi
}

trap cleanup_hosts_temp EXIT

remove_runtime_host_mapping() {
    if ! awk -v managed_line="${MANAGED_HOSTS_LINE}" \
        '$0 == managed_line { found = 1 } END { exit(found ? 0 : 1) }' \
        "${HOSTS_FILE}"; then
        return
    fi

    HOSTS_TEMP="$(mktemp "${HOSTS_FILE}.czur-sdk-open.XXXXXX")"
    awk -v managed_line="${MANAGED_HOSTS_LINE}" '$0 != managed_line' \
        "${HOSTS_FILE}" > "${HOSTS_TEMP}"
    /bin/cp "${HOSTS_TEMP}" "${HOSTS_FILE}"
    rm -f "${HOSTS_TEMP}"
    HOSTS_TEMP=""
}

CONSOLE_USER="$(stat -f '%Su' /dev/console 2>/dev/null || true)"
if [[ -n "${CONSOLE_USER}" && "${CONSOLE_USER}" != "root" && "${CONSOLE_USER}" != "loginwindow" ]]; then
    CONSOLE_UID="$(id -u "${CONSOLE_USER}")"
    launchctl bootout "gui/${CONSOLE_UID}/com.czur.sdk-open" 2>/dev/null || true
fi

remove_runtime_host_mapping
rm -f /Library/LaunchAgents/com.czur.sdk-open.plist
rm -rf /Library/CZUR/sdk-open
pkgutil --forget com.czur.sdk-open.runtime >/dev/null 2>&1 || true

echo "SDK Open runtime removed. User data, the shared root CA, and com.czur.device-helper were preserved."
