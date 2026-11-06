#!/bin/bash
# Copyright (c) 2026 CZUR Tech. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
    echo "This test requires macOS." >&2
    exit 1
fi
SDK_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_ROOT="$(mktemp -d /tmp/sdk-open-mac-config.XXXXXX)"
trap 'rm -rf "$TEST_ROOT"' EXIT

INCLUDES=()
for directory in runtime transport application facade interfaces providers/mock \
    third_party third_party/cpp-httplib third_party/websocketpp \
    third_party/asio/asio/include third_party/CGraph/src; do
    INCLUDES+=("-I${SDK_ROOT}/${directory}")
done
TLS_ROOT="${SDK_ROOT}/../../third_party_repo/openssl3"
TEST_ARCH="${SDK_OPEN_TEST_ARCH:-$(uname -m)}"
TLS_ARCH="mac-${TEST_ARCH}"
[[ "$TLS_ARCH" != mac-x86_64 ]] || TLS_ARCH=mac-x86-64
INCLUDES+=("-I${TLS_ROOT}/include/mac")
CXX_FLAGS=(-arch "$TEST_ARCH" -std=c++11 -O1 -DASIO_STANDALONE -D_WEBSOCKETPP_CPP11_STL_ -DSDK_OPEN_ENABLE_TLS=1 -DCPPHTTPLIB_OPENSSL_SUPPORT -DSDK_OPEN_TLS_TRANSACTION_TESTING)
"${CXX:-clang++}" "${CXX_FLAGS[@]}" "${INCLUDES[@]}" -fsyntax-only \
    "$SDK_ROOT/runtime/sdk_open_main.cpp"
"${CXX:-clang++}" "${CXX_FLAGS[@]}" "${INCLUDES[@]}" -Wl,-dead_strip \
    "$SDK_ROOT/test/mac_runtime_config_test.cpp" \
    "$SDK_ROOT/runtime/sdk_config.cpp" "$SDK_ROOT/runtime/sdk_runtime_paths.cpp" \
    "$SDK_ROOT/runtime/sdk_tls_certificate_manager.cpp" \
    "${TLS_ROOT}/lib/${TLS_ARCH}/libssl.a" "${TLS_ROOT}/lib/${TLS_ARCH}/libcrypto.a" \
    -framework CoreFoundation -framework Security -o "$TEST_ROOT/mac_runtime_config_test"

cp "$SDK_ROOT/../../packaging/sdk_open/macos/tls/legacy-sdk-runtime.localhost.pem" "$TEST_ROOT/legacy-old.pem"
cp "$SDK_ROOT/../../packaging/sdk_open/tls/runtime/sdk-runtime.localhost.fullchain.pem" "$TEST_ROOT/legacy-new.pem"
cp "$SDK_ROOT/../../packaging/sdk_open/tls/runtime/sdk-runtime.localhost.key.pem" "$TEST_ROOT/legacy-key.pem"
# Do not inherit SDK overrides from the developer's running environment.
while IFS= read -r variable; do
    case "$variable" in SDK_*|CZUR_SDK_RUNTIME_DIR) unset "$variable" ;; esac
done < <(compgen -e)
CZUR_SDK_RUNTIME_DIR="$TEST_ROOT/state" SDK_OPEN_LOG_DIR="$TEST_ROOT/logs" \
    "$TEST_ROOT/mac_runtime_config_test" "$TEST_ROOT/config.env"
