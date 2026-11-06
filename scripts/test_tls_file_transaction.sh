#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_OPEN_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/czur-tls-transaction-test.XXXXXX")"
trap 'rm -rf "${BUILD_DIR}"' EXIT

"${CXX:-clang++}" \
    -std=c++11 \
    -Wall \
    -Wextra \
    -Werror \
    -I"${SDK_OPEN_DIR}" \
    "${SDK_OPEN_DIR}/test/tls_file_transaction_test.cpp" \
    -o "${BUILD_DIR}/tls_file_transaction_test"

"${BUILD_DIR}/tls_file_transaction_test"
