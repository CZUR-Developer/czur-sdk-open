#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: jenkins_build_sdk_open_from_workspace.sh [options]

Run the SDK Open macOS PKG build from the current repository workspace.

Options:
  --arch <arm64|x86_64>        Target architecture (default: arm64).
  --build-type <Release|Debug> CMake build type (default: Release).
  --output-dir <dir>           Jenkins staging directory (required).
  --skip-web-build             Reuse checked-in SDK Open web assets.
  --skip-build                 Package an already configured SDK Open build.
  --skip-notary                Do not submit or staple the PKG.
  --skip-sign                  Do not sign Mach-O files or the PKG.
  --skip-pkg-sign              Sign Mach-O files but leave the outer PKG unsigned.
  --clean                      Remove the architecture-specific CMake build directory.
  -h, --help                   Show this help.

Environment:
  USE_PARENT_BUILD_METADATA  Set to true for a parent Job invocation.
  PARENT_BUILD_COMMIT        Required in parent mode; expected superproject commit.
  PARENT_JOB_NAME            Optional parent Job name for the manifest.
  PARENT_BUILD_NUMBER        Optional parent build number for the manifest.
  OSS_TARGET_DIR             Parent-mode OSS destination (recorded in the manifest).
  CZUR_MAC_NODE_BIN            Optional Node/pnpm bin directory.
  CZUR_MAC_KEYCHAIN_PASSWORD   Optional password used to unlock the keychain.
  CZUR_MAC_KEYCHAIN_PATH       Optional keychain path.
  CZUR_MAC_APP_SIGN_IDENTITY   Optional Developer ID Application identity.
  CZUR_MAC_CODESIGN_IDENTITY   Compatibility alias for app signing identity.
  CZUR_MAC_DEBUG_CODE_SIGN_IDENTITY
                               Optional Debug signing identity.
  CZUR_MAC_INSTALLER_SIGN_IDENTITY
                               Optional Developer ID Installer identity.
  CZUR_MAC_DEVELOPMENT_TEAM    Optional Apple Team ID.
  CZUR_MAC_NOTARY_PROFILE      Optional notarytool keychain profile.
EOF
}

die() {
    echo "Error: $*" >&2
    exit 1
}

read_config_field() {
    local file="$1"
    local field="$2"
    sed -n "s/^[[:space:]]*${field}[[:space:]]*=[[:space:]]*//p" "$file" | head -1 | sed 's/[[:space:]]*$//' || true
}

prepend_path() {
    local directory="$1"
    [ -d "$directory" ] || return 0

    case ":${PATH:-}:" in
        *":$directory:"*) ;;
        *) PATH="$directory${PATH:+:$PATH}" ;;
    esac
}

setup_ci_path() {
    local directory
    local nvm_bin

    for directory in /sbin /usr/sbin /bin /usr/bin /usr/local/bin /opt/homebrew/bin; do
        prepend_path "$directory"
    done

    if [ -n "${CZUR_MAC_NODE_BIN:-}" ]; then
        [ -d "$CZUR_MAC_NODE_BIN" ] || die "CZUR_MAC_NODE_BIN is not a directory: $CZUR_MAC_NODE_BIN"
        prepend_path "$CZUR_MAC_NODE_BIN"
    elif [ -n "${HOME:-}" ]; then
        for nvm_bin in "$HOME"/.nvm/versions/node/v22.*/bin; do
            if [ -x "$nvm_bin/node" ] && [ -x "$nvm_bin/pnpm" ]; then
                prepend_path "$nvm_bin"
                break
            fi
        done
    fi

    export PATH
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1 (PATH=${PATH:-})"
}

unlock_keychain_if_requested() {
    if [ -z "${CZUR_MAC_KEYCHAIN_PASSWORD:-}" ]; then
        return 0
    fi

    local keychain_path="${CZUR_MAC_KEYCHAIN_PATH:-$HOME/Library/Keychains/login.keychain-db}"
    [ -f "$keychain_path" ] || die "keychain not found: $keychain_path"

    echo "--->> Unlocking keychain: $keychain_path"
    security unlock-keychain -p "$CZUR_MAC_KEYCHAIN_PASSWORD" "$keychain_path"
    security set-keychain-settings -lut 21600 "$keychain_path"
    security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$CZUR_MAC_KEYCHAIN_PASSWORD" "$keychain_path"
}

main() {
    local arch="arm64"
    local build_type="Release"
    local output_dir=""
    local skip_web_build="false"
    local skip_build="false"
    local skip_notary="false"
    local skip_sign="false"
    local skip_pkg_sign="false"
    local clean="false"
    local use_parent_build_metadata="${USE_PARENT_BUILD_METADATA:-false}"
    local parent_build_commit="${PARENT_BUILD_COMMIT:-}"
    local parent_job_name="${PARENT_JOB_NAME:-}"
    local parent_build_number="${PARENT_BUILD_NUMBER:-}"
    local oss_target_dir="${OSS_TARGET_DIR:-}"

    while [ "$#" -gt 0 ]; do
        case "$1" in
            --arch)
                [ "$#" -ge 2 ] || die "--arch requires a value"
                arch="$2"
                shift 2
                ;;
            --build-type)
                [ "$#" -ge 2 ] || die "--build-type requires a value"
                build_type="$2"
                shift 2
                ;;
            --output-dir)
                [ "$#" -ge 2 ] || die "--output-dir requires a value"
                output_dir="$2"
                shift 2
                ;;
            --skip-web-build) skip_web_build="true"; shift ;;
            --skip-build) skip_build="true"; shift ;;
            --skip-notary) skip_notary="true"; shift ;;
            --skip-sign) skip_sign="true"; shift ;;
            --skip-pkg-sign) skip_pkg_sign="true"; shift ;;
            --clean) clean="true"; shift ;;
            -h|--help) usage; return 0 ;;
            *) die "unknown argument: $1" ;;
        esac
    done

    [ "$(uname -s)" = "Darwin" ] || die "this script must run on macOS"
    [ -n "$output_dir" ] || die "--output-dir is required"
    case "$arch" in arm64|x86_64) ;; *) die "unsupported --arch: $arch" ;; esac
    case "$build_type" in Release|Debug) ;; *) die "unsupported --build-type: $build_type" ;; esac
    [ "$output_dir" != "/" ] || die "refusing to use / as --output-dir"

    case "$use_parent_build_metadata" in
        true|false) ;;
        *) die "USE_PARENT_BUILD_METADATA must be true or false" ;;
    esac
    if [ "$use_parent_build_metadata" = "true" ]; then
        printf '%s\n' "$parent_build_commit" | grep -Eq '^[0-9a-fA-F]{40}$' \
            || die "PARENT_BUILD_COMMIT must be a 40-character hexadecimal commit"
        [ -n "$oss_target_dir" ] || die "OSS_TARGET_DIR is required in parent mode"
        case "$oss_target_dir" in
            *[[:cntrl:]]*|*..*|*\**|*\?*|*\[*|*\]*|*\|*)
                die "OSS_TARGET_DIR contains invalid characters" ;;
        esac
        case "$parent_job_name" in
            *[[:cntrl:]]*) die "PARENT_JOB_NAME contains control characters" ;;
        esac
        case "$parent_build_number" in
            *[[:cntrl:]]*) die "PARENT_BUILD_NUMBER contains control characters" ;;
        esac
        case "$oss_target_dir" in
            "oss://czur-capeditor-build/sdk-open/Package/Development/"*"/macos/$arch/") ;;
            *) die "OSS_TARGET_DIR must be under SDK Open Development and end with /macos/$arch/" ;;
        esac
    fi

    setup_ci_path
    for command_name in bash cmake ninja pkgbuild productbuild codesign ditto file security pkgutil plutil; do
        require_command "$command_name"
    done
    require_command git
    require_command xcrun
    if [ "$skip_web_build" = "false" ] && [ "$skip_build" = "false" ]; then
        require_command pnpm
    fi

    local workspace_dir
    local git_root
    workspace_dir="$(pwd -P)"
    git_root="$(git rev-parse --show-toplevel 2>/dev/null)" || die "current directory is not a Git repository"
    git_root="$(cd "$git_root" && pwd -P)"
    [ "$workspace_dir" = "$git_root" ] || die "run this script from the repository root: $git_root"

    local version_file="$workspace_dir/src/sdk_open/version/version.ini"
    [ -f "$version_file" ] || die "SDK Open version file not found: $version_file"
    [ -x "$workspace_dir/src/sdk_open/packaging/macos/build-sdk-open-pkg.sh" ] || die "SDK Open macOS packaging script is not executable"

    echo "--->> Preparing Jenkins workspace"
    git submodule sync --recursive
    # A failed first checkout can leave a submodule at the requested commit
    # while its index still contains thousands of staged deletions. A normal
    # update then skips the checkout because HEAD already matches. Force the
    # checkout so persistent Jenkins workspaces heal that partial state.
    git submodule update --init --recursive --force
    local git_commit
    git_commit="$(git rev-parse HEAD)"
    if [ "$use_parent_build_metadata" = "true" ]; then
        [ "$git_commit" = "$parent_build_commit" ] \
            || die "checked out commit $git_commit does not match PARENT_BUILD_COMMIT"
    fi
    # Keep dependency preparation identical to the macOS client pipeline. CEF
    # and ABBYY archives are intentionally cached outside Git on the Mac agent.
    bash -c 'source "$1"; prepare_macos_dependencies "$2" "$3"' \
        _ \
        "$workspace_dir/build_and_pack/scripts/macos/jenkins_build_from_workspace.sh" \
        "$workspace_dir" \
        "$arch"

    local version
    local build
    local stage
    version="$(read_config_field "$version_file" version)"
    build="$(read_config_field "$version_file" build)"
    stage="$(read_config_field "$version_file" stage)"
    [ -n "$version" ] || die "version is empty in $version_file"
    [ -n "$build" ] || die "build is empty in $version_file"
    [ -n "$stage" ] || die "stage is empty in $version_file"
    printf '%s\n' "$build" | grep -Eq '^[0-9]+$' || die "build is not numeric in $version_file"

    local sdk_open_commit
    sdk_open_commit="$(git -C "$workspace_dir/src/sdk_open" rev-parse HEAD 2>/dev/null)" \
        || die "cannot determine src/sdk_open commit"

    mkdir -p "$output_dir"
    output_dir="$(cd "$output_dir" && pwd -P)"
    case "$output_dir" in
        "$workspace_dir"/artifacts/sdk-open/macos/*) ;;
        *) die "--output-dir must be below $workspace_dir/artifacts/sdk-open/macos" ;;
    esac
    # The staging directory is generated by this invocation. Removing only its
    # children prevents stale PKGs/checksums from being uploaded on retries.
    find "$output_dir" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +

    unlock_keychain_if_requested

    local build_args=(
        --arch "$arch"
        --build-type "$build_type"
        --output-dir "$output_dir"
    )
    [ "$skip_web_build" = "true" ] && build_args+=(--skip-web-build)
    [ "$skip_build" = "true" ] && build_args+=(--skip-build)
    [ "$skip_notary" = "true" ] && build_args+=(--skip-notary)
    [ "$skip_sign" = "true" ] && build_args+=(--skip-sign)
    [ "$skip_pkg_sign" = "true" ] && build_args+=(--skip-pkg-sign)
    [ "$clean" = "true" ] && build_args+=(--clean)

    local log_path="$output_dir/jenkins-sdk-open-macos-build.log"
    echo "--->> Starting SDK Open macOS package build"
    echo "arch=$arch"
    echo "build_type=$build_type"
    echo "version=$version"
    echo "build=$build"
    echo "stage=${stage:-unknown}"
    echo "output_dir=$output_dir"
    "$workspace_dir/src/sdk_open/packaging/macos/build-sdk-open-pkg.sh" "${build_args[@]}" 2>&1 | tee "$log_path"

    local expected_pkg="sdk-open_macos_${arch}_v${version}.${build}.${stage}.pkg"
    local expected_symbols="${expected_pkg%.pkg}.dSYM.zip"
    local expected_symbols_manifest="${expected_pkg%.pkg}.symbols.json"
    local pkg_count
    pkg_count="$(find "$output_dir" -maxdepth 1 -type f -name '*.pkg' | wc -l | tr -d '[:space:]')"
    [ "$pkg_count" -eq 1 ] || die "SDK Open macOS build must produce exactly one PKG (found $pkg_count)"
    local actual_pkg
    actual_pkg="$(find "$output_dir" -maxdepth 1 -type f -name '*.pkg' -print -quit)"
    [ "$(basename "$actual_pkg")" = "$expected_pkg" ] \
        || die "unexpected PKG name: $(basename "$actual_pkg"); expected $expected_pkg"
    local symbols_count
    symbols_count="$(find "$output_dir" -maxdepth 1 -type f -name '*.dSYM.zip' | wc -l | tr -d '[:space:]')"
    [ "$symbols_count" -eq 1 ] \
        || die "SDK Open macOS build must produce exactly one dSYM archive (found $symbols_count)"
    [ -f "$output_dir/$expected_symbols" ] \
        || die "SDK Open macOS build did not produce expected dSYM archive: $expected_symbols"
    local symbols_manifest_count
    symbols_manifest_count="$(find "$output_dir" -maxdepth 1 -type f -name '*.symbols.json' | wc -l | tr -d '[:space:]')"
    [ "$symbols_manifest_count" -eq 1 ] \
        || die "SDK Open macOS build must produce exactly one symbols manifest (found $symbols_manifest_count)"
    [ -f "$output_dir/$expected_symbols_manifest" ] \
        || die "SDK Open macOS build did not produce expected symbols manifest: $expected_symbols_manifest"
    [ -f "$output_dir/SHA256SUMS.txt" ] || die "SDK Open macOS build did not produce SHA256SUMS.txt"
    local checksum_entry_count
    checksum_entry_count="$(awk 'NF { count++ } END { print count + 0 }' "$output_dir/SHA256SUMS.txt")"
    [ "$checksum_entry_count" -eq 2 ] \
        || die "SHA256SUMS.txt must contain exactly two entries (found $checksum_entry_count)"
    local expected_checksum_targets
    local actual_checksum_targets
    expected_checksum_targets="$(printf '%s\n%s\n' "$expected_pkg" "$expected_symbols" | sort)"
    actual_checksum_targets="$(awk 'NF { target=$2; sub(/^\\*/, "", target); print target }' "$output_dir/SHA256SUMS.txt" | sort)"
    [ "$actual_checksum_targets" = "$expected_checksum_targets" ] \
        || die "SHA256SUMS.txt must cover exactly $expected_pkg and $expected_symbols"
    (
        cd "$output_dir"
        shasum -a 256 -c SHA256SUMS.txt
    ) || die "SHA256SUMS.txt verification failed"

    local package_sha256
    local symbol_sha256
    local symbols_manifest_sha256
    package_sha256="$(shasum -a 256 "$output_dir/$expected_pkg" | awk '{print $1}')"
    symbol_sha256="$(shasum -a 256 "$output_dir/$expected_symbols" | awk '{print $1}')"
    symbols_manifest_sha256="$(shasum -a 256 "$output_dir/$expected_symbols_manifest" | awk '{print $1}')"
    python3 - \
        "$output_dir/$expected_symbols_manifest" \
        "$version" "$build" "$stage" "$arch" "$build_type" \
        "${parent_build_commit:-$git_commit}" "$sdk_open_commit" \
        "$expected_pkg" "$package_sha256" "$expected_symbols" "$symbol_sha256" <<'PY'
import json
import re
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
    package_name,
    package_sha256,
    symbol_name,
    symbol_sha256,
) = sys.argv[1:]
with open(manifest_path, "r", encoding="utf-8") as file_obj:
    manifest = json.load(file_obj)

expected_fields = {
    "version": version,
    "build": build,
    "stage": stage,
    "arch": architecture,
    "build_type": build_type,
    "parent_build_commit": parent_build_commit,
    "sdk_open_commit": sdk_open_commit,
}
for key, expected in expected_fields.items():
    if manifest.get(key) != expected:
        raise RuntimeError(
            "symbols manifest field {0} mismatch: expected {1!r}, got {2!r}".format(
                key, expected, manifest.get(key)
            )
        )
if manifest.get("package") != {"name": package_name, "sha256": package_sha256}:
    raise RuntimeError("symbols manifest package metadata mismatch")
symbols = manifest.get("symbols", {})
if symbols.get("name") != symbol_name or symbols.get("sha256") != symbol_sha256:
    raise RuntimeError("symbols manifest archive metadata mismatch")
expected_binary_names = {
    "sdk_open_app",
    "com.czur.device-helper",
    "libsdk_private_providers.dylib",
    "libsdk_private_auth_c_api.dylib",
}
binaries = symbols.get("binaries")
if (
    not isinstance(binaries, list)
    or len(binaries) != len(expected_binary_names)
    or {item.get("name") for item in binaries} != expected_binary_names
):
    raise RuntimeError("symbols manifest binary list mismatch")
uuid_pattern = re.compile(r"^[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}$")
if any(not uuid_pattern.match(item.get("uuid", "")) for item in binaries):
    raise RuntimeError("symbols manifest contains an invalid binary UUID")
PY

    cat > "$output_dir/jenkins-sdk-open-macos-build-manifest.txt" <<EOF
project=sdk_open
version=$version
build=$build
stage=${stage:-}
arch=$arch
build_type=$build_type
git_commit=$git_commit
sdk_open_commit=$sdk_open_commit
use_parent_build_metadata=$use_parent_build_metadata
parent_build_commit=${parent_build_commit:-}
parent_job_name=${parent_job_name:-}
parent_build_number=${parent_build_number:-}
oss_target_dir=${oss_target_dir:-}
package_name=$expected_pkg
symbol_archive_name=$expected_symbols
symbols_manifest_name=$expected_symbols_manifest
package_sha256=$package_sha256
symbol_sha256=$symbol_sha256
symbols_manifest_sha256=$symbols_manifest_sha256
skip_web_build=$skip_web_build
skip_build=$skip_build
skip_notary=$skip_notary
skip_sign=$skip_sign
skip_pkg_sign=$skip_pkg_sign
clean=$clean
branch_name=${BRANCH_NAME:-}
jenkins_job=${JOB_NAME:-}
jenkins_build_number=${BUILD_NUMBER:-}
created_at=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
EOF

    echo "--->> SDK Open macOS package build finished"
    find "$output_dir" -maxdepth 1 -type f -print | sort
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
