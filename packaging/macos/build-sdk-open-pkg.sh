#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../../scripts/sdk-paths.sh"
REPO_ROOT="${SDK_OPEN_HOST_SOURCE_ROOT}"

ARCH="all"
BUILD_TYPE="Release"
OUTPUT_DIR="${REPO_ROOT}/output/sdk-open/macos"
DEFAULT_CODESIGN_IDENTITY="Developer ID Application: Dalian CZUR TECH CO.,LTD (C2UTRKBSL6)"
DEFAULT_INSTALLER_SIGN_IDENTITY="Developer ID Installer: Dalian CZUR TECH CO.,LTD (C2UTRKBSL6)"
DEFAULT_DEVELOPMENT_TEAM="C2UTRKBSL6"
DEFAULT_NOTARY_PROFILE="CZUR_DMG_PWD"
SKIP_WEB_BUILD=0
SKIP_BUILD=0
SKIP_NOTARY=0
SKIP_SIGN=0
SKIP_PKG_SIGN=0
CLEAN=0
DRY_RUN=0
APP_SIGN_IDENTITY="${CZUR_MAC_APP_SIGN_IDENTITY:-${CZUR_MAC_CODESIGN_IDENTITY:-${DEFAULT_CODESIGN_IDENTITY}}}"
DEBUG_SIGN_IDENTITY="${CZUR_MAC_DEBUG_CODE_SIGN_IDENTITY:-${APP_SIGN_IDENTITY}}"
INSTALLER_SIGN_IDENTITY="${CZUR_MAC_INSTALLER_SIGN_IDENTITY:-${DEFAULT_INSTALLER_SIGN_IDENTITY}}"
DEVELOPMENT_TEAM="${CZUR_MAC_DEVELOPMENT_TEAM:-${DEFAULT_DEVELOPMENT_TEAM}}"
NOTARY_PROFILE="${CZUR_MAC_NOTARY_PROFILE:-${DEFAULT_NOTARY_PROFILE}}"
SDK_OPEN_ENTITLEMENTS="${SCRIPT_DIR}/sdk-open-entitlements.plist"
SDK_OPEN_LAUNCH_AGENT_PLIST="${SCRIPT_DIR}/com.czur.sdk-open.plist"

usage() {
    echo "Usage: $0 [options]"
    echo "  --arch arm64|x86_64|all"
    echo "  --build-type Debug|Release"
    echo "  --output-dir DIR"
    echo "  --skip-web-build  Use existing src/sdk_open/web assets"
    echo "  --skip-build      Package existing architecture build directories"
    echo "  --skip-notary     Do not submit or staple the PKG"
    echo "  --skip-sign       Do not sign Mach-O files or the product PKG"
    echo "  --skip-pkg-sign   Sign Mach-O files, but leave the outer PKG unsigned (local testing only)"
    echo "  --app-sign-identity ID"
    echo "  --debug-sign-identity ID"
    echo "  --installer-sign-identity ID"
    echo "  --development-team TEAM_ID"
    echo "  --notary-profile KEYCHAIN_PROFILE"
    echo "  --clean"
    echo "  --dry-run"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) ARCH="$2"; shift 2 ;;
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --app-sign-identity) APP_SIGN_IDENTITY="$2"; shift 2 ;;
        --debug-sign-identity) DEBUG_SIGN_IDENTITY="$2"; shift 2 ;;
        --installer-sign-identity) INSTALLER_SIGN_IDENTITY="$2"; shift 2 ;;
        --development-team) DEVELOPMENT_TEAM="$2"; shift 2 ;;
        --notary-profile) NOTARY_PROFILE="$2"; shift 2 ;;
        --skip-web-build) SKIP_WEB_BUILD=1; shift ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --skip-notary) SKIP_NOTARY=1; shift ;;
        --skip-sign) SKIP_SIGN=1; shift ;;
        --skip-pkg-sign) SKIP_PKG_SIGN=1; shift ;;
        --clean) CLEAN=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "${ARCH}" in arm64|x86_64|all) ;; *) echo "Invalid --arch: ${ARCH}" >&2; exit 2 ;; esac
case "${BUILD_TYPE}" in Debug|Release) ;; *) echo "Invalid --build-type: ${BUILD_TYPE}" >&2; exit 2 ;; esac

VERSION_FILE="${SDK_OPEN_SOURCE_ROOT}/version/version.ini"
VERSION="$(sed -n 's/^version=//p' "${VERSION_FILE}" | head -1)"
BUILD="$(sed -n 's/^build=//p' "${VERSION_FILE}" | head -1)"
STAGE="$(sed -n 's/^stage=//p' "${VERSION_FILE}" | head -1)"
PKG_VERSION="${VERSION}.${BUILD}"
HELPER_VERSION="$(sed -n 's/^daemon_version=//p' "${REPO_ROOT}/packaging/mac/daemon_version.ini" | head -1)"

if [[ "${ARCH}" == "all" ]]; then
    ARCHES=(arm64 x86_64)
else
    ARCHES=("${ARCH}")
fi

run() {
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        printf '+ '
        printf '%q ' "$@"
        printf '\n'
    else
        "$@"
    fi
}

require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required tool not found: $1" >&2
        exit 1
    fi
}

has_codesign_identity() {
    security find-identity -v -p codesigning 2>/dev/null | grep -Fq "\"$1\""
}

has_installer_identity() {
    security find-identity -v -p basic 2>/dev/null | grep -Fq "\"$1\""
}

for tool in cmake ninja pkgbuild productbuild codesign ditto file security pkgutil plutil dsymutil dwarfdump python3; do
    require_tool "${tool}"
done

if [[ "${DRY_RUN}" -eq 0 ]]; then
    # Native trust policy enforces certificate lifetime limits that OpenSSL
    # clients do not check. Explicitly anchor to the packaged CA for this
    # build-time check; the installer separately configures System trust.
    security verify-cert \
        -c "${SDK_OPEN_SOURCE_ROOT}/packaging/tls/runtime/sdk-runtime.localhost.fullchain.pem" \
        -r "${SDK_OPEN_SOURCE_ROOT}/packaging/tls/runtime/local-runtime-root-ca.crt" \
        -p ssl -s sdk-runtime.localhost >/dev/null
    plutil -lint "${SDK_OPEN_ENTITLEMENTS}" >/dev/null
    plutil -lint "${SDK_OPEN_LAUNCH_AGENT_PLIST}" >/dev/null
    camera_entitlement_value="$(
        /usr/libexec/PlistBuddy \
            -c "Print :com.apple.security.device.camera" \
            "${SDK_OPEN_ENTITLEMENTS}" 2>/dev/null || true
    )"
    if [[ "${camera_entitlement_value}" != "true" ]]; then
        echo "SDK Open Camera entitlement must be true: ${SDK_OPEN_ENTITLEMENTS}" >&2
        exit 1
    fi
    launch_agent_program="$(
        /usr/libexec/PlistBuddy -c "Print :Program" \
            "${SDK_OPEN_LAUNCH_AGENT_PLIST}" 2>/dev/null || true
    )"
    launch_agent_argument_zero="$(
        /usr/libexec/PlistBuddy -c "Print :ProgramArguments:0" \
            "${SDK_OPEN_LAUNCH_AGENT_PLIST}" 2>/dev/null || true
    )"
    if [[ "${launch_agent_program}" != "/Library/CZUR/sdk-open/sdk_open_app" ||
          "${launch_agent_argument_zero}" != "/Library/CZUR/sdk-open/sdk_open_app" ]]; then
        echo "SDK Open LaunchAgent must execute the signed sdk_open_app directly: ${SDK_OPEN_LAUNCH_AGENT_PLIST}" >&2
        exit 1
    fi
fi

read_macho_bundle_version() {
    plutil -p "$1" 2>/dev/null |
        sed -n 's/.*"CFBundleVersion" => "\([0-9.][0-9.]*\)".*/\1/p' |
        head -1
}

if [[ "${BUILD_TYPE}" == "Debug" && "${SKIP_SIGN}" -eq 0 ]] &&
   ! has_codesign_identity "${DEBUG_SIGN_IDENTITY}"; then
    echo "Debug signing identity is unavailable; falling back to ad-hoc signing: ${DEBUG_SIGN_IDENTITY}" >&2
    DEBUG_SIGN_IDENTITY="-"
fi

if [[ "${SKIP_SIGN}" -eq 1 ]]; then
    SKIP_PKG_SIGN=1
fi

if [[ "${DRY_RUN}" -eq 0 && "${BUILD_TYPE}" == "Release" && "${SKIP_SIGN}" -eq 0 ]]; then
    if [[ -z "${APP_SIGN_IDENTITY}" ]]; then
        echo "Release packaging requires a Developer ID Application identity." >&2
        exit 1
    fi
    if ! has_codesign_identity "${APP_SIGN_IDENTITY}"; then
        echo "Developer ID Application identity is unavailable or has no private key: ${APP_SIGN_IDENTITY}" >&2
        exit 1
    fi
fi
if [[ "${DRY_RUN}" -eq 0 && "${BUILD_TYPE}" == "Release" && "${SKIP_PKG_SIGN}" -eq 0 ]]; then
    require_tool productsign
    if [[ -z "${INSTALLER_SIGN_IDENTITY}" ]]; then
        echo "Release PKG signing requires CZUR_MAC_INSTALLER_SIGN_IDENTITY (Developer ID Installer)." >&2
        echo "The existing Developer ID Application identity can sign Mach-O/DMG files, but Apple does not allow it to sign a PKG." >&2
        echo "For a local unsigned outer PKG, pass --skip-pkg-sign --skip-notary; Mach-O files remain signed." >&2
        exit 1
    fi
    if ! has_installer_identity "${INSTALLER_SIGN_IDENTITY}"; then
        echo "Developer ID Installer identity is unavailable or has no private key: ${INSTALLER_SIGN_IDENTITY}" >&2
        echo "Run: security find-identity -v -p basic" >&2
        exit 1
    fi
fi
if [[ "${DRY_RUN}" -eq 0 && "${BUILD_TYPE}" == "Release" && "${SKIP_NOTARY}" -eq 0 ]]; then
    if [[ "${SKIP_PKG_SIGN}" -eq 1 ]]; then
        echo "Release notarization requires a Developer ID Installer-signed PKG; remove --skip-pkg-sign or add --skip-notary." >&2
        exit 1
    fi
    if [[ -z "${NOTARY_PROFILE}" ]]; then
        echo "Release notarization requires --notary-profile (or CZUR_MAC_NOTARY_PROFILE)." >&2
        exit 1
    fi
fi

if [[ "${SKIP_WEB_BUILD}" -eq 0 && "${SKIP_BUILD}" -eq 0 ]]; then
    require_tool pnpm
    run pnpm --dir "${SDK_OPEN_SOURCE_ROOT}/frontend/admin-site" install --force --no-frozen-lockfile
    run pnpm --dir "${SDK_OPEN_SOURCE_ROOT}/frontend/admin-site" build
    run pnpm --dir "${SDK_OPEN_SOURCE_ROOT}/frontend/demo-site" install --force --no-frozen-lockfile
    run pnpm --dir "${SDK_OPEN_SOURCE_ROOT}/frontend/demo-site" build
    run cmake -E remove_directory "${SDK_OPEN_SOURCE_ROOT}/web/admin"
    run cmake -E remove_directory "${SDK_OPEN_SOURCE_ROOT}/web/demo"
    run cmake -E copy_directory "${SDK_OPEN_SOURCE_ROOT}/frontend/admin-site/dist" "${SDK_OPEN_SOURCE_ROOT}/web/admin"
    run cmake -E copy_directory "${SDK_OPEN_SOURCE_ROOT}/frontend/demo-site/dist" "${SDK_OPEN_SOURCE_ROOT}/web/demo"
fi

run mkdir -p "${OUTPUT_DIR}"

sign_macho_tree() {
    local root="$1"
    local identity="$2"
    shift 2
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        echo "+ codesign Mach-O files below ${root} with identity ${identity:-<release-identity>}"
        return
    fi
    while IFS= read -r candidate; do
        local skip_candidate=0
        local excluded
        for excluded in "$@"; do
            if [[ "${candidate}" == "${excluded}" ]]; then
                skip_candidate=1
                break
            fi
        done
        if [[ "${skip_candidate}" -eq 1 ]]; then
            continue
        fi
        if file "${candidate}" | grep -q 'Mach-O'; then
            if [[ "${BUILD_TYPE}" == "Release" ]]; then
                run codesign --force --sign "${identity}" --options runtime --timestamp "${candidate}"
            elif [[ "${identity}" != "-" ]]; then
                run codesign --force --sign "${identity}" --options runtime "${candidate}"
            else
                run codesign --force --sign - "${candidate}"
            fi
            run codesign --verify --strict --verbose=2 "${candidate}"
        fi
    done < <(find "${root}" -type f -print)
}

sign_named_binary() {
    local binary="$1"
    local identifier="$2"
    local identity="$3"
    local entitlements="${4:-}"
    if [[ "${BUILD_TYPE}" == "Release" ]]; then
        if [[ -n "${entitlements}" ]]; then
            run codesign --force --sign "${identity}" --identifier "${identifier}" \
                --options runtime --timestamp --entitlements "${entitlements}" "${binary}"
        else
            run codesign --force --sign "${identity}" --identifier "${identifier}" \
                --options runtime --timestamp "${binary}"
        fi
    elif [[ "${identity}" != "-" ]]; then
        if [[ -n "${entitlements}" ]]; then
            run codesign --force --sign "${identity}" --identifier "${identifier}" \
                --options runtime --entitlements "${entitlements}" "${binary}"
        else
            run codesign --force --sign "${identity}" --identifier "${identifier}" \
                --options runtime "${binary}"
        fi
    else
        if [[ -n "${entitlements}" ]]; then
            run codesign --force --sign - --identifier "${identifier}" \
                --entitlements "${entitlements}" "${binary}"
        else
            run codesign --force --sign - --identifier "${identifier}" "${binary}"
        fi
    fi
    run codesign --verify --strict --verbose=2 "${binary}"
}

verify_camera_entitlement() {
    local binary="$1"
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        echo "+ verify Camera entitlement in ${binary}"
        return
    fi

    local signed_entitlements
    signed_entitlements="$(codesign -d --entitlements - "${binary}" 2>&1 || true)"
    if [[ "${signed_entitlements}" != *"com.apple.security.device.camera"* ]]; then
        echo "Signed sdk_open_app is missing com.apple.security.device.camera: ${binary}" >&2
        echo "${signed_entitlements}" >&2
        exit 1
    fi
}

export_sdk_dsyms() {
    local dsym_root="$1"
    local archive_path="$2"
    shift 2

    run cmake -E remove_directory "${dsym_root}"
    run mkdir -p "${dsym_root}"
    local binary
    for binary in "$@"; do
        local dsym_path="${dsym_root}/$(basename "${binary}").dSYM"
        echo "Generating dSYM: ${binary}"
        run dsymutil "${binary}" -o "${dsym_path}"
        if [[ "${DRY_RUN}" -eq 0 ]]; then
            # dsymutil can succeed with an empty bundle for older Release
            # outputs compiled without -g, including --skip-build inputs.
            if ! dwarfdump --debug-info --recurse-depth=0 "${dsym_path}" |
                grep 'DW_TAG_compile_unit' >/dev/null; then
                echo "dSYM has no debug information: ${dsym_path}" >&2
                echo "Rebuild with this packaging script without --skip-build to enable debug symbols." >&2
                exit 1
            fi
            local binary_uuids
            local dsym_uuids
            binary_uuids="$(dwarfdump --uuid "${binary}" | awk '/UUID:/ {print $2, $3}' | sort)"
            dsym_uuids="$(dwarfdump --uuid "${dsym_path}" | awk '/UUID:/ {print $2, $3}' | sort)"
            if [[ -z "${binary_uuids}" || "${binary_uuids}" != "${dsym_uuids}" ]]; then
                echo "dSYM UUID does not match binary: ${binary}" >&2
                exit 1
            fi
        fi
    done

    # Keep symbols outside the PKG payload and replace any previous archive.
    run cmake -E rm -f "${archive_path}"
    run ditto -c -k --keepParent "${dsym_root}" "${archive_path}"
    echo "dSYM output: ${archive_path}"
}

write_symbols_manifest() {
    local target_arch="$1"
    local build_dir="${REPO_ROOT}/build/sdk-open-macos-${target_arch}-${BUILD_TYPE}"
    local package_name="sdk-open_macos_${target_arch}_v${VERSION}.${BUILD}.${STAGE}.pkg"
    local package_path="${OUTPUT_DIR}/${package_name}"
    local archive_path="${OUTPUT_DIR}/${package_name%.pkg}.dSYM.zip"
    local manifest_path="${OUTPUT_DIR}/${package_name%.pkg}.symbols.json"
    local git_commit
    local sdk_open_commit
    git_commit="$(git -C "${REPO_ROOT}" rev-parse HEAD)"
    sdk_open_commit="$(git -C "${SDK_OPEN_SOURCE_ROOT}" rev-parse HEAD)"

    python3 - \
        "${manifest_path}" \
        "${VERSION}" "${BUILD}" "${STAGE}" "${target_arch}" "${BUILD_TYPE}" \
        "${PARENT_BUILD_COMMIT:-${git_commit}}" "${sdk_open_commit}" \
        "${package_path}" "${archive_path}" \
        "sdk_open_app" "${build_dir}/dSYMs/sdk_open_app.dSYM" \
        "com.czur.device-helper" "${build_dir}/dSYMs/com.czur.device-helper.dSYM" \
        "libsdk_private_providers.dylib" "${build_dir}/dSYMs/libsdk_private_providers.dylib.dSYM" \
        "libsdk_private_auth_c_api.dylib" "${build_dir}/dSYMs/libsdk_private_auth_c_api.dylib.dSYM" <<'PY'
import hashlib
import json
import os
import re
import subprocess
import sys

(
    manifest_path,
    version,
    build,
    stage,
    architecture,
    build_type,
    parent_build_commit,
    sdk_open_commit,
    package_path,
    symbol_path,
    *binary_arguments,
) = sys.argv[1:]


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file_obj:
        for chunk in iter(lambda: file_obj.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


binaries = []
for index in range(0, len(binary_arguments), 2):
    name = binary_arguments[index]
    dsym_path = binary_arguments[index + 1]
    output = subprocess.check_output(
        ["dwarfdump", "--uuid", dsym_path], text=True
    )
    matches = re.findall(r"UUID:\s+([0-9A-Fa-f-]+)\s+\(([^)]+)\)", output)
    if len(matches) != 1:
        raise RuntimeError(
            "Expected exactly one UUID for {0}, found: {1}".format(name, matches)
        )
    uuid, uuid_arch = matches[0]
    if uuid_arch != architecture:
        raise RuntimeError(
            "dSYM architecture mismatch for {0}: expected {1}, got {2}".format(
                name, architecture, uuid_arch
            )
        )
    binaries.append({"name": name, "uuid": uuid.upper()})

manifest = {
    "version": version,
    "build": build,
    "stage": stage,
    "arch": architecture,
    "build_type": build_type,
    "parent_build_commit": parent_build_commit,
    "sdk_open_commit": sdk_open_commit,
    "package": {
        "name": os.path.basename(package_path),
        "sha256": sha256_file(package_path),
    },
    "symbols": {
        "name": os.path.basename(symbol_path),
        "sha256": sha256_file(symbol_path),
        "binaries": binaries,
    },
}
with open(manifest_path, "w", encoding="utf-8", newline="\n") as file_obj:
    json.dump(manifest, file_obj, ensure_ascii=False, indent=2, sort_keys=True)
    file_obj.write("\n")
PY
    echo "Symbols manifest output: ${manifest_path}"
}

build_arch_pkg() {
    local target_arch="$1"
    local build_dir="${REPO_ROOT}/build/sdk-open-macos-${target_arch}-${BUILD_TYPE}"
    local work_dir="${build_dir}/pkg-work"
    local runtime_root="${work_dir}/runtime-root"
    local helper_root="${work_dir}/helper-root"
    local helper_scripts="${work_dir}/helper-scripts"
    local helper_payload_root="${helper_root}/Library/Application Support/CZUR/DeviceHelperUpdate/${HELPER_VERSION}"
    local component_dir="${work_dir}/components"
    local runtime_output="${build_dir}/${BUILD_TYPE}"
    local helper_binary="${build_dir}/src/app/process/device_daemon/com.czur.device-helper"
    local helper_plist="${build_dir}/generated/mac/device_helper-Launchd.plist"
    local filename="sdk-open_macos_${target_arch}_v${VERSION}.${BUILD}.${STAGE}.pkg"
    local unsigned_product="${work_dir}/${filename}"
    local final_product="${OUTPUT_DIR}/${filename}"
    local dsym_archive="${OUTPUT_DIR}/${filename%.pkg}.dSYM.zip"

    if [[ "${CLEAN}" -eq 1 ]]; then
        run cmake -E remove_directory "${build_dir}"
    fi

    if [[ "${SKIP_BUILD}" -eq 0 ]]; then
        # Match the normal macOS application build: retain Release
        # optimization while emitting symbols for C/C++/Objective-C sources.
        local cmake_symbol_flags=()
        if [[ "${BUILD_TYPE}" == "Release" ]]; then
            cmake_symbol_flags=(
                -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -g"
                -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -g"
                -DCMAKE_OBJC_FLAGS_RELEASE="-O3 -DNDEBUG -g"
                -DCMAKE_OBJCXX_FLAGS_RELEASE="-O3 -DNDEBUG -g"
                -DCMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT="dwarf-with-dsym"
            )
        fi
        run cmake -S "${REPO_ROOT}" -B "${build_dir}" -G Ninja \
            -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
            -DCMAKE_OSX_ARCHITECTURES="${target_arch}" \
            -DBUILD_SDK_OPEN=ON \
            -DBUILD_SDK_WEB=OFF \
            -DSDK_OPEN_USE_PREBUILT_WEB=ON \
            -DSDK_OPEN_ENABLE_HTTP_SERVER=ON \
            -DSDK_OPEN_ENABLE_TLS=ON \
            -DBUILD_SDK_PRIVATE=ON \
            -DSDK_OPEN_USE_PRIVATE_PROVIDER=ON \
            -DCZUR_MAC_DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM}" \
            -DCZUR_MAC_CODE_SIGN_IDENTITY="${APP_SIGN_IDENTITY}" \
            -DCZUR_MAC_DEBUG_CODE_SIGN_IDENTITY="${DEBUG_SIGN_IDENTITY}" \
            -DCZUR_MAC_CODE_SIGN_TIMESTAMP=ON \
            ${cmake_symbol_flags[@]+"${cmake_symbol_flags[@]}"}
        run cmake --build "${build_dir}" --target sdk_open_app czur_device_helper_daemon --parallel
    elif [[ "${DRY_RUN}" -eq 0 ]]; then
        # --skip-build must never silently package stale objects from this
        # script's architecture-specific build directory. A common local
        # workflow builds build_mac_arm64_sdk and then packages here, while
        # this script reads build/sdk-open-macos-arm64-Release instead.
        local ninja_dry_run
        if ! ninja_dry_run="$(ninja -C "${build_dir}" -n sdk_open_app czur_device_helper_daemon 2>&1)"; then
            echo "Cannot validate --skip-build outputs in ${build_dir}:" >&2
            echo "${ninja_dry_run}" >&2
            echo "Run this packaging command without --skip-build first." >&2
            exit 1
        fi
        if ! grep -Fq "ninja: no work to do." <<<"${ninja_dry_run}"; then
            echo "Refusing to package stale --skip-build outputs from ${build_dir}." >&2
            echo "Ninja reports pending work:" >&2
            echo "${ninja_dry_run}" >&2
            echo "Run this packaging command without --skip-build, or build the exact directory above." >&2
            exit 1
        fi
    fi

    if [[ "${DRY_RUN}" -eq 0 ]]; then
        for required in "${runtime_output}/sdk_open_app" "${helper_binary}" "${helper_plist}"; do
            if [[ ! -e "${required}" ]]; then
                echo "Build output is missing: ${required}" >&2
                exit 1
            fi
        done
        local built_helper_version
        built_helper_version="$(read_macho_bundle_version "${helper_binary}" || true)"
        if [[ "${built_helper_version}" != "${HELPER_VERSION}" ]]; then
            echo "Built helper version does not match packaging version: expected ${HELPER_VERSION}, got '${built_helper_version}'." >&2
            echo "Reconfigure and rebuild without --skip-build before packaging." >&2
            exit 1
        fi
    fi

    # Private provider/auth code is built into separate dylibs on macOS;
    # its symbols are not part of sdk_open_app's dSYM.
    export_sdk_dsyms "${build_dir}/dSYMs" "${dsym_archive}" \
        "${runtime_output}/sdk_open_app" "${helper_binary}" \
        "${runtime_output}/libsdk_private_providers.dylib" \
        "${runtime_output}/libsdk_private_auth_c_api.dylib"

    run cmake -E remove_directory "${work_dir}"
    run mkdir -p \
        "${runtime_root}/Library/CZUR/sdk-open" \
        "${runtime_root}/Library/LaunchAgents" \
        "${helper_payload_root}" \
        "${helper_scripts}" \
        "${component_dir}"
    run ditto "${runtime_output}" "${runtime_root}/Library/CZUR/sdk-open"
    run cp "${SCRIPT_DIR}/uninstall.sh" "${runtime_root}/Library/CZUR/sdk-open/uninstall.sh"
    run cp -R "${SDK_OPEN_SOURCE_ROOT}/packaging/tls/runtime" "${runtime_root}/Library/CZUR/sdk-open/tls"
    # Runtime credentials are generated by the signed binary during
    # postinstall; do not ship a reusable leaf private key in the PKG.
    run cmake -E rm -f \
        "${runtime_root}/Library/CZUR/sdk-open/tls/local-runtime-root-ca.crt" \
        "${runtime_root}/Library/CZUR/sdk-open/tls/sdk-runtime.localhost.fullchain.pem" \
        "${runtime_root}/Library/CZUR/sdk-open/tls/sdk-runtime.localhost.key.pem"
    run cp "${SCRIPT_DIR}/tls/legacy-sdk-runtime.localhost.pem" "${runtime_root}/Library/CZUR/sdk-open/tls/legacy-sdk-runtime.localhost.pem"
    run cp "${SDK_OPEN_LAUNCH_AGENT_PLIST}" "${runtime_root}/Library/LaunchAgents/com.czur.sdk-open.plist"
    run chmod 755 "${runtime_root}/Library/CZUR/sdk-open/sdk_open_app" "${runtime_root}/Library/CZUR/sdk-open/uninstall.sh"
    run cp "${helper_binary}" "${helper_payload_root}/com.czur.device-helper"
    run cp "${helper_plist}" "${helper_payload_root}/com.czur.device-helper.plist"
    run chmod 755 "${helper_payload_root}/com.czur.device-helper"
    run chmod 644 "${helper_payload_root}/com.czur.device-helper.plist"

    if [[ "${DRY_RUN}" -eq 1 ]]; then
        echo "+ render helper preinstall with minimum version ${HELPER_VERSION}"
    else
        sed -e "s/@HELPER_VERSION@/${HELPER_VERSION}/g" \
            "${SCRIPT_DIR}/scripts/helper/preinstall" > "${helper_scripts}/preinstall"
        sed -e "s/@HELPER_VERSION@/${HELPER_VERSION}/g" \
            "${SCRIPT_DIR}/scripts/helper/postinstall" > "${helper_scripts}/postinstall"
        chmod 755 "${helper_scripts}/preinstall" "${helper_scripts}/postinstall"
    fi

    if [[ "${SKIP_SIGN}" -eq 0 ]]; then
        local identity="${APP_SIGN_IDENTITY}"
        if [[ "${BUILD_TYPE}" == "Debug" ]]; then identity="${DEBUG_SIGN_IDENTITY}"; fi
        local runtime_binary="${runtime_root}/Library/CZUR/sdk-open/sdk_open_app"
        local staged_helper="${helper_payload_root}/com.czur.device-helper"
        # 主程序和 helper 需要固定 identifier，不能先在通用 Mach-O 遍历中签名
        # 后再重签；Developer ID + timestamp 连续重签可能产生不可用的签名。
        sign_macho_tree "${runtime_root}/Library/CZUR/sdk-open" "${identity}" \
            "${runtime_binary}"
        sign_macho_tree "${helper_payload_root}" "${identity}" \
            "${staged_helper}"
        sign_named_binary "${runtime_root}/Library/CZUR/sdk-open/sdk_open_app" \
            com.czur.sdk-open "${identity}" "${SDK_OPEN_ENTITLEMENTS}"
        verify_camera_entitlement "${runtime_binary}"
        sign_named_binary \
            "${staged_helper}" \
            com.czur.device-helper "${identity}"
    fi

    run pkgbuild --root "${runtime_root}" \
        --scripts "${SCRIPT_DIR}/scripts/runtime" \
        --identifier com.czur.sdk-open.runtime \
        --version "${PKG_VERSION}" \
        --install-location / \
        "${component_dir}/sdk-open-runtime.pkg"
    run pkgbuild --root "${helper_root}" \
        --scripts "${helper_scripts}" \
        --identifier com.czur.device-helper \
        --version "${HELPER_VERSION}" \
        --install-location / \
        "${component_dir}/device-helper.pkg"

    if [[ "${SKIP_SIGN}" -eq 0 ]]; then
        # pkgbuild 不应改写源 root；再次验证，避免把损坏签名装进产品包。
        run codesign --verify --strict --verbose=2 \
            "${runtime_root}/Library/CZUR/sdk-open/sdk_open_app"
        run codesign --verify --strict --verbose=2 \
            "${helper_payload_root}/com.czur.device-helper"
    fi

    if [[ "${DRY_RUN}" -eq 1 ]]; then
        echo "+ render ${SCRIPT_DIR}/Distribution.xml.in -> ${work_dir}/Distribution.xml"
    else
        sed -e "s/@ARCH@/${target_arch}/g" \
            -e "s/@VERSION@/${PKG_VERSION}/g" \
            -e "s/@HELPER_VERSION@/${HELPER_VERSION}/g" \
            "${SCRIPT_DIR}/Distribution.xml.in" > "${work_dir}/Distribution.xml"
    fi
    run productbuild --distribution "${work_dir}/Distribution.xml" \
        --package-path "${component_dir}" "${unsigned_product}"

    # 不让 productsign/cp 复用上一次输出文件，避免残留签名数据污染新包。
    run cmake -E rm -f "${final_product}"
    if [[ "${BUILD_TYPE}" == "Release" && "${SKIP_PKG_SIGN}" -eq 0 ]]; then
        run productsign --sign "${INSTALLER_SIGN_IDENTITY}" --timestamp "${unsigned_product}" "${final_product}"
        run pkgutil --check-signature "${final_product}"
    else
        run cp "${unsigned_product}" "${final_product}"
    fi

    if [[ "${BUILD_TYPE}" == "Release" && "${SKIP_NOTARY}" -eq 0 ]]; then
        run xcrun notarytool submit "${final_product}" --keychain-profile "${NOTARY_PROFILE}" --wait
        run xcrun stapler staple "${final_product}"
        run xcrun stapler validate "${final_product}"
    fi
}

for target_arch in "${ARCHES[@]}"; do
    build_arch_pkg "${target_arch}"
done

if [[ "${DRY_RUN}" -eq 0 ]]; then
    for target_arch in "${ARCHES[@]}"; do
        write_symbols_manifest "${target_arch}"
    done
    (
        cd "${OUTPUT_DIR}"
        shasum -a 256 sdk-open_macos_*_v"${VERSION}.${BUILD}.${STAGE}.pkg" \
            sdk-open_macos_*_v"${VERSION}.${BUILD}.${STAGE}.dSYM.zip" > SHA256SUMS.txt
    )
    echo "PKG output: ${OUTPUT_DIR}"
fi
