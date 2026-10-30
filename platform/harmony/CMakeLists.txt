cmake_minimum_required(VERSION 3.16)

project(SdkOpenHarmony LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS" AND NOT "${OHOS_PLATFORM}" STREQUAL "OHOS")
    message(FATAL_ERROR "sdk_open_harmony is an OHOS-only build; use the desktop root build for other platforms")
endif()
if(NOT OHOS_ARCH STREQUAL "arm64-v8a")
    message(FATAL_ERROR "sdk_open_harmony currently supports OHOS_ARCH=arm64-v8a only")
endif()

set(LIB_ARCH "ohos-arm64-v8a" CACHE STRING "sdk_open OHOS third-party architecture")
set(THIRD_PARTY_SYSTEM "ohos" CACHE STRING "sdk_open OHOS third-party headers")
set(CZUR_TARGET_OUTPUT_DIR "${CMAKE_BINARY_DIR}/runtime" CACHE PATH "sdk_open runtime output directory")
set(BUILD_SDK_OPEN_TESTS OFF CACHE BOOL "Disable desktop sdk_open tests in the Harmony app build" FORCE)
set(BUILD_SDK_WEB OFF CACHE BOOL "Web assets are staged by the Harmony project" FORCE)
set(SDK_OPEN_USE_PREBUILT_WEB OFF CACHE BOOL "Web assets are staged by the Harmony project" FORCE)
set(SDK_OPEN_ENABLE_TLS OFF CACHE BOOL "TLS is enabled after OHOS crypto packaging is available" FORCE)
set(SDK_OPEN_ENABLE_HTTP_SERVER ON CACHE BOOL "Enable sdk_open HTTP/WS listeners" FORCE)
set(BUILD_SDK_PRIVATE OFF CACHE BOOL "Private desktop provider is not part of the Harmony demo" FORCE)
set(SDK_OPEN_USE_PRIVATE_PROVIDER OFF CACHE BOOL "Use mock providers for the Harmony demo" FORCE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/Dependencies.cmake")
if(NOT EXISTS "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/CMakeLists.txt")
    message(FATAL_ERROR "Harmony real providers require SDK_OPEN_HOST_SOURCE_ROOT")
endif()
set(SDK_OPEN_REPO_ROOT "${SDK_OPEN_HOST_SOURCE_ROOT}")
set(SDK_OPEN_HARMONY_HOST_BUILD ON)
add_subdirectory("${SDK_OPEN_SOURCE_ROOT}" sdk_open_runtime)

set(OS_HARMONYOS ON)
set(BUILD_SDK_OPEN ON)
set(PROJECT_ARCH "arm64")
# Reused module sources use the same symbol to select Harmony-safe headers and
# implementations (for example, Cairo without the unavailable SVG/PDF
# backends).  The CMake variable alone is not visible to the preprocessor.
add_compile_definitions(OS_HARMONYOS=1)
set(CZUR_CZOCR_INCLUDE_DIR "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/include/ohos")
include_directories("${SDK_OPEN_THIRD_PARTY_ROOT}/json")
include_directories("${SDK_OPEN_THIRD_PARTY_ROOT}/openssl3/include/ohos")
include_directories("${SDK_OPEN_THIRD_PARTY_ROOT}/libxml2/include/ohos/libxml2")
include_directories("${SDK_OPEN_THIRD_PARTY_ROOT}/fontconfig/include/ohos")
include_directories("${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/include/ohos")

macro(GROUP_TARGET_SOURCES target root)
    # Source grouping is cosmetic and is not available in the standalone
    # DevEco CMake invocation; keep the module CMakeLists reusable.
endmacro()

# Only non-device modules are brought into the Harmony service.  SANE/TWAIN
# and their desktop helpers are intentionally not part of this target.
add_subdirectory("${SDK_OPEN_REPO_ROOT}/src/module/utils" sdk_harmony_utils)
add_subdirectory("${SDK_OPEN_REPO_ROOT}/src/module/imgengine" sdk_harmony_imgengine)
add_subdirectory("${SDK_OPEN_REPO_ROOT}/src/module/pdf" sdk_harmony_pdf)
add_subdirectory("${SDK_OPEN_REPO_ROOT}/src/module/ofd" sdk_harmony_ofd)
add_subdirectory("${SDK_OPEN_REPO_ROOT}/src/module/docx" sdk_harmony_docx)
set(CZUR_ENABLE_ABBYY_OCR OFF CACHE BOOL "Disable ABBYY in Harmony sdk_open" FORCE)
add_subdirectory("${SDK_OPEN_REPO_ROOT}/src/module/ocr" sdk_harmony_ocr)
add_subdirectory("${SDK_OPEN_REPO_ROOT}/src/module/authorized" sdk_harmony_auth)

# Some reused modules still select Linux ARM64 OpenSSL/ICU paths on Harmony.
# Correct their target properties inside this standalone build only. Encoding
# detection is not used by these providers, so do not link the Linux ICU module.
get_target_property(_sdk_utils_sources utils_lib SOURCES)
list(REMOVE_ITEM _sdk_utils_sources encoding_detector.cpp)
set_property(TARGET utils_lib PROPERTY SOURCES "${_sdk_utils_sources}")
foreach(_sdk_target utils_lib ofd_lib auth_lib auth_lib_private_ossl3)
    if(TARGET ${_sdk_target})
        foreach(_sdk_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES
                INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
            get_target_property(_sdk_values ${_sdk_target} ${_sdk_property})
            if(_sdk_values)
                list(FILTER _sdk_values EXCLUDE REGEX "/unicode/lib/arm64/")
                string(REPLACE "/openssl3/lib/arm64/" "/openssl3/lib/ohos/"
                    _sdk_values "${_sdk_values}")
                string(REPLACE "/openssl3/include/linux" "/openssl3/include/ohos"
                    _sdk_values "${_sdk_values}")
                set_property(TARGET ${_sdk_target} PROPERTY ${_sdk_property} "${_sdk_values}")
            endif()
        endforeach()
    endif()
endforeach()

add_subdirectory("${SDK_OPEN_SOURCE_ROOT}/providers/private" sdk_harmony_providers)
add_library(sdkopen_harmony SHARED
    native/sdk_open_harmony_host.cpp
    native/sdk_open_harmony_napi.cpp
    native/sdk_open_harmony_ocr.cpp
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/authorized/auth_manager.cpp"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/authorized/auth_data_process.cpp"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/authorized/auth_util.cpp"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/authorized/auth_logger.cpp"
)
# Adapt the shared OCR module's initializer only in this Harmony SDK host.
target_link_options(sdkopen_harmony PRIVATE "-Wl,--wrap=czocr_init_in_dev_stage")
target_include_directories(sdkopen_harmony PRIVATE
    "${SDK_OPEN_SOURCE_DIR}/runtime"
    "${SDK_OPEN_SOURCE_DIR}/interfaces"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/auth"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/support"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/devices"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/authorized"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/ocr"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/ofd"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/ofd/include"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/pdf"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/imgengine/src"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/docx/include"
    "${SDK_OPEN_HARMONY_OPENSSL3_INCLUDE_DIR}"
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/app/process/graphic"
    "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/include/ohos"
    "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/include/ohos"
    "${SDK_OPEN_THIRD_PARTY_ROOT}/zxing/include/ohos"
    "${SDK_OPEN_THIRD_PARTY_ROOT}/tiff/include/ohos"
    "${SDK_OPEN_THIRD_PARTY_ROOT}/libexif/include/ohos"
    "${CMAKE_CURRENT_LIST_DIR}/native"
)
target_compile_definitions(sdkopen_harmony PRIVATE
    SDK_OPEN_HARMONY=1
    SDK_OPEN_HARMONY_REAL_PROVIDERS=1
    CPPHTTPLIB_OPENSSL_SUPPORT
    DEVICE_STATIC_LINK
    SDK_PRIVATE_DEVICE_CONFIG_SOURCE_PATH="${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/devices/device_config_compact.conf"
)
target_compile_options(sdkopen_harmony PRIVATE -ffunction-sections -fdata-sections)

get_target_property(_sdk_provider_includes sdkopen_harmony INCLUDE_DIRECTORIES)
get_target_property(_sdk_provider_definitions sdkopen_harmony COMPILE_DEFINITIONS)
target_include_directories(sdk_private_providers PUBLIC ${_sdk_provider_includes})
target_compile_definitions(sdk_private_providers PRIVATE ${_sdk_provider_definitions})
target_compile_options(sdk_private_providers PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(sdk_private_providers PRIVATE
    sdk_open_interfaces sdk_open_logging
    utils_lib imgengine pdf_lib ofd_lib docx ocr_lib auth_lib)
target_link_libraries(sdkopen_harmony PRIVATE sdk_private_providers)

# MuPDF's archives contain a private JPEG implementation and memory manager.
# Keep them local so OpenCV uses the packaged libjpeg ABI instead. docx also
# links these archives and must not export their symbols into the same process.
foreach(_sdk_open_mupdf_consumer sdkopen_harmony docx)
    target_link_options(${_sdk_open_mupdf_consumer} PRIVATE
        "-Wl,--exclude-libs,libmupdf.a:libmupdf-third.a")
endforeach()

set(SDK_OPEN_HARMONY_THIRD_PARTY_DIR "${SDK_OPEN_THIRD_PARTY_ROOT}")
set(SDK_OPEN_HARMONY_OPENCV_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/opencv/lib/ohos-arm64-v8a/libopencv_world.so.4.5.0")
set(SDK_OPEN_HARMONY_CZCV_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/czcv/lib/ohos/libczcv_scan.so")
set(SDK_OPEN_HARMONY_ZXING_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/zxing/lib/ohos-arm64-v8a/libZXingCore.so.1.0.7")
set(SDK_OPEN_HARMONY_TIFF_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/tiff/lib/ohos-arm64-v8a/libtiff.so.6.0.2")
set(SDK_OPEN_HARMONY_EXIF_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/libexif/lib/ohos-arm64-v8a/libexif.a")
set(SDK_OPEN_HARMONY_ZLIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/zlib/lib/ohos-arm64-v8a/libz.so.1.2.13")
set(SDK_OPEN_HARMONY_JPEG_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/libjpeg-turbo/lib/ohos-arm64-v8a/libjpeg.so.62.4.0")
set(SDK_OPEN_HARMONY_PNG_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/libpng/lib/ohos-arm64-v8a/libpng16.so.16.50.0")
set(SDK_OPEN_HARMONY_OPENMP_LIB
    "/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/native/llvm/lib/aarch64-linux-ohos/libomp.so")
set(SDK_OPEN_HARMONY_CXX_LIB
    "/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/native/llvm/lib/aarch64-linux-ohos/libc++_shared.so")
if(DEFINED ENV{DEVECO_SDK_HOME})
    set(_sdk_open_harmony_sdk_root "$ENV{DEVECO_SDK_HOME}/default/openharmony")
    if(EXISTS "${_sdk_open_harmony_sdk_root}/native/llvm/lib/aarch64-linux-ohos/libomp.so")
        set(SDK_OPEN_HARMONY_OPENMP_LIB
            "${_sdk_open_harmony_sdk_root}/native/llvm/lib/aarch64-linux-ohos/libomp.so")
        set(SDK_OPEN_HARMONY_CXX_LIB
            "${_sdk_open_harmony_sdk_root}/native/llvm/lib/aarch64-linux-ohos/libc++_shared.so")
    endif()
endif()

set(SDK_OPEN_HARMONY_OPENSSL3_SSL_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/openssl3/lib/ohos/libssl.so.3")
set(SDK_OPEN_HARMONY_OPENSSL3_CRYPTO_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/openssl3/lib/ohos/libcrypto.so.3")
set(SDK_OPEN_HARMONY_OPENSSL3_INCLUDE_DIR
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/openssl3/include/ohos")
set(SDK_OPEN_HARMONY_CZOCR_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/czocr/lib/ohos/libczcv_ocr_api.so")
set(SDK_OPEN_HARMONY_LIBXML2_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/libxml2/lib/ohos-arm64-v8a/libxml2.so.2.9.13")
set(SDK_OPEN_HARMONY_CAIRO_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/cairo/lib/ohos-arm64-v8a/libcairo.a")
set(SDK_OPEN_HARMONY_FONTCONFIG_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/fontconfig/lib/ohos-arm64-v8a/libfontconfig.a")
set(SDK_OPEN_HARMONY_FREEIMAGE_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/freeimage/lib/ohos-arm64-v8a/libfreeimage-3.18.0.so")
set(SDK_OPEN_HARMONY_FREETYPE_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/freetype/lib/ohos-arm64-v8a/libfreetype.so.2.8.1")
set(SDK_OPEN_HARMONY_HARFBUZZ_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/harfbuzz/lib/ohos-arm64-v8a/libharfbuzz.so")
set(SDK_OPEN_HARMONY_PIXMAN_LIB
    "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/pixman/lib/ohos-arm64-v8a/libpixman-1.a")

foreach(_sdk_open_harmony_dependency
        "${SDK_OPEN_HARMONY_OPENCV_LIB}"
        "${SDK_OPEN_HARMONY_CZCV_LIB}"
        "${SDK_OPEN_HARMONY_ZXING_LIB}"
        "${SDK_OPEN_HARMONY_TIFF_LIB}"
        "${SDK_OPEN_HARMONY_EXIF_LIB}"
        "${SDK_OPEN_HARMONY_ZLIB}"
        "${SDK_OPEN_HARMONY_JPEG_LIB}"
        "${SDK_OPEN_HARMONY_PNG_LIB}"
        "${SDK_OPEN_HARMONY_OPENMP_LIB}"
        "${SDK_OPEN_HARMONY_CXX_LIB}")
    if(NOT EXISTS "${_sdk_open_harmony_dependency}")
        message(FATAL_ERROR "sdk_open Harmony provider dependency is missing: ${_sdk_open_harmony_dependency}")
    endif()
endforeach()

foreach(_sdk_open_harmony_real_dependency
        "${SDK_OPEN_HARMONY_OPENSSL3_SSL_LIB}"
        "${SDK_OPEN_HARMONY_OPENSSL3_CRYPTO_LIB}"
        "${SDK_OPEN_HARMONY_CZOCR_LIB}"
        "${SDK_OPEN_HARMONY_LIBXML2_LIB}"
        "${SDK_OPEN_HARMONY_CAIRO_LIB}"
        "${SDK_OPEN_HARMONY_FONTCONFIG_LIB}"
        "${SDK_OPEN_HARMONY_FREEIMAGE_LIB}"
        "${SDK_OPEN_HARMONY_FREETYPE_LIB}"
        "${SDK_OPEN_HARMONY_HARFBUZZ_LIB}"
        "${SDK_OPEN_HARMONY_PIXMAN_LIB}")
    if(NOT EXISTS "${_sdk_open_harmony_real_dependency}")
        message(FATAL_ERROR "sdk_open Harmony real-provider dependency is missing: ${_sdk_open_harmony_real_dependency}")
    endif()
endforeach()

target_link_libraries(sdkopen_harmony PRIVATE
    sdk_open_runtime
    sdk_mock_providers
    utils_lib
    imgengine
    pdf_lib
    ofd_lib
    docx
    ocr_lib
    auth_lib
    "${SDK_OPEN_HARMONY_OPENCV_LIB}"
    "${SDK_OPEN_HARMONY_CZCV_LIB}"
    "${SDK_OPEN_HARMONY_ZXING_LIB}"
    "${SDK_OPEN_HARMONY_TIFF_LIB}"
    "${SDK_OPEN_HARMONY_EXIF_LIB}"
    "${SDK_OPEN_HARMONY_OPENSSL3_SSL_LIB}"
    "${SDK_OPEN_HARMONY_OPENSSL3_CRYPTO_LIB}"
    "${SDK_OPEN_HARMONY_CZOCR_LIB}"
    "${SDK_OPEN_HARMONY_LIBXML2_LIB}"
    "${SDK_OPEN_HARMONY_CAIRO_LIB}"
    "${SDK_OPEN_HARMONY_FONTCONFIG_LIB}"
    "${SDK_OPEN_HARMONY_FREEIMAGE_LIB}"
    "${SDK_OPEN_HARMONY_FREETYPE_LIB}"
    "${SDK_OPEN_HARMONY_HARFBUZZ_LIB}"
    "${SDK_OPEN_HARMONY_PIXMAN_LIB}"
    libace_napi.z.so
    libhilog_ndk.z.so
)
set_target_properties(sdkopen_harmony PROPERTIES
    OUTPUT_NAME "sdkopen_harmony"
    POSITION_INDEPENDENT_CODE ON
)
target_link_options(sdkopen_harmony PRIVATE -Wl,--gc-sections)

add_custom_command(TARGET sdkopen_harmony POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_ZLIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libz.so"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_ZLIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libz.so.1"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_JPEG_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libjpeg.so.62"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_PNG_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libpng16.so.16"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_OPENCV_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libopencv_world.so.4.5"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_CZCV_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libczcv_scan.so"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_ZXING_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libZXingCore.so.1"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_TIFF_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libtiff.so.6"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_OPENMP_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libomp.so"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_CXX_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libc++_shared.so"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_OPENSSL3_SSL_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libssl.so.3"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_OPENSSL3_CRYPTO_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libcrypto.so.3"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_CZOCR_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libczcv_ocr_api.so"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_LIBXML2_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libxml2.so.2"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_FREEIMAGE_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libfreeimage.so.3"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_FREETYPE_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libfreetype.so.2.8"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/libzip/lib/ohos-arm64-v8a/libzip.so.5.5"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libzip.so.5"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SDK_OPEN_HARMONY_HARFBUZZ_LIB}"
        "$<TARGET_FILE_DIR:sdkopen_harmony>/libharfbuzz.so"
    COMMAND "${CMAKE_COMMAND}"
        "-DMODEL_SOURCE_DIR=${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/czocr/models/windows64"
        "-DOCR_RUNTIME_LIB=${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/czocr/lib/ohos/libczcv_ocr.so"
        "-DOCR_ZLIB_LIB=${SDK_OPEN_HARMONY_ZLIB}"
        "-DMODEL_OUTPUT_DIR=$<TARGET_FILE_DIR:sdkopen_harmony>"
        "-DNATIVE_ONLY=ON"
        -P "${SDK_OPEN_REPO_ROOT}/src/sdk_open/cmake/CopyHarmonyOcrModels.cmake"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdkopen_harmony>/models"
    COMMENT "Copy SDK Open Harmony provider runtime libraries"
)

add_custom_target(sdk_open_harmony_core DEPENDS sdkopen_harmony)

# Model data is staged from rawfile; only libraries belong in HAP libs/.
# Refresh native libraries even when the target does not need relinking.
add_custom_target(copy_sdk_open_harmony_ocr_models ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DMODEL_SOURCE_DIR=${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/czocr/models/windows64"
        "-DOCR_RUNTIME_LIB=${SDK_OPEN_HARMONY_THIRD_PARTY_DIR}/czocr/lib/ohos/libczcv_ocr.so"
        "-DOCR_ZLIB_LIB=${SDK_OPEN_HARMONY_ZLIB}"
        "-DMODEL_OUTPUT_DIR=$<TARGET_FILE_DIR:sdkopen_harmony>"
        "-DNATIVE_ONLY=ON"
        -P "${SDK_OPEN_REPO_ROOT}/src/sdk_open/cmake/CopyHarmonyOcrModels.cmake"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdkopen_harmony>/models"
    VERBATIM
)
add_dependencies(copy_sdk_open_harmony_ocr_models sdkopen_harmony)
add_dependencies(sdk_open_harmony_core copy_sdk_open_harmony_ocr_models)
