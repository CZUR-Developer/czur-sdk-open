#!/usr/bin/env bash
# Build only the missing SDK Open OpenSSL dependency from the repository archive.
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/sdk-paths.sh"
ROOT_DIR="${SDK_OPEN_HOST_SOURCE_ROOT}"
SDK_HOME="${DEVECO_SDK_HOME:-/Applications/DevEco-Studio.app/Contents/sdk}"
NATIVE="${SDK_HOME}/default/openharmony/native"
ARCHIVE="${SDK_OPEN_THIRD_PARTY_ROOT}/src/openssl-OpenSSL_3_0_16.zip"
WORK_DIR="${ROOT_DIR}/build/sdk-open-openssl"
LIB_DIR="${SDK_OPEN_THIRD_PARTY_ROOT}/openssl3/lib/ohos"
INCLUDE_DIR="${SDK_OPEN_THIRD_PARTY_ROOT}/openssl3/include/ohos/openssl"
READELF="${NATIVE}/llvm/bin/llvm-readelf"

verify_library() {
  [[ -f "$1" ]] || return 1
  local info
  info="$("${READELF}" -h -d "$1")"
  [[ "${info}" == *AArch64* && "${info}" != *libc.so.6* && "${info}" != *ld-linux* ]]
}

if [[ "${1:-}" == "--help" ]]; then
  printf 'Usage: %s [--force]\nReuse valid lib/ohos outputs, or build OpenSSL 3.0.16 from the local LFS archive.\n' "$0"
  exit 0
fi
[[ $# == 0 || ( $# == 1 && "$1" == "--force" ) ]] || { echo 'Expected --force or no arguments' >&2; exit 2; }
[[ -x "${READELF}" ]] || { echo "SDK llvm-readelf missing: ${READELF}" >&2; exit 1; }
if [[ "${1:-}" != "--force" ]] && verify_library "${LIB_DIR}/libcrypto.so.3" && \
    verify_library "${LIB_DIR}/libssl.so.3" && [[ -f "${INCLUDE_DIR}/configuration.h" ]]; then
  echo '[sdk-open-harmony] reuse existing OpenSSL 3 lib/ohos outputs'
  exit 0
fi
[[ -f "${ARCHIVE}" ]] || { echo "Source archive missing: ${ARCHIVE}" >&2; exit 1; }
if head -c 150 "${ARCHIVE}" | grep -q 'git-lfs.github.com/spec'; then
  echo 'Restore third_party_repo/src/openssl-OpenSSL_3_0_16.zip with git lfs pull before building.' >&2
  exit 1
fi
# The repository archive expands to openssl-openssl-3.0.16; its public headers
# are templates until Configure/make generate the platform-specific headers.
mkdir -p "${WORK_DIR}/source" "${WORK_DIR}/ohos" "${LIB_DIR}" "${INCLUDE_DIR}"
unzip -q -o "${ARCHIVE}" -d "${WORK_DIR}/source"
SOURCE_DIR="${WORK_DIR}/source/openssl-openssl-3.0.16"
[[ -f "${SOURCE_DIR}/Configure" ]] || { echo 'Unexpected OpenSSL source archive layout' >&2; exit 1; }
cd "${WORK_DIR}/ohos"
export CC="${NATIVE}/llvm/bin/clang"
export CXX="${NATIVE}/llvm/bin/clang++"
export AR="${NATIVE}/llvm/bin/llvm-ar"
export RANLIB="${NATIVE}/llvm/bin/llvm-ranlib"
export CROSS_COMPILE=""
export CFLAGS="--target=aarch64-linux-ohos -I${NATIVE}/sysroot/usr/include/aarch64-linux-ohos -fPIC -O2"
export LDFLAGS="--target=aarch64-linux-ohos"
perl "${SOURCE_DIR}/Configure" linux-aarch64 --prefix="${WORK_DIR}/install" \
  --openssldir="${WORK_DIR}/install/ssl" --libdir=lib shared no-asm no-tests \
  no-ui-console no-async no-engine no-module
make -j"${BUILD_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}" build_libs
verify_library libcrypto.so.3 && verify_library libssl.so.3
cp libcrypto.so.3 libssl.so.3 "${LIB_DIR}/"
cp "${SOURCE_DIR}/include/openssl/"*.h "${SOURCE_DIR}/include/openssl/"*.H "${INCLUDE_DIR}/"
cp include/openssl/*.h "${INCLUDE_DIR}/"
echo "[sdk-open-harmony] OpenSSL 3 ready: ${LIB_DIR}"
