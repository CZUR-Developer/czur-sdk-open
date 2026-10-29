#!/usr/bin/env bash
set -euo pipefail

# Use the smoke-test venv, Node 22 and MacPorts tools in non-interactive Jenkins runs.
export PATH="$HOME/.venvs/sdk-open-smoke/bin:$HOME/.nvm/versions/node/v22.22.0/bin:/opt/local/bin:/opt/local/sbin:$PATH"

if [ "$#" -ne 8 ]; then
    echo "Usage: $0 <arch> <artifact-dir> <work-dir> <report-dir> <sdk-version> <sdk-build> <sdk-stage> <ossutil-config>" >&2
    exit 2
fi

arch="$1"
artifact_dir="$2"
work_dir="$3"
report_dir="$4"
sdk_version="$5"
sdk_build="$6"
sdk_stage="$7"
ossutil_config="$8"
automation_dir="${work_dir}/SmartEye/src/workspace/api-output/tests/czur-sdk-open-api-automation"
automation_env="${work_dir}/smarteye.env"
smoke_log="${report_dir}/smoke-test.log"
installer_timeout_seconds="${SDK_OPEN_MACOS_INSTALL_TIMEOUT_SECONDS:-180}"
system_keychain="/Library/Keychains/System.keychain"
started_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

cleanup_secrets() {
    rm -f -- "${automation_env}" "${automation_dir}/.env"
}
trap cleanup_secrets EXIT

case "${installer_timeout_seconds}" in
    ''|*[!0-9]*)
        echo "SDK_OPEN_MACOS_INSTALL_TIMEOUT_SECONDS must be a positive integer" >&2
        exit 2
        ;;
esac
if [[ "${installer_timeout_seconds}" -le 0 ]]; then
    echo "SDK_OPEN_MACOS_INSTALL_TIMEOUT_SECONDS must be a positive integer" >&2
    exit 2
fi

root_ca_sha256() {
    /usr/bin/openssl x509 -in "$1" -outform der 2>/dev/null |
        /usr/bin/shasum -a 256 |
        /usr/bin/awk '{ print toupper($1) }'
}

require_pretrusted_root_ca() {
    local root_ca="$1"
    local expected_sha256
    expected_sha256="$(root_ca_sha256 "${root_ca}")"
    if [[ -z "${expected_sha256}" ]] ||
       ! /usr/bin/security find-certificate -a -Z -c "Local Runtime Root CA" \
            "${system_keychain}" 2>/dev/null |
            /usr/bin/awk '/SHA-256 hash:/ { print toupper($3) }' |
            /usr/bin/grep -Fxq "${expected_sha256}"; then
        echo "SDK Open smoke requires Local Runtime Root CA (${expected_sha256:-unknown})" >&2
        echo "to be preinstalled and trusted in ${system_keychain}." >&2
        echo "Provision it through Keychain Access, MDM, or a configuration profile before retrying." >&2
        return 1
    fi
}

run_with_timeout() {
    local timeout_seconds="$1"
    shift
    python3 - "${timeout_seconds}" "$@" <<'PY'
import os
import signal
import subprocess
import sys

timeout_seconds = int(sys.argv[1])
command = sys.argv[2:]
process = subprocess.Popen(command, start_new_session=True)
try:
    raise SystemExit(process.wait(timeout=timeout_seconds))
except subprocess.TimeoutExpired:
    print(
        f"Command timed out after {timeout_seconds}s: {' '.join(command)}",
        file=sys.stderr,
    )
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
    raise SystemExit(124)
PY
}

if [ -z "${SDK_OPEN_SMOKE_API_KEY:-}" ]; then
    echo "SDK_OPEN_SMOKE_API_KEY credential must contain exactly one non-empty line" >&2
    exit 1
fi
case "${SDK_OPEN_SMOKE_API_KEY}" in
    *$'\n'*|*$'\r'*)
        echo "SDK_OPEN_SMOKE_API_KEY credential must contain exactly one non-empty line" >&2
        exit 1
        ;;
esac

case "${arch}" in arm64|x86_64) ;; *) echo "Unsupported macOS smoke architecture: ${arch}" >&2; exit 2 ;; esac

package_count="$(find "${artifact_dir}" -maxdepth 1 -type f -name "sdk-open_macos_${arch}_*.pkg" -print | wc -l | tr -d '[:space:]')"
if [ "${package_count}" -ne 1 ]; then
    echo "Expected exactly one SDK Open macOS package for ${arch}, found ${package_count}" >&2
    exit 1
fi
package_path="$(find "${artifact_dir}" -maxdepth 1 -type f -name "sdk-open_macos_${arch}_*.pkg" -print)"
package_root_ca="${work_dir}/installed-local-runtime-root-ca.crt"

rm -rf -- "${work_dir}" "${report_dir}"
mkdir -p "${work_dir}" "${report_dir}"
touch "${smoke_log}"

git clone --depth 1 --branch 3.2AI --single-branch \
    git@gitee.com:czur_dl/SmartEye.git "${work_dir}/SmartEye"
smarteye_commit="$(git -C "${work_dir}/SmartEye" rev-parse HEAD)"
test -f "${automation_dir}/package-lock.json"
test -f "${automation_dir}/.env"
umask 077
awk '
    /^[[:space:]]*(export[[:space:]]+)?SDK_API_KEY[[:space:]]*=/ { next }
    { print }
' "${automation_dir}/.env" >"${automation_env}"
printf '\nSDK_API_KEY=%s\n' "${SDK_OPEN_SMOKE_API_KEY}" >>"${automation_env}"
install -m 0600 "${automation_env}" "${automation_dir}/.env"

run_status=0
set +e
(
    set -euo pipefail
    export SDK_EXPECTED_VERSION="${sdk_version}"
    export SDK_EXPECTED_BUILD="${sdk_build}"
    echo "Installing SDK Open package with a ${installer_timeout_seconds}s timeout."
    set +e
    run_with_timeout "${installer_timeout_seconds}" \
        sudo -n /usr/sbin/installer -pkg "${package_path}" -target /
    install_status=$?
    set -e
    if [[ "${install_status}" -ne 0 ]]; then
        echo "SDK Open package installation failed with exit code ${install_status}." >&2
        echo "Relevant macOS installer processes:" >&2
        ps -axo pid,ppid,user,etime,state,command |
            /usr/bin/grep -E '[i]nstaller|[i]nstalld|[p]ackage_script_service|[s]ecurity add-trusted-cert' || true
        echo "Recent /var/log/install.log entries:" >&2
        tail -n 200 /var/log/install.log 2>&1 || true
        exit "${install_status}"
    fi
    cp /Library/CZUR/sdk-open/tls/local-runtime-root-ca.crt "${package_root_ca}"
    require_pretrusted_root_ca "${package_root_ca}"
    export NODE_EXTRA_CA_CERTS="${package_root_ca}"
    for attempt in $(seq 1 60); do
        if env -u CURL_CA_BUNDLE -u SSL_CERT_FILE -u SSL_CERT_DIR \
            /usr/bin/curl --disable --fail --silent --show-error --max-time 3 \
            --cacert "${package_root_ca}" \
            https://sdk-runtime.localhost:18082/healthz >/dev/null; then
            break
        fi
        if [ "${attempt}" -eq 60 ]; then
            echo "SDK Open macOS runtime did not become healthy within 60 seconds" >&2
            exit 1
        fi
        sleep 1
    done

    # curl/Node with an explicit CA can pass while Safari rejects certificate
    # policy violations. Verify the certificate served by the live endpoint
    # using the native keychain policy too.
    /usr/bin/security verify-cert https://sdk-runtime.localhost:18082/healthz

    cd "${automation_dir}"
    npm ci
    npm run test:full:no-device
    test -s artifacts/results.json
    node -e '
const report = require(process.argv[1]);
const failed = Number(report.stats?.unexpected ?? 0);
if (failed > 0) {
    console.error(`Smoke report contains ${failed} unexpected test failure(s)`);
    process.exit(1);
}
' "${automation_dir}/artifacts/results.json"
) 2>&1 | tee -a "${smoke_log}"
run_status="${PIPESTATUS[0]}"
set -e

if [ -d "${automation_dir}/artifacts" ]; then
    find "${automation_dir}/artifacts" -type f -name 'trace.zip' -delete
    rm -rf "${automation_dir}/artifacts/blob-report"
    cp -a "${automation_dir}/artifacts" "${report_dir}/artifacts"
fi

finished_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
python3 - "${report_dir}/metadata.json" "${arch}" "${sdk_version}" "${sdk_build}" \
    "${sdk_stage}" "${smarteye_commit}" "${started_at}" "${finished_at}" "${run_status}" <<'PY'
import json
import sys

path, arch, version, build, stage, commit, started, finished, status = sys.argv[1:]
payload = {
    "target": f"macos-{arch}",
    "sdk_version": version,
    "sdk_build": build,
    "sdk_stage": stage,
    "smarteye_branch": "3.2AI",
    "smarteye_commit": commit,
    "started_at": started,
    "finished_at": finished,
    "exit_code": int(status),
    "status": "passed" if status == "0" else "failed",
}
with open(path, "w", encoding="utf-8") as output:
    json.dump(payload, output, ensure_ascii=False, indent=2)
    output.write("\n")
PY

test -r "${ossutil_config}"
if [ "${run_status}" -ne 0 ]; then
    echo "SDK Open macOS ${arch} smoke failed with exit code ${run_status}" >&2
fi
exit "${run_status}"
