#!/usr/bin/env bash
# Source this file from SDK build/package scripts.
SDK_OPEN_SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_OPEN_HOST_SOURCE_ROOT="${SDK_OPEN_HOST_SOURCE_ROOT:-$(cd "${SDK_OPEN_SOURCE_ROOT}/../.." && pwd)}"
SDK_OPEN_THIRD_PARTY_ROOT="${SDK_OPEN_THIRD_PARTY_ROOT:-${SDK_OPEN_HOST_SOURCE_ROOT}/third_party_repo}"
export SDK_OPEN_SOURCE_ROOT SDK_OPEN_HOST_SOURCE_ROOT SDK_OPEN_THIRD_PARTY_ROOT
