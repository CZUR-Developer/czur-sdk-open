#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 <target> <work-dir> <sdk-deb> <automation-archive> <automation-env> <archive-sha256> <sdk-version> <sdk-build> <smarteye-commit>" >&2
    exit 2
}

if [ "$#" -ne 9 ]; then
    usage
fi

target="$1"
work_dir="$2"
sdk_deb="$3"
automation_archive="$4"
automation_env="$5"
expected_archive_sha256="$6"
sdk_version="$7"
sdk_build="$8"
smarteye_commit="$9"

report_dir="${work_dir}/report"
automation_dir="${work_dir}/automation"
smoke_log="${report_dir}/smoke-test.log"
report_archive="${work_dir}/smoke-report-${target}.tar.gz"
started_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

cleanup_secrets() {
    rm -f -- "${automation_env}" "${automation_dir}/.env"
}
trap cleanup_secrets EXIT

rm -rf "${report_dir}" "${automation_dir}" "${report_archive}"
mkdir -p "${report_dir}" "${automation_dir}"
touch "${smoke_log}"

run_status=0
set +e
(
    set -euo pipefail

    for required_file in "${sdk_deb}" "${automation_archive}" "${automation_env}"; do
        if [ ! -f "${required_file}" ]; then
            echo "Missing SDK Open smoke input: ${required_file}" >&2
            exit 1
        fi
    done

    actual_archive_sha256="$(sha256sum "${automation_archive}" | awk '{print $1}')"
    if [ "${actual_archive_sha256}" != "${expected_archive_sha256}" ]; then
        echo "SmartEye automation archive SHA-256 mismatch: expected=${expected_archive_sha256}, actual=${actual_archive_sha256}" >&2
        exit 1
    fi

    tar -xzf "${automation_archive}" -C "${automation_dir}"
    install -m 0600 "${automation_env}" "${automation_dir}/.env"

    # The Linux build ECS runs as root. Some minimal build images do not ship
    # sudo, while SmartEye's portable bootstrap intentionally uses it.
    if [ "$(id -u)" -eq 0 ] && ! command -v sudo >/dev/null 2>&1; then
        mkdir -p "${work_dir}/bin"
        printf '#!/usr/bin/env bash\nexec "$@"\n' >"${work_dir}/bin/sudo"
        chmod 0755 "${work_dir}/bin/sudo"
        export PATH="${work_dir}/bin:${PATH}"
    fi

    export SDK_EXPECTED_VERSION="${sdk_version}"
    export SDK_EXPECTED_BUILD="${sdk_build}"

    cd "${automation_dir}"
    bash scripts/bootstrap-linux.sh --sdk-package "${sdk_deb}"
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

{
    echo
    echo "===== sdk-open service status ====="
    systemctl status sdk-open.service --no-pager || true
    echo
    echo "===== sdk-open recent journal ====="
    journalctl -u sdk-open.service --no-pager -n 300 || true
} >>"${smoke_log}" 2>&1

if [ -d "${automation_dir}/artifacts" ]; then
    # Keep the merged HTML/JSON/JUnit reports and image attachments, but never
    # publish raw Playwright traces/blob reports because they can embed request
    # headers or other authentication material.
    find "${automation_dir}/artifacts" -type f -name 'trace.zip' -delete
    rm -rf "${automation_dir}/artifacts/blob-report"
    cp -a "${automation_dir}/artifacts" "${report_dir}/artifacts"
fi

finished_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
status_text='passed'
if [ "${run_status}" -ne 0 ]; then
    status_text='failed'
fi
sdk_package_name="$(basename "${sdk_deb}")"
sdk_package_sha256="$(sha256sum "${sdk_deb}" | awk '{print $1}')"

cat >"${report_dir}/metadata.json" <<EOF
{
  "target": "${target}",
  "sdk_version": "${sdk_version}",
  "sdk_build": "${sdk_build}",
  "sdk_package": "${sdk_package_name}",
  "sdk_package_sha256": "${sdk_package_sha256}",
  "smarteye_branch": "3.2AI",
  "smarteye_commit": "${smarteye_commit}",
  "started_at": "${started_at}",
  "finished_at": "${finished_at}",
  "exit_code": ${run_status},
  "status": "${status_text}"
}
EOF

tar -czf "${report_archive}" -C "${report_dir}" .
echo "SDK Open ${target} smoke report archive: ${report_archive}"
exit "${run_status}"
