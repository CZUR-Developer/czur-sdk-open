#!/usr/bin/env bash

set -euo pipefail

manifest_path="${1:-manifest.json}"
signature_path="${2:-manifest.json.sig}"
oss_config="${OSSUTIL_CONFIG_FILE:-${HOME}/oss/.ossutilnewconfig}"
fixed_manifest='oss://czur-capeditor-build/sdk-open/Package/Release/manifest.json'
fixed_signature='oss://czur-capeditor-build/sdk-open/Package/Release/manifest.json.sig'

if [[ ! -f "${manifest_path}" ]]; then
    echo "Manifest file not found: ${manifest_path}" >&2
    exit 1
fi
if [[ ! -f "${signature_path}" ]]; then
    echo "Manifest signature file not found: ${signature_path}" >&2
    exit 1
fi

# Keep the signature-first order so readers never observe a new manifest with
# the previous signature.
retry_command() {
    local max_attempts="$1"
    local attempt=1
    shift
    until "$@"; do
        if [[ "${attempt}" -ge "${max_attempts}" ]]; then
            echo "Command failed after ${max_attempts} attempts: $1" >&2
            return 1
        fi
        attempt=$((attempt + 1))
        sleep 2
    done
}

retry_command 3 ossutil cp -f "${signature_path}" "${fixed_signature}" --config-file "${oss_config}"
retry_command 3 ossutil cp -f "${manifest_path}" "${fixed_manifest}" --config-file "${oss_config}"
echo "Uploaded ${signature_path} and ${manifest_path} to ${fixed_signature} and ${fixed_manifest}"
