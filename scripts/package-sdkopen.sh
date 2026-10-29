#!/bin/bash

# CapEdit Debian Package Builder
# Supports multiple architectures and Linux distributions

set -e

# Set PACKAGE_SDKOPEN_TEST_SOURCE_ONLY=1 to source helper functions without running main.

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/sdk-paths.sh"
PROJECT_ROOT="${SDK_OPEN_HOST_SOURCE_ROOT}"
OUTPUT_DIR="$PROJECT_ROOT/output"
PACKAGING_DIR="$SDK_OPEN_SOURCE_ROOT/packaging/debian"
BUILD_DIR="$PROJECT_ROOT/build_package"
SDK_WEB_ADMIN_DIR="$SDK_OPEN_SOURCE_ROOT/frontend/admin-site"
SDK_WEB_DEMO_DIR="$SDK_OPEN_SOURCE_ROOT/frontend/demo-site"
SDK_WEB_ADMIN_DIST_DIR="$SDK_WEB_ADMIN_DIR/dist"
SDK_WEB_DEMO_DIST_DIR="$SDK_WEB_DEMO_DIR/dist"
SDK_WEB_ASSETS_DIR="$SDK_OPEN_SOURCE_ROOT/web"
SDK_WEB_ADMIN_ASSETS_DIR="$SDK_WEB_ASSETS_DIR/admin"
SDK_WEB_DEMO_ASSETS_DIR="$SDK_WEB_ASSETS_DIR/demo"
SDK_TLS_RUNTIME_TEMPLATE="$SDK_OPEN_SOURCE_ROOT/packaging/tls/templates/sdk-open-runtime.env"

# Default values
ARCH="x86-64"
DISTRO="ubuntu"
BUILD_TYPE="Release"
PACKAGE_NAME="sdk-open"
STAGE="Release"
SDK_WEB_IMAGE="node:20-bookworm"
SDK_WEB_PLATFORM=""
SKIP_SDK_WEB_BUILD=false
SKIP_BUILD_IMAGE=false
DRY_RUN=false
CMAKE_EXTRA_ARGS=(
    "-DBUILD_SDK_OPEN=ON"
    "-DBUILD_SDK_WEB=OFF"
    "-DSDK_OPEN_USE_PREBUILT_WEB=ON"
    "-DSDK_OPEN_ENABLE_HTTP_SERVER=ON"
    "-DBUILD_SDK_PRIVATE=ON"
    "-DSDK_OPEN_USE_PRIVATE_PROVIDER=ON"
)

# Read version information from the SDK Open version source file.
read_config_field() {
    local file="$1"
    local key="$2"

    grep -E "^[[:space:]]*${key}[[:space:]]*=" "$file" 2>/dev/null \
        | tail -n 1 \
        | cut -d'=' -f2- \
        | sed 's/^ *//;s/ *$//' || true
}

read_version_info() {
    local version_file="$SDK_OPEN_SOURCE_ROOT/version/version.ini"
    if [ ! -f "$version_file" ]; then
        print_error "Version file not found: $version_file"
    fi

    # Read version and build from file
    local file_version=$(read_config_field "$version_file" "version" | tr -d ' ')
    local file_build=$(read_config_field "$version_file" "build" | tr -d ' ')
    local file_stage=$(read_config_field "$version_file" "stage")

    if [ -z "$file_version" ] || [ -z "$file_build" ]; then
        print_error "Invalid version format in $version_file"
    fi

    if [ -z "$file_stage" ]; then
        file_stage="$STAGE"
    fi

    VERSION="${file_version}.${file_build}"
    VERSION_ONLY="$file_version"
    BUILD_NUMBER="$file_build"
    STAGE="$file_stage"
    VERSION_WITH_STAGE="${VERSION}.${STAGE}"

    print_info "Read version info: $VERSION (version: $VERSION_ONLY, build: $BUILD_NUMBER, stage: $STAGE)"
}

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

# Usage function
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Build Debian package for CZURScanner

OPTIONS:
    --arch ARCH         Target architecture (x86-64, arm64, loongarch64) [default: x86-64]
    --distro DISTRO     Target distribution (ubuntu, kylin, uos, sm, common) [default: ubuntu]
    --build-type TYPE   Build type (Release, Debug) [default: Release]
    --cmake-arg ARG     Extra CMake argument passed through to build.sh (repeatable)
    --sdk-web-image IMG  Docker image used to prebuild SDK Open web [default: node:20-bookworm]
    --sdk-web-platform PLATFORM
                         Docker platform used to prebuild SDK Open web [default: host platform]
    --skip-sdk-web-build Skip SDK Open web Docker build and use existing src/sdk_open/web assets
    --skip-build-image  Pass --skip-build-image to build.sh for the C++ build
    --skip-build        Skip build step, only package existing files
    --dry-run           Print commands without executing build/package steps
    --clean             Clean build directory only (no rebuild)
    --help              Show this help message

VERSION INFORMATION:
    SDK Open packaging reads src/sdk_open/version/version.ini.
    Format: src/sdk_open/version/version.ini contains version=X.Y.Z, build=N, stage=StageName
    Final package version will be: X.Y.Z.N and filenames append ".Stage"

EXAMPLES:
    $0                                              # Build x86-64 Ubuntu package
    $0 --arch arm64 --distro kylin                  # Build ARM64 Kylin package
    $0 --arch x86-64 --distro uos                   # Build x86-64 UOS package
    $0 --skip-build --arch x86-64 --distro ubuntu   # Skip build, only package existing files
    $0 --arch arm64 --distro uos --sdk-web-image node:20-bookworm
    $0 --arch x86-64 --distro common --sdk-web-platform linux/amd64
    $0 --clean                                      # Clean build directory only

SUPPORTED DISTRIBUTIONS:
    ubuntu - Ubuntu and Ubuntu-based distributions (standard paths)
    kylin  - Kylin OS (direct /opt/apps/ + ICO icons)
    uos    - UnionTech OS (entries/files structure)
    sm     - Secure/General Linux Systems (standard paths)
    common - Generic Debian package (fallback)

EOF
}

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --arch)
                ARCH="$2"
                shift 2
                ;;
            --distro)
                DISTRO="$2"
                shift 2
                ;;
            --build-type)
                BUILD_TYPE="$2"
                shift 2
                ;;
            --cmake-arg)
                CMAKE_EXTRA_ARGS+=("$2")
                shift 2
                ;;
            --sdk-web-image)
                SDK_WEB_IMAGE="$2"
                shift 2
                ;;
            --sdk-web-platform)
                SDK_WEB_PLATFORM="$2"
                shift 2
                ;;
            --skip-sdk-web-build)
                SKIP_SDK_WEB_BUILD=true
                shift
                ;;
            --skip-build-image)
                SKIP_BUILD_IMAGE=true
                shift
                ;;
            --skip-build)
                SKIP_BUILD=true
                shift
                ;;
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --clean)
                CLEAN_BUILD=true
                CLEAN_ONLY=true
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            --)
                shift
                while [[ $# -gt 0 ]]; do
                    CMAKE_EXTRA_ARGS+=("$1")
                    shift
                done
                break
                ;;
            *)
                print_error "Unknown option: $1"
                ;;
        esac
    done
}

# Docker SDK web assets are static files, so build them on the host Docker
# platform instead of the target package architecture. This avoids running a
# cached arm64 Node image on an amd64 host (or the reverse).
detect_host_docker_platform() {
    case "$(uname -m)" in
        x86_64)
            echo "linux/amd64"
            ;;
        aarch64|arm64)
            echo "linux/arm64"
            ;;
        loongarch64)
            echo "linux/loong64"
            ;;
        *)
            print_error "Unsupported host architecture for SDK Web Docker platform: $(uname -m)"
            ;;
    esac
}

# Validate arguments
validate_args() {
    # Validate architecture
    case "$ARCH" in
        x86-64|arm64|loongarch64)
            ;;
        *)
            print_error "Unsupported architecture: $ARCH"
            ;;
    esac

    # Validate distribution
    case "$DISTRO" in
        ubuntu|kylin|uos|sm|common)
            ;;
        *)
            print_error "Unsupported distribution: $DISTRO"
            ;;
    esac

    # Validate build type
    case "$BUILD_TYPE" in
        Release|Debug)
            ;;
        *)
            print_error "Unsupported build type: $BUILD_TYPE"
            ;;
    esac

    if [ -z "$SDK_WEB_IMAGE" ]; then
        print_error "SDK Web Docker image must not be empty"
    fi

    if [ -z "$SDK_WEB_PLATFORM" ]; then
        SDK_WEB_PLATFORM="$(detect_host_docker_platform)"
    fi
}

# Convert architecture for dpkg
get_dpkg_arch() {
    case "$ARCH" in
        x86-64)
            echo "amd64"
            ;;
        arm64)
            echo "arm64"
            ;;
        loongarch64)
            echo "loongarch64"
            ;;
        *)
            print_error "Unknown architecture mapping for: $ARCH"
            ;;
    esac
}


run_cmd() {
    if [ "$DRY_RUN" = true ]; then
        printf '[DRY-RUN]'
        printf ' %q' "$@"
        printf '\n'
    else
        "$@"
    fi
}


ensure_sdk_web_source_exists() {
    if [ ! -f "$SDK_WEB_ADMIN_DIR/package.json" ]; then
        print_error "SDK Open admin-site package.json not found: $SDK_WEB_ADMIN_DIR/package.json"
    fi
    if [ ! -f "$SDK_WEB_DEMO_DIR/package.json" ]; then
        print_error "SDK Open demo-site package.json not found: $SDK_WEB_DEMO_DIR/package.json"
    fi
}

ensure_sdk_web_assets_exist() {
    local admin_index="$SDK_WEB_ADMIN_ASSETS_DIR/index.html"
    local demo_index="$SDK_WEB_DEMO_ASSETS_DIR/index.html"

    if [ ! -f "$admin_index" ]; then
        print_error "SDK Open admin web asset missing: $admin_index"
    fi
    if [ ! -f "$demo_index" ]; then
        print_error "SDK Open demo web asset missing: $demo_index"
    fi
}

copy_sdk_web_dist_to_assets() {
    if [ "$DRY_RUN" = true ]; then
        print_info "[DRY-RUN] would refresh SDK Open web assets from frontend dist directories"
        return
    fi

    if [ ! -f "$SDK_WEB_ADMIN_DIST_DIR/index.html" ]; then
        print_error "SDK Open admin dist missing after build: $SDK_WEB_ADMIN_DIST_DIR/index.html"
    fi
    if [ ! -f "$SDK_WEB_DEMO_DIST_DIR/index.html" ]; then
        print_error "SDK Open demo dist missing after build: $SDK_WEB_DEMO_DIST_DIR/index.html"
    fi

    rm -rf "$SDK_WEB_ADMIN_ASSETS_DIR" "$SDK_WEB_DEMO_ASSETS_DIR"
    mkdir -p "$SDK_WEB_ADMIN_ASSETS_DIR" "$SDK_WEB_DEMO_ASSETS_DIR"
    cp -a "$SDK_WEB_ADMIN_DIST_DIR"/. "$SDK_WEB_ADMIN_ASSETS_DIR"/
    cp -a "$SDK_WEB_DEMO_DIST_DIR"/. "$SDK_WEB_DEMO_ASSETS_DIR"/
}

build_sdk_web_assets() {
    if [ "$SKIP_BUILD" = true ]; then
        print_info "Skipping SDK Web Docker build because --skip-build was specified"
        ensure_sdk_web_assets_exist
        return
    fi

    ensure_sdk_web_source_exists

    if [ "$SKIP_SDK_WEB_BUILD" = true ]; then
        print_info "Skipping SDK Web Docker build, using existing src/sdk_open/web assets"
        ensure_sdk_web_assets_exist
        return
    fi

    print_info "Building SDK Open web assets with Docker"
    print_info "SDK Web Docker image: $SDK_WEB_IMAGE"
    print_info "SDK Web Docker platform: $SDK_WEB_PLATFORM"

    run_cmd docker run --rm \
        --platform "$SDK_WEB_PLATFORM" \
        -v "$PROJECT_ROOT:/workspace" \
        -w /workspace \
        -e CI=true \
        -e COREPACK_HOME=/tmp/corepack \
        -e PNPM_STORE_PATH=/tmp/pnpm-store \
        --user "$(id -u):$(id -g)" \
        "$SDK_WEB_IMAGE" \
        bash -lc "set -euo pipefail
corepack prepare pnpm@10 --activate
corepack pnpm config set store-dir /tmp/pnpm-store
corepack pnpm config set confirmModulesPurge false
corepack pnpm --dir src/sdk_open/frontend/admin-site install --frozen-lockfile
corepack pnpm --dir src/sdk_open/frontend/admin-site build
corepack pnpm --dir src/sdk_open/frontend/demo-site install --frozen-lockfile
corepack pnpm --dir src/sdk_open/frontend/demo-site build"

    if [ "$DRY_RUN" = true ]; then
        print_info "[DRY-RUN] skipping SDK Open web asset validation"
        return
    fi

    copy_sdk_web_dist_to_assets
    ensure_sdk_web_assets_exist
    print_success "SDK Open web assets are ready"
}

# Package filenames need to be renamed for sm distro only
get_deb_filename_package_name() {
    if [ "$DISTRO" = "sm" ]; then
        echo "scanner"
    else
        echo "$PACKAGE_NAME"
    fi
}

# Build the project
build_project() {
    if [ "$SKIP_BUILD" != true ]; then
        print_info "Building CapEdit for architecture: $ARCH"

        # Check if build script exists
        if [ ! -f "$PROJECT_ROOT/build.sh" ]; then
            print_error "Build script not found: $PROJECT_ROOT/build.sh"
        fi

        # Build the project
        cd "$PROJECT_ROOT"
        local build_cmd=(./build.sh --arch "$ARCH" --build-type "$BUILD_TYPE" --skip-web-build --target sdk_open_app)
        if [ "$SKIP_BUILD_IMAGE" = true ]; then
            build_cmd+=(--skip-build-image)
        fi
        if [ ${#CMAKE_EXTRA_ARGS[@]} -gt 0 ]; then
            build_cmd+=(-- "${CMAKE_EXTRA_ARGS[@]}")
        fi
        run_cmd "${build_cmd[@]}"

        # Check if build was successful
        local release_dir="$OUTPUT_DIR/$BUILD_TYPE"
        if [ "$DRY_RUN" != true ] && [ ! -f "$release_dir/sdk_open_app" ]; then
            print_error "Build failed: sdk_open_app executable not found in $release_dir"
        fi

        print_success "Build completed successfully"
    else
        print_info "Skipping build step, using existing files"
        local release_dir="$OUTPUT_DIR/$BUILD_TYPE"
        if [ ! -f "$release_dir/sdk_open_app" ]; then
            print_error "sdk_open_app executable not found in $release_dir. Please run build first."
        fi
        ensure_sdk_web_assets_exist
    fi
}

# Clean build directory
clean_build() {
    if [ "$CLEAN_BUILD" = true ]; then
        print_info "Cleaning build directory"
        rm -rf "$BUILD_DIR"
    fi
}

copy_sdk_open_runtime() {
    local release_dir="$1"
    local install_dir="$2"
    local sdk_open_binary="$release_dir/sdk_open_app"

    if [ ! -f "$sdk_open_binary" ]; then
        print_error "sdk_open_app executable not found in $release_dir"
    fi

    print_info "Copying targeted sdk_open_app build output"
    cp -a "$release_dir"/. "$install_dir/"

    if [ ! -f "$install_dir/ocr_fonts/SourceHanSansCN-Normal.otf" ]; then
        print_error "SDK Open runtime is missing OCR text-layer font: $install_dir/ocr_fonts/SourceHanSansCN-Normal.otf"
    fi
}

# Prepare package structure
prepare_package() {
    print_info "Preparing package structure for $DISTRO"

    local dpkg_arch=$(get_dpkg_arch)
    local package_dir="$BUILD_DIR/${PACKAGE_NAME}_${VERSION_WITH_STAGE}_${dpkg_arch}_${DISTRO}"

    rm -rf "$package_dir"

    # Create package directory structure based on distribution
    if [ "$DISTRO" = "uos" ]; then
        # UOS specific directory structure (both UOS-specific and standard Linux locations)
        mkdir -p "$package_dir"/{DEBIAN,opt/apps/sdk-open/files}
    else
        mkdir -p "$package_dir"/{DEBIAN,opt/apps/sdk-open/}
    fi

    # Copy DEBIAN control files
    cp -r "$PACKAGING_DIR/common/DEBIAN"/* "$package_dir/DEBIAN/"

    # Update version in control file
    sed -i "s/Version: .*/Version: $VERSION/" "$package_dir/DEBIAN/control"
    sed -i "s/Architecture: .*/Architecture: $dpkg_arch/" "$package_dir/DEBIAN/control"

    # Ensure control file ends with newline
    if [ -n "$(tail -c1 "$package_dir/DEBIAN/control")" ]; then
        echo "" >> "$package_dir/DEBIAN/control"
    fi

    # Make scripts executable
    chmod +x "$package_dir/DEBIAN"/{preinst,postinst,postrm,prerm} 2>/dev/null || true

    # build.sh clears output before target builds, so this copies only the
    # sdk_open_app/private-provider output produced by the selected target.
    local release_dir="$OUTPUT_DIR/$BUILD_TYPE"
    local install_dir

    if [ "$DISTRO" = "uos" ]; then
        install_dir="$package_dir/opt/apps/sdk-open/files"
    else
        install_dir="$package_dir/opt/apps/sdk-open"
    fi
    copy_sdk_open_runtime "$release_dir" "$install_dir"

    if [ ! -f "$SDK_TLS_RUNTIME_TEMPLATE" ]; then
        print_error "SDK Open TLS runtime template is missing under src/sdk_open/packaging/tls"
    fi

    # 只携带运行时配置模板。根 CA 私钥和叶证书由签名后的 sdk_open_app
    # 在客户机器生成，DEB 不再携带可跨机器复用的固定私钥。
    mkdir -p "$install_dir/tls/default"
    cp "$SDK_TLS_RUNTIME_TEMPLATE" "$install_dir/tls/default/runtime.env"

    # Copy system configuration files (etc directory) if they exist
    if [ -d "$PACKAGING_DIR/common/etc" ]; then
        print_info "Copying system configuration files (etc)"
        mkdir -p "$package_dir/etc"
        cp -r "$PACKAGING_DIR/common/etc"/* "$package_dir/etc/"
        print_info "Copied udev rules and system configuration files"
    fi

    if [ -d "$PACKAGING_DIR/common/lib/systemd/system/" ]; then
    print_info "Copying system configuration files (service)"
        mkdir -p "$package_dir/lib/systemd/system"
        cp -r "$PACKAGING_DIR/common/lib/systemd/system"/* "$package_dir/lib/systemd/system/"
        print_info "Copied service files"
    fi

    if [ "$DISTRO" = "uos" ]; then
        local old_path="/opt/apps/sdk-open"
        local new_path="/opt/apps/sdk-open/files"
        local path_files=(
            "$package_dir/lib/systemd/system/sdk-open.service"
        )

        for path_file in "${path_files[@]}"; do
            if [ -f "$path_file" ]; then
                sed -i "s|$old_path|$new_path|g" "$path_file"
            fi
        done
    fi

    # Set proper permissions
    find "$package_dir" -type d -exec chmod 755 {} \;
    find "$package_dir" -type f -exec chmod 644 {} \;
    chmod +x "$package_dir/DEBIAN"/{preinst,postinst,postrm,prerm} 2>/dev/null || true

    chmod 755 "$install_dir/tls" "$install_dir/tls/default"

    chmod +x "$install_dir/sdk_open_app"

    echo "$package_dir"
}

# Build Debian package
build_package() {
    local package_dir="$1"
    local dpkg_arch=$(get_dpkg_arch)
    local deb_package_name=$(get_deb_filename_package_name)
    local package_filename="${deb_package_name}_${VERSION_WITH_STAGE}_${dpkg_arch}.deb"
    local output_package="$BUILD_DIR/$package_filename"

    print_info "Building Debian package: $package_filename"

    # Build the package
    cd "$BUILD_DIR"
    fakeroot dpkg-deb -Z xz --build "$(basename "$package_dir")" "$package_filename"

    # Check if package was created successfully
    if [ ! -f "$output_package" ]; then
        print_error "Package build failed: $output_package not found"
    fi

    # Get package size
    local package_size=$(du -h "$output_package" | cut -f1)

    print_success "Package built successfully: $output_package ($package_size)"

    # Show package info
    print_info "Package information:"
    dpkg-deb --info "$output_package"
}

# Main function
main() {
    print_info "CZURScanner Debian Package Builder"
    print_info "=============================="

    # Parse arguments
    parse_args "$@"

    # Read version information from file
    read_version_info

    # Validate arguments
    validate_args

    # Print configuration
    print_info "Configuration:"
    echo "  Architecture: $ARCH"
    echo "  Distribution: $DISTRO"
    echo "  Version: $VERSION (base: $VERSION_ONLY, build: $BUILD_NUMBER)"
    echo "  Stage: $STAGE"
    echo "  Build Type: $BUILD_TYPE"
    echo "  SDK Web Docker image: $SDK_WEB_IMAGE"
    echo "  SDK Web Docker platform: $SDK_WEB_PLATFORM"
    echo "  Skip SDK Web Build: $SKIP_SDK_WEB_BUILD"
    echo "  Skip Build Image: $SKIP_BUILD_IMAGE"
    echo "  Dry Run: $DRY_RUN"
    echo "  Clean Build: ${CLEAN_BUILD:-false}"
    echo "  Clean Only: ${CLEAN_ONLY:-false}"
    echo ""

    # Check dependencies
    if [ "$DRY_RUN" != true ] && ! command -v dpkg-deb >/dev/null 2>&1; then
        print_error "dpkg-deb is required but not installed"
    fi

    if [ "$DRY_RUN" != true ] && ! command -v fakeroot >/dev/null 2>&1; then
        print_error "fakeroot is required but not installed"
    fi

    # Clean if requested
    clean_build

    # If clean only, exit after cleaning
    if [ "$CLEAN_ONLY" = true ]; then
        print_success "Build directory cleaned successfully!"
        exit 0
    fi

    # Build SDK Open web assets before the low-glibc C++ build.
    build_sdk_web_assets

    # Build the project
    build_project

    if [ "$DRY_RUN" = true ]; then
        print_info "Dry run enabled; skipping package directory creation and dpkg-deb build"
        exit 0
    fi

    # Prepare package structure
    local package_dir=$(prepare_package)

    # Build Debian package
    print_info "Starting package build process..."

    build_package "$package_dir"

    # Get the actual package path
    local dpkg_arch=$(get_dpkg_arch)
    local deb_package_name=$(get_deb_filename_package_name)
    local package_filename="${deb_package_name}_${VERSION_WITH_STAGE}_${dpkg_arch}.deb"
    local output_package="$BUILD_DIR/$package_filename"

    # Verify package was created
    if [ ! -f "$output_package" ]; then
        print_error "Package build failed: $output_package not found"
    fi

    print_success "Packaging completed successfully!"
    print_info "Package location: $output_package"

    # Copy to output directory if different
    local final_output="$PROJECT_ROOT/${deb_package_name}_${VERSION_WITH_STAGE}_${dpkg_arch}_${DISTRO}.deb"
    cp "$output_package" "$final_output"
    print_info "Final package: $final_output"

}

# Run main function with all arguments
if [ "${PACKAGE_SDKOPEN_TEST_SOURCE_ONLY:-0}" != "1" ]; then
    main "$@"
fi
