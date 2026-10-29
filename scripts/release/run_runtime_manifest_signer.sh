#!/usr/bin/env bash

set -euo pipefail
set +x

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/sdk-paths.sh"
ROOT_DIR="${SDK_OPEN_HOST_SOURCE_ROOT}"
RUNTIME_MANIFEST_IMAGE="${CZUR_WEB_BUILD_IMAGE:?Jenkins must configure CZUR_WEB_BUILD_IMAGE for Runtime Manifest signing}"
PRIVATE_KEY_FILE="${RUNTIME_MANIFEST_PRIVATE_KEY_FILE:?RUNTIME_MANIFEST_PRIVATE_KEY_FILE is required}"
PUBLIC_KEY_FILE="${RUNTIME_MANIFEST_PUBLIC_KEY_FILE:?RUNTIME_MANIFEST_PUBLIC_KEY_FILE is required}"

if ! command -v docker >/dev/null 2>&1; then
    echo 'Docker is required to validate and sign the Runtime Manifest.' >&2
    exit 1
fi

for key_file in "${PRIVATE_KEY_FILE}" "${PUBLIC_KEY_FILE}"; do
    if [ ! -r "${key_file}" ]; then
        echo "Runtime Manifest signing key file is not readable: ${key_file}" >&2
        exit 1
    fi
done

echo "Running Runtime Manifest signer with image: ${RUNTIME_MANIFEST_IMAGE}"
docker run --rm \
    --network none \
    --user "$(id -u):$(id -g)" \
    --volume "${ROOT_DIR}:/workspace" \
    --volume "${PRIVATE_KEY_FILE}:/run/secrets/runtime-manifest-private-key.pem:ro" \
    --volume "${PUBLIC_KEY_FILE}:/run/secrets/runtime-manifest-public-key.pem:ro" \
    --workdir /workspace \
    "${RUNTIME_MANIFEST_IMAGE}" \
    node src/sdk_open/scripts/release/sign_runtime_manifest.mjs \
    "$@" \
    --private-key-file /run/secrets/runtime-manifest-private-key.pem \
    --public-key-file /run/secrets/runtime-manifest-public-key.pem
