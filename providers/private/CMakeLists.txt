if(NOT EXISTS "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/CMakeLists.txt")
    message(FATAL_ERROR "Real providers require SDK_OPEN_HOST_SOURCE_ROOT pointing to CapEditor")
endif()
if(NOT IS_DIRECTORY "${SDK_OPEN_THIRD_PARTY_ROOT}")
    message(FATAL_ERROR "Real providers require SDK_OPEN_THIRD_PARTY_ROOT containing prebuilt platform libraries")
endif()

include("${SDK_OPEN_SOURCE_ROOT}/cmake/PrivateProviderSources.cmake")
if(SDK_OPEN_HARMONY_HOST_BUILD)
    set(_sdk_harmony_provider_sources ${SDK_PRIVATE_PROVIDER_SRCS} ${SDK_PRIVATE_AUTH_CORE_SRCS})
    list(FILTER _sdk_harmony_provider_sources EXCLUDE REGEX "/(private_sane_provider|private_storage_cleanup|private_sdk_logger)\\.cpp$")
    add_library(sdk_private_providers STATIC ${_sdk_harmony_provider_sources})
    set_target_properties(sdk_private_providers PROPERTIES POSITION_INDEPENDENT_CODE ON)
    return()
endif()

set(SDK_PRIVATE_OPENSSL3_ROOT "${SDK_OPEN_THIRD_PARTY_ROOT}/openssl3")
set(SDK_PRIVATE_OPENSSL3_INCLUDE_DIR "${SDK_PRIVATE_OPENSSL3_ROOT}/include")
if(EXISTS "${SDK_PRIVATE_OPENSSL3_ROOT}/include/${THIRD_PARTY_SYSTEM}/openssl/ssl.h")
    set(SDK_PRIVATE_OPENSSL3_INCLUDE_DIR "${SDK_PRIVATE_OPENSSL3_ROOT}/include/${THIRD_PARTY_SYSTEM}")
endif()
set(SDK_PRIVATE_REQUIRES_OPENSSL3 OFF)
if(SDK_PRIVATE_USE_OPENSSL3 OR SDK_OPEN_USE_PRIVATE_PROVIDER)
    set(SDK_PRIVATE_REQUIRES_OPENSSL3 ON)
    include_directories(BEFORE "${SDK_PRIVATE_OPENSSL3_INCLUDE_DIR}")
endif()
set(SDK_PRIVATE_OPENSSL11_ROOT "${SDK_OPEN_THIRD_PARTY_ROOT}/openssl")
set(SDK_PRIVATE_OPENSSL11_INCLUDE_DIR "${SDK_PRIVATE_OPENSSL11_ROOT}/include")
set(SDK_PRIVATE_OPENSSL11_SSL_LIB "${SDK_PRIVATE_OPENSSL11_ROOT}/lib/${LIB_ARCH}/libssl.lib")
set(SDK_PRIVATE_OPENSSL3_SSL_LIB "${SDK_PRIVATE_OPENSSL3_ROOT}/lib/${LIB_ARCH}/libssl.lib")
set(SDK_PRIVATE_OPENCV_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH}/libopencv_world.so.4.5.0")
set(SDK_PRIVATE_TIFF_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/tiff/lib/${LIB_ARCH}/libtiff.so.6.0.2")
if(UNIX AND NOT APPLE)
    set(SDK_PRIVATE_CZCV_LIB_DIR "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/lib/${LIB_ARCH}")
    file(GLOB SDK_PRIVATE_CZCV_VERSIONED_LIBS
        "${SDK_PRIVATE_CZCV_LIB_DIR}/libczcv_scan.so.*")
    list(FILTER SDK_PRIVATE_CZCV_VERSIONED_LIBS INCLUDE REGEX "\\.so\\.[0-9]+\\.[0-9]+")
    if(SDK_PRIVATE_CZCV_VERSIONED_LIBS)
        list(SORT SDK_PRIVATE_CZCV_VERSIONED_LIBS)
        list(GET SDK_PRIVATE_CZCV_VERSIONED_LIBS -1 SDK_PRIVATE_CZCV_RUNTIME_LIB)
        get_filename_component(SDK_PRIVATE_CZCV_RUNTIME_NAME "${SDK_PRIVATE_CZCV_RUNTIME_LIB}" NAME)
    elseif(EXISTS "${SDK_PRIVATE_CZCV_LIB_DIR}/libczcv_scan.so")
        set(SDK_PRIVATE_CZCV_RUNTIME_LIB "${SDK_PRIVATE_CZCV_LIB_DIR}/libczcv_scan.so")
        set(SDK_PRIVATE_CZCV_RUNTIME_NAME "libczcv_scan.so")
    else()
        message(FATAL_ERROR "czcv_scan runtime library not found in ${SDK_PRIVATE_CZCV_LIB_DIR}")
    endif()
endif()
set(SDK_PRIVATE_ZBAR_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/zbar/lib/${LIB_ARCH}/libzbar.so.0.2.0")
set(SDK_PRIVATE_ZXING_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/zxing/lib/${LIB_ARCH}/libZXingCore.so.1.0.7")
set(SDK_PRIVATE_OCR_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/lib/${LIB_ARCH}/libczcv_ocr_api.so")
set(SDK_PRIVATE_JPEG_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libjpeg.so.62.4.0")
set(SDK_PRIVATE_TURBOJPEG_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libturbojpeg.so.0.4.0")
set(SDK_PRIVATE_PNG16_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/libpng/lib/${LIB_ARCH}/libpng16.so.16.50.0")
set(SDK_PRIVATE_PNG12_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/libpng/lib/${LIB_ARCH}/libpng12.so.0.59.0")
set(SDK_PRIVATE_FREEIMAGE_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/freeimage/lib/${LIB_ARCH}/libfreeimage-3.18.0.so")
set(SDK_PRIVATE_LIBEXIF_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/libexif/lib/${LIB_ARCH}/libexif.so.12.3.4")
set(SDK_PRIVATE_LIBZIP_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/libzip/lib/${LIB_ARCH}/libzip.so.5.5")
set(SDK_PRIVATE_ICUUC_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/unicode/lib/${LIB_ARCH}/libicuuc.so.55.1")
set(SDK_PRIVATE_ICUDATA_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/unicode/lib/${LIB_ARCH}/libicudata.so.55.1")
set(SDK_PRIVATE_ICUI18N_RUNTIME_LIB "${SDK_OPEN_THIRD_PARTY_ROOT}/unicode/lib/${LIB_ARCH}/libicui18n.so.55.1")
# SDK Open 各架构统一使用 x86-64 模型目录；算法模型文件格式与运行库架构无关。
set(SDK_PRIVATE_CZCV_MODEL_DIR "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/models/x86-64")
set(SDK_PRIVATE_OCR_MODEL_DIR "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/models/${LIB_ARCH}")
if(APPLE)
    # CZCV macOS packages share the architecture-independent model set.
    set(SDK_PRIVATE_CZCV_MODEL_DIR "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/models/x86-64")
endif()
if(WIN32 AND CZUR_SDK_OPEN_ISOLATED_BUILD)
    set(SDK_PRIVATE_CZCV_MODEL_DIR "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/models/x86-64")
endif()
if(WIN32 AND CZUR_SDK_OPEN_ISOLATED_BUILD AND LIB_ARCH STREQUAL "windows32")
    set(SDK_PRIVATE_OCR_MODEL_DIR "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/models/windows64")
endif()
set(SDK_PRIVATE_RUNTIME_OUTPUT_DIR "${CEF_TARGET_OUT_DIR}")

if(WIN32)
    set(SDK_PRIVATE_OPENSSL11_CRYPTO_LIB "${SDK_PRIVATE_OPENSSL11_ROOT}/lib/${LIB_ARCH}/libcrypto.lib")
    set(SDK_PRIVATE_OPENSSL3_CRYPTO_LIB "${SDK_PRIVATE_OPENSSL3_ROOT}/lib/${LIB_ARCH}/libcrypto.lib")
elseif(APPLE)
    set(SDK_PRIVATE_OPENSSL11_SSL_LIB "${SDK_PRIVATE_OPENSSL11_ROOT}/lib/${LIB_ARCH}/libssl.dylib")
    set(SDK_PRIVATE_OPENSSL3_SSL_LIB "${SDK_PRIVATE_OPENSSL3_ROOT}/lib/${LIB_ARCH}/libssl.dylib")
    set(SDK_PRIVATE_OPENSSL11_CRYPTO_LIB "${SDK_PRIVATE_OPENSSL11_ROOT}/lib/${LIB_ARCH}/libcrypto.dylib")
    set(SDK_PRIVATE_OPENSSL3_CRYPTO_LIB "${SDK_PRIVATE_OPENSSL3_ROOT}/lib/${LIB_ARCH}/libcrypto.dylib")
else()
    set(SDK_PRIVATE_OPENSSL11_SSL_LIB "${SDK_PRIVATE_OPENSSL11_ROOT}/lib/${LIB_ARCH}/libssl.so.1.1")
    set(SDK_PRIVATE_OPENSSL3_SSL_LIB "${SDK_PRIVATE_OPENSSL3_ROOT}/lib/${LIB_ARCH}/libssl.so.3")
    set(SDK_PRIVATE_OPENSSL11_CRYPTO_LIB "${SDK_PRIVATE_OPENSSL11_ROOT}/lib/${LIB_ARCH}/libcrypto.so.1.1")
    set(SDK_PRIVATE_OPENSSL3_CRYPTO_LIB "${SDK_PRIVATE_OPENSSL3_ROOT}/lib/${LIB_ARCH}/libcrypto.so.3")
endif()

include("${SDK_OPEN_SOURCE_ROOT}/cmake/PrivateProviderSources.cmake")

# 复用采编王的 CZCV 参数包装，Linux 旧字符串 ABI 仅限该源文件。
if(NOT WIN32 AND NOT APPLE)
    set_source_files_properties("${SDK_OPEN_HOST_SOURCE_ROOT}/src/app/process/graphic/czcv_cfg_compat.cpp"
        PROPERTIES COMPILE_DEFINITIONS "_GLIBCXX_USE_CXX11_ABI=0")
endif()

set(SDK_PRIVATE_AUTH_TOOL_SRCS
    "${SDK_OPEN_SOURCE_ROOT}/tools/auth/sdk_auth_tool.cpp"
)

set(SDK_PRIVATE_C_API_SRCS
    "${SDK_OPEN_SOURCE_ROOT}/bindings/c/czur_sdk_private_c_api.cpp"
)

# sdk_private 会把多个项目静态库链接进共享库，Linux/macOS 下这些目标都必须使用 PIC。
foreach(_sdk_private_pic_target
    auth_lib
    auth_lib_private_ossl3
    devices_lib
    imgengine
    ocr_lib
    ofd_lib
    pdf_lib
    sane_dev
    tiff_lib
    twain_dev
    utils_lib
)
    if(TARGET ${_sdk_private_pic_target})
        set_target_properties(${_sdk_private_pic_target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
        )
    endif()
endforeach()
unset(_sdk_private_pic_target)

function(add_sdk_private_variant target_suffix openssl_include_dir openssl_ssl_lib openssl_crypto_lib auth_lib_target)
    set(core_target "sdk_private_auth_core${target_suffix}")
    set(providers_target "sdk_private_providers${target_suffix}")
    set(tool_target "sdk_auth_tool${target_suffix}")
    set(c_api_target "sdk_private_auth_c_api${target_suffix}")

    if(NOT EXISTS "${openssl_crypto_lib}")
        message(FATAL_ERROR "Missing OpenSSL crypto library for ${core_target}: ${openssl_crypto_lib}")
    endif()
    if(NOT EXISTS "${openssl_ssl_lib}")
        message(FATAL_ERROR "Missing OpenSSL ssl library for ${providers_target}: ${openssl_ssl_lib}")
    endif()
    if(NOT TARGET "${auth_lib_target}")
        message(FATAL_ERROR "Missing auth target for ${core_target}: ${auth_lib_target}")
    endif()

    add_library(${core_target} STATIC ${SDK_PRIVATE_AUTH_CORE_SRCS})
    set_target_properties(${core_target} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
    if(MSVC)
        target_compile_options(${core_target} PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/support/private_sdk_windows_compat.h"
        )
    endif()
    target_include_directories(${core_target} BEFORE PUBLIC
        "${openssl_include_dir}"
    )
    target_include_directories(${core_target} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${SDK_OPEN_SOURCE_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/auth"
        "${CMAKE_CURRENT_SOURCE_DIR}/support"
        "${SDK_OPEN_SOURCE_ROOT}/interfaces"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/authorized"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module"
    )
    target_compile_definitions(${core_target} PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
        DEVICE_STATIC_LINK
        SDK_PRIVATE_DEVICE_CONFIG_SOURCE_PATH=\"${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/devices/device_config_compact.conf\"
    )
    target_link_libraries(${core_target} PUBLIC
        sdk_open_interfaces
        sdk_open_entitlement_policy
        sdk_open_third_party
        ${auth_lib_target}
        devices_lib
        "${openssl_ssl_lib}"
        "${openssl_crypto_lib}"
    )

    add_library(${c_api_target} SHARED ${SDK_PRIVATE_C_API_SRCS})
    if(MSVC)
        target_compile_options(${c_api_target} PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/support/private_sdk_windows_compat.h"
        )
    endif()
    target_include_directories(${c_api_target} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${SDK_OPEN_SOURCE_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/auth"
        "${SDK_OPEN_SOURCE_ROOT}/bindings/c"
        "${CMAKE_CURRENT_SOURCE_DIR}/support"
        "${SDK_OPEN_SOURCE_ROOT}/interfaces"
        "${openssl_include_dir}"
    )
    target_compile_definitions(${c_api_target} PRIVATE
        CZUR_SDK_PRIVATE_C_API_BUILD
    )
    set_target_properties(${c_api_target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}"
    )
    if(NOT WIN32 AND NOT APPLE)
        target_compile_options(${c_api_target} PRIVATE -fvisibility=hidden)
        set_target_properties(${c_api_target} PROPERTIES
            BUILD_RPATH "$ORIGIN;${SDK_PRIVATE_OPENSSL11_ROOT}/lib/${LIB_ARCH};${SDK_PRIVATE_OPENSSL3_ROOT}/lib/${LIB_ARCH}"
        )
    elseif(APPLE)
        set_target_properties(${c_api_target} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            BUILD_RPATH "@loader_path"
            INSTALL_RPATH "@loader_path"
            INSTALL_RPATH_USE_LINK_PATH FALSE
        )
        if("${openssl_ssl_lib}" STREQUAL "${SDK_PRIVATE_OPENSSL3_SSL_LIB}")
            set(_sdk_private_c_api_ssl_name "libssl.3.dylib")
            set(_sdk_private_c_api_crypto_name "libcrypto.3.dylib")
        else()
            set(_sdk_private_c_api_ssl_name "libssl.dylib")
            set(_sdk_private_c_api_crypto_name "libcrypto.dylib")
        endif()
        add_custom_command(TARGET ${c_api_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libturbojpeg.0.4.0.dylib"
                "$<TARGET_FILE_DIR:${c_api_target}>/libturbojpeg.0.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${openssl_ssl_lib}"
                "$<TARGET_FILE_DIR:${c_api_target}>/${_sdk_private_c_api_ssl_name}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${openssl_crypto_lib}"
                "$<TARGET_FILE_DIR:${c_api_target}>/${_sdk_private_c_api_crypto_name}"
            COMMENT "Copy sdk_private auth C API macOS runtime libraries"
            VERBATIM
        )
        unset(_sdk_private_c_api_ssl_name)
        unset(_sdk_private_c_api_crypto_name)
    endif()
    target_link_libraries(${c_api_target} PUBLIC
        ${core_target}
    )
    if(WIN32 AND TARGET sdk_open_app)
        add_dependencies(sdk_open_app ${c_api_target})
        # sdk_open 授权会话通过 LoadLibrary("sdk_private_auth_c_api.dll") 加载，必须复制到 sdk_open_app 同级目录。
        # 这里挂在当前目录创建的 c_api target 上，并复制到统一运行目录，避免形成 CMake target 依赖环。
        add_custom_command(TARGET ${c_api_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${CZUR_TARGET_OUTPUT_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:${c_api_target}>"
                "${CZUR_TARGET_OUTPUT_DIR}/$<TARGET_FILE_NAME:${c_api_target}>"
            COMMENT "Copy private auth C API DLL into sdk_open_app runtime directory"
        )
    endif()
    if(APPLE AND TARGET sdk_open_app)
        # Keep the standalone private C API in the SDK Open runtime payload.
        add_dependencies(sdk_open_app ${c_api_target})
    endif()

    add_library(${providers_target} SHARED ${SDK_PRIVATE_PROVIDER_SRCS})
    if(MSVC)
        target_compile_options(${providers_target} PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/support/private_sdk_windows_compat.h"
        )
    endif()
    target_include_directories(${providers_target} BEFORE PRIVATE
        "${SDK_PRIVATE_OPENSSL3_INCLUDE_DIR}"
    )
    if(OS_HARMONYOS)
        target_include_directories(${providers_target} BEFORE PRIVATE
            "${SDK_OPEN_HOST_SOURCE_ROOT}/src/platform/harmony/stubs"
        )
    endif()
    target_include_directories(${providers_target} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${SDK_OPEN_SOURCE_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/auth"
        "${CMAKE_CURRENT_SOURCE_DIR}/support"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/app/process/graphic"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/app/process/interface"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/app/process/common"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/framework/binder/czbinder/include"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/framework/binder/czservicemanager/include"
        "${SDK_OPEN_SOURCE_ROOT}/interfaces"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/devices"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/ofd"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/ofd/src"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/sane"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/pdf"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/pdf/mupdf"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/ocr"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/imgengine/src"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/docx/include"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/include"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/include"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/json"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/zbar/include"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/zxing/include"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/zxing/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/zxing/include/ZXing"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/include"
        "${CZUR_CZOCR_INCLUDE_DIR}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/freeimage/include"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/freeimage/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/tiff/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/libxml2/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/cairo/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/libzip/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/mupdf/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/fontconfig/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/libexif/include/${THIRD_PARTY_SYSTEM}"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/lcms2mt/include/${THIRD_PARTY_SYSTEM}"
    )
    if(NOT WIN32)
        target_include_directories(${providers_target} PUBLIC
            "/usr/include/freetype2"
            "/usr/include/fontconfig"
        )
    endif()
    target_compile_definitions(${providers_target} PRIVATE
        CZUR_SDK_PRIVATE_PROVIDERS_BUILD
        CPPHTTPLIB_OPENSSL_SUPPORT
        DEVICE_STATIC_LINK
        SDK_PRIVATE_DEVICE_CONFIG_SOURCE_PATH=\"${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/devices/device_config_compact.conf\"
    )
    set_target_properties(${providers_target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}"
    )
    set(_sdk_private_provider_link_flags)
    if(MSVC)
        list(APPEND _sdk_private_provider_link_flags
            /NODEFAULTLIB:MSVCRT
            /NODEFAULTLIB:MSVCRTD
        )
    endif()
    if(NOT WIN32 AND NOT APPLE)
        target_compile_options(${providers_target} PRIVATE -fvisibility=hidden)
        list(APPEND _sdk_private_provider_link_flags
            -Wl,--exclude-libs,ALL
            -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/sdk_private_providers.exports
            -Wl,--no-as-needed
        )
        set_target_properties(${providers_target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            C_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            BUILD_RPATH "$ORIGIN"
        )
    elseif(APPLE)
        set_target_properties(${providers_target} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            BUILD_RPATH "@loader_path"
            INSTALL_RPATH "@loader_path"
            INSTALL_RPATH_USE_LINK_PATH FALSE
        )
    endif()
    if(_sdk_private_provider_link_flags)
        string(REPLACE ";" " " _sdk_private_provider_link_flags "${_sdk_private_provider_link_flags}")
        set_target_properties(${providers_target} PROPERTIES
            LINK_FLAGS "${_sdk_private_provider_link_flags}"
        )
    endif()
    unset(_sdk_private_provider_link_flags)
    target_link_libraries(${providers_target} PRIVATE
        sdk_open_interfaces
        sdk_open_logging
        ${core_target}
        devices_lib
        sane_dev
        imgengine
        pdf_lib
        ofd_lib
        ocr_lib
        "${SDK_PRIVATE_OPENSSL3_SSL_LIB}"
        "${SDK_PRIVATE_OPENSSL3_CRYPTO_LIB}"
    )

    if(WIN32)
        czur_win_third_party_lib("czcv" "czcv_scan.lib" "czcv_scan.lib" SDK_PRIVATE_CZCV_SCAN_LIB)
        czur_win_third_party_lib("czocr" "czcv_ocr_api.lib" "czcv_ocr_api.lib" SDK_PRIVATE_CZOCR_LIB)
        czur_win_third_party_lib("opencv" "opencv_world450.lib" "opencv_world450d.lib" SDK_PRIVATE_OPENCV_LIB)
        czur_win_third_party_lib("zxing" "ZXingCoreStatic.lib" "ZXingCoreStatic.lib" SDK_PRIVATE_ZXING_LIB)
        czur_win_third_party_lib("libjpeg-turbo" "jpeg.lib" "jpeg.lib" SDK_PRIVATE_JPEG_LIB)
        czur_win_third_party_lib("libpng" "libpng16.lib" "libpng16d.lib" SDK_PRIVATE_PNG_LIB)
        czur_win_third_party_lib("tiff" "tiff.lib" "tiffd.lib" SDK_PRIVATE_TIFF_LIB)
        czur_win_third_party_lib("freeimage" "FreeImage.lib" "FreeImaged.lib" SDK_PRIVATE_FREEIMAGE_LIB)
        czur_win_third_party_lib("libexif" "libexif.lib" "libexif.lib" SDK_PRIVATE_LIBEXIF_LIB)
        target_link_libraries(${providers_target} PRIVATE
            twain_dev
            ${SDK_PRIVATE_CZCV_SCAN_LIB}
            ${SDK_PRIVATE_CZOCR_LIB}
            ${SDK_PRIVATE_OPENCV_LIB}
            ${SDK_PRIVATE_ZXING_LIB}
            ${SDK_PRIVATE_JPEG_LIB}
            ${SDK_PRIVATE_PNG_LIB}
            ${SDK_PRIVATE_TIFF_LIB}
            ${SDK_PRIVATE_FREEIMAGE_LIB}
            ${SDK_PRIVATE_LIBEXIF_LIB}
        )
        if(TARGET twain_helper)
            add_dependencies(${providers_target} twain_helper)
            add_custom_command(TARGET ${providers_target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:twain_helper>"
                    "$<TARGET_FILE_DIR:${providers_target}>/$<TARGET_FILE_NAME:twain_helper>"
                COMMENT "Copy TWAIN helper into private provider runtime directory"
            )
            if(TWAIN_HELPER_X86_EXE AND EXISTS "${TWAIN_HELPER_X86_EXE}")
                add_custom_command(TARGET ${providers_target} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${TWAIN_HELPER_X86_EXE}"
                        "$<TARGET_FILE_DIR:${providers_target}>/twain_helper_x86.exe"
                    COMMENT "Copy prebuilt x86 TWAIN helper into private provider runtime directory"
                )
            endif()
            if(LIB_ARCH STREQUAL "windows64" AND TWAIN_HELPER_X64_EXE AND EXISTS "${TWAIN_HELPER_X64_EXE}")
                add_custom_command(TARGET ${providers_target} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${TWAIN_HELPER_X64_EXE}"
                        "$<TARGET_FILE_DIR:${providers_target}>/twain_helper_x64.exe"
                    COMMENT "Copy prebuilt x64 TWAIN helper into private provider runtime directory"
                )
            endif()
        endif()
    elseif(APPLE)
        set(_sdk_private_mac_runtime_inputs
            "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/lib/${LIB_ARCH}/czcv_scan.framework"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/lib/${LIB_ARCH}/libczcv_ocr_api.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/lib/${LIB_ARCH}/libczcv_ocr.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH}/libopencv_world.4.5.0.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/zxing/lib/${LIB_ARCH}/libZXingCore.1.0.7.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libjpeg.62.4.0.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libturbojpeg.0.4.0.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/libpng/lib/${LIB_ARCH}/libpng16.16.50.0.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/tiff/lib/${LIB_ARCH}/libtiff.6.0.2.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/freeimage/lib/${LIB_ARCH}/libfreeimage.3.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/libexif/lib/${LIB_ARCH}/libexif.12.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/cairo/lib/${LIB_ARCH}/libcairo.2.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/fontconfig/lib/${LIB_ARCH}/libfontconfig.1.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/libzip/lib/${LIB_ARCH}/libzip.5.5.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/libxml2/lib/${LIB_ARCH}/libxml2.2.9.13.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/zlib/lib/${LIB_ARCH}/libz.1.2.13.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/freetype/lib/${LIB_ARCH}/libfreetype.2.8.1.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/openssl/lib/${LIB_ARCH}/libssl.1.1.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/openssl/lib/${LIB_ARCH}/libcrypto.1.1.dylib"
            "${SDK_PRIVATE_OPENSSL3_SSL_LIB}"
            "${SDK_PRIVATE_OPENSSL3_CRYPTO_LIB}"
            "${SDK_PRIVATE_CZCV_MODEL_DIR}"
            "${SDK_PRIVATE_OCR_MODEL_DIR}"
        )
        foreach(_sdk_private_mac_runtime_input IN LISTS _sdk_private_mac_runtime_inputs)
            if(NOT EXISTS "${_sdk_private_mac_runtime_input}")
                message(FATAL_ERROR "Missing SDK private macOS runtime input: ${_sdk_private_mac_runtime_input}")
            endif()
        endforeach()
        unset(_sdk_private_mac_runtime_input)
        unset(_sdk_private_mac_runtime_inputs)

        target_link_libraries(${providers_target} PRIVATE
            # czcv_scan.framework also exports cv::* symbols. Keep our OpenCV before
            # czcv_scan so SDK Open image code binds to third_party_repo/opencv.
            "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH}/libopencv_world.4.5.0.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/lib/${LIB_ARCH}/czcv_scan.framework/czcv_scan"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/lib/${LIB_ARCH}/libczcv_ocr_api.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/zxing/lib/${LIB_ARCH}/libZXingCore.1.0.7.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libjpeg.62.4.0.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/libpng/lib/${LIB_ARCH}/libpng16.16.50.0.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/tiff/lib/${LIB_ARCH}/libtiff.6.0.2.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/freeimage/lib/${LIB_ARCH}/libfreeimage.3.dylib"
            "${SDK_OPEN_THIRD_PARTY_ROOT}/libexif/lib/${LIB_ARCH}/libexif.12.dylib"
            pthread
        )
        # Match every copied filename to the dependency's LC_LOAD_DYLIB name.
        # All private dylibs use @loader_path so the directory remains portable
        # after PKG installation instead of retaining repository build paths.
        add_custom_command(TARGET ${providers_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "$<TARGET_FILE_DIR:${providers_target}>/czcv_scan.framework"
            COMMAND /usr/bin/ditto
                "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/lib/${LIB_ARCH}/czcv_scan.framework"
                "$<TARGET_FILE_DIR:${providers_target}>/czcv_scan.framework"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/lib/${LIB_ARCH}/libczcv_ocr_api.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libczcv_ocr_api.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH}/libopencv_world.4.5.0.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libopencv_world.4.5.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/zxing/lib/${LIB_ARCH}/libZXingCore.1.0.7.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libZXingCore.1.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libjpeg.62.4.0.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libjpeg.62.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libturbojpeg.0.4.0.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libturbojpeg.0.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/libpng/lib/${LIB_ARCH}/libpng16.16.50.0.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libpng16.16.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/tiff/lib/${LIB_ARCH}/libtiff.6.0.2.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libtiff.6.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/freeimage/lib/${LIB_ARCH}/libfreeimage.3.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libfreeimage.3.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/libexif/lib/${LIB_ARCH}/libexif.12.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libexif.12.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/cairo/lib/${LIB_ARCH}/libcairo.2.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libcairo.2.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/fontconfig/lib/${LIB_ARCH}/libfontconfig.1.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libfontconfig.1.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/libzip/lib/${LIB_ARCH}/libzip.5.5.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libzip.5.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/libxml2/lib/${LIB_ARCH}/libxml2.2.9.13.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libxml2.2.9.13.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/zlib/lib/${LIB_ARCH}/libz.1.2.13.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libz.1.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/freetype/lib/${LIB_ARCH}/libfreetype.2.8.1.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libfreetype.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/openssl/lib/${LIB_ARCH}/libssl.1.1.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libssl.1.1.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/openssl/lib/${LIB_ARCH}/libssl.1.1.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libssl.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/openssl/lib/${LIB_ARCH}/libcrypto.1.1.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libcrypto.1.1.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/openssl/lib/${LIB_ARCH}/libcrypto.1.1.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/libcrypto.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_PRIVATE_OPENSSL3_SSL_LIB}"
                "$<TARGET_FILE_DIR:${providers_target}>/libssl.3.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_PRIVATE_OPENSSL3_CRYPTO_LIB}"
                "$<TARGET_FILE_DIR:${providers_target}>/libcrypto.3.dylib"
            COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "$<TARGET_FILE_DIR:${providers_target}>/czcv_model"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${SDK_PRIVATE_CZCV_MODEL_DIR}"
                "$<TARGET_FILE_DIR:${providers_target}>/czcv_model"
            COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "$<TARGET_FILE_DIR:${providers_target}>/models"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${SDK_PRIVATE_OCR_MODEL_DIR}"
                "$<TARGET_FILE_DIR:${providers_target}>/models"
            # The model directory also contains an older engine copy. Match the
            # desktop app: always load the upgraded engine from czocr/lib.
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/lib/${LIB_ARCH}/libczcv_ocr.dylib"
                "$<TARGET_FILE_DIR:${providers_target}>/models/libczcv_ocr.dylib"
            COMMENT "Copy sdk_private macOS runtime libraries and models"
            VERBATIM
        )
    elseif(OS_HARMONYOS)
        target_link_libraries(${providers_target} PRIVATE
            "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH}/libopencv_world.so.4.5.0"
            pthread
        )
    else()
        target_link_libraries(${providers_target} PRIVATE
        "${SDK_OPEN_THIRD_PARTY_ROOT}/czcv/lib/${LIB_ARCH}/libczcv_scan.so"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/czocr/lib/${LIB_ARCH}/libczcv_ocr_api.so"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH}/libopencv_world.so"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/zbar/lib/${LIB_ARCH}/libzbar.so"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/zxing/lib/${LIB_ARCH}/libZXingCore.so"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libjpeg.so"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/libpng/lib/${LIB_ARCH}/libpng16.so"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/tiff/lib/${LIB_ARCH}/libtiff.so"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/freeimage/lib/${LIB_ARCH}/libfreeimage.so"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/libexif/lib/${LIB_ARCH}/libexif.so"
        stdc++fs
        pthread
        rt
        )
    endif()

    add_executable(${tool_target} ${SDK_PRIVATE_AUTH_TOOL_SRCS})
    target_include_directories(${tool_target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${SDK_OPEN_SOURCE_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/auth"
        "${CMAKE_CURRENT_SOURCE_DIR}/support"
        "${SDK_OPEN_SOURCE_ROOT}/interfaces"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/authorized"
    )
    target_link_libraries(${tool_target} PRIVATE
        ${core_target}
    )
    if(APPLE)
        set_target_properties(${tool_target} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            BUILD_RPATH "@loader_path"
            INSTALL_RPATH "@loader_path"
            INSTALL_RPATH_USE_LINK_PATH FALSE
        )
        if("${openssl_ssl_lib}" STREQUAL "${SDK_PRIVATE_OPENSSL3_SSL_LIB}")
            set(_sdk_private_tool_ssl_name "libssl.3.dylib")
            set(_sdk_private_tool_crypto_name "libcrypto.3.dylib")
        else()
            set(_sdk_private_tool_ssl_name "libssl.dylib")
            set(_sdk_private_tool_crypto_name "libcrypto.dylib")
        endif()
        add_custom_command(TARGET ${tool_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libturbojpeg.0.4.0.dylib"
                "$<TARGET_FILE_DIR:${tool_target}>/libturbojpeg.0.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${openssl_ssl_lib}"
                "$<TARGET_FILE_DIR:${tool_target}>/${_sdk_private_tool_ssl_name}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${openssl_crypto_lib}"
                "$<TARGET_FILE_DIR:${tool_target}>/${_sdk_private_tool_crypto_name}"
            COMMENT "Copy sdk_auth_tool macOS runtime libraries"
            VERBATIM
        )
        unset(_sdk_private_tool_ssl_name)
        unset(_sdk_private_tool_crypto_name)
    endif()
endfunction()

set(SDK_PRIVATE_DEFAULT_USES_OPENSSL3 OFF)
if(SDK_PRIVATE_REQUIRES_OPENSSL3 AND NOT TARGET auth_lib_private_ossl3)
    message(FATAL_ERROR "SDK private OpenSSL 3 chain requires auth_lib_private_ossl3. Check third_party_repo/openssl3 headers and libraries for ${LIB_ARCH}.")
endif()

if(SDK_PRIVATE_REQUIRES_OPENSSL3)
    set(SDK_PRIVATE_DEFAULT_AUTH_LIB_TARGET auth_lib_private_ossl3)
    set(SDK_PRIVATE_DEFAULT_OPENSSL_INCLUDE_DIR "${SDK_PRIVATE_OPENSSL3_INCLUDE_DIR}")
    set(SDK_PRIVATE_DEFAULT_OPENSSL_SSL_LIB "${SDK_PRIVATE_OPENSSL3_SSL_LIB}")
    set(SDK_PRIVATE_DEFAULT_OPENSSL_CRYPTO_LIB "${SDK_PRIVATE_OPENSSL3_CRYPTO_LIB}")
    set(SDK_PRIVATE_DEFAULT_USES_OPENSSL3 ON)
elseif(TARGET auth_lib_private_ossl3)
    set(SDK_PRIVATE_DEFAULT_AUTH_LIB_TARGET auth_lib_private_ossl3)
    set(SDK_PRIVATE_DEFAULT_OPENSSL_INCLUDE_DIR "${SDK_PRIVATE_OPENSSL3_INCLUDE_DIR}")
    set(SDK_PRIVATE_DEFAULT_OPENSSL_SSL_LIB "${SDK_PRIVATE_OPENSSL3_SSL_LIB}")
    set(SDK_PRIVATE_DEFAULT_OPENSSL_CRYPTO_LIB "${SDK_PRIVATE_OPENSSL3_CRYPTO_LIB}")
    set(SDK_PRIVATE_DEFAULT_USES_OPENSSL3 ON)
else()
    set(SDK_PRIVATE_DEFAULT_AUTH_LIB_TARGET auth_lib)
    set(SDK_PRIVATE_DEFAULT_OPENSSL_INCLUDE_DIR "${SDK_PRIVATE_OPENSSL11_INCLUDE_DIR}")
    set(SDK_PRIVATE_DEFAULT_OPENSSL_SSL_LIB "${SDK_PRIVATE_OPENSSL11_SSL_LIB}")
    set(SDK_PRIVATE_DEFAULT_OPENSSL_CRYPTO_LIB "${SDK_PRIVATE_OPENSSL11_CRYPTO_LIB}")
endif()

add_sdk_private_variant("" "${SDK_PRIVATE_DEFAULT_OPENSSL_INCLUDE_DIR}" "${SDK_PRIVATE_DEFAULT_OPENSSL_SSL_LIB}" "${SDK_PRIVATE_DEFAULT_OPENSSL_CRYPTO_LIB}" "${SDK_PRIVATE_DEFAULT_AUTH_LIB_TARGET}")

add_executable(sdk_private_c_api_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_c_api_smoke_test.cpp"
)
target_include_directories(sdk_private_c_api_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
)
target_link_libraries(sdk_private_c_api_smoke_test PRIVATE
    sdk_private_auth_c_api
)

add_executable(sdk_private_ocr_language_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_ocr_language_smoke_test.cpp"
)
target_include_directories(sdk_private_ocr_language_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
)
target_link_libraries(sdk_private_ocr_language_smoke_test PRIVATE
    sdk_private_providers
)

add_executable(sdk_private_auth_service_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_auth_service_smoke_test.cpp"
)
target_include_directories(sdk_private_auth_service_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)
target_compile_definitions(sdk_private_auth_service_smoke_test PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
)
target_link_libraries(sdk_private_auth_service_smoke_test PRIVATE
    sdk_private_auth_core
)
if(NOT WIN32)
    target_link_libraries(sdk_private_auth_service_smoke_test PRIVATE pthread)
endif()

add_executable(sdk_private_master_secret_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_master_secret_smoke_test.cpp"
)
target_include_directories(sdk_private_master_secret_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)
target_link_libraries(sdk_private_master_secret_smoke_test PRIVATE
    sdk_private_auth_core
)

add_executable(sdk_private_offline_activation_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_offline_activation_smoke_test.cpp"
)
target_include_directories(sdk_private_offline_activation_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)
target_link_libraries(sdk_private_offline_activation_smoke_test PRIVATE
    sdk_private_auth_core
)

add_executable(sdk_private_image_enhance_provider_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_image_enhance_provider_smoke_test.cpp"
)
target_include_directories(sdk_private_image_enhance_provider_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)
target_link_libraries(sdk_private_image_enhance_provider_smoke_test PRIVATE
    sdk_private_providers
)

add_executable(sdk_private_provider_bundle_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_provider_bundle_smoke_test.cpp"
)
target_include_directories(sdk_private_provider_bundle_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)
target_link_libraries(sdk_private_provider_bundle_smoke_test PRIVATE
    sdk_private_providers
)
if(BUILD_SDK_OPEN_TESTS)
    add_test(NAME sdk_private.provider_bundle
             COMMAND sdk_private_provider_bundle_smoke_test)
endif()

if(BUILD_SDK_OPEN_TESTS)
    add_executable(sdk_harmony_device_provider_contract_test
        "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_harmony_device_provider_contract_test.cpp"
    )
    target_include_directories(sdk_harmony_device_provider_contract_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${SDK_OPEN_SOURCE_ROOT}"
        "${SDK_OPEN_SOURCE_ROOT}/interfaces"
        "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/devices"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/include"
        "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/include/${THIRD_PARTY_SYSTEM}"
    )
    target_link_libraries(sdk_harmony_device_provider_contract_test PRIVATE
        devices_lib
        sdk_open_third_party
    )
    if(WIN32)
        czur_win_third_party_lib(
            "opencv" "opencv_world450.lib" "opencv_world450d.lib"
            SDK_HARMONY_DEVICE_PROVIDER_TEST_OPENCV_LIB)
        target_link_libraries(sdk_harmony_device_provider_contract_test PRIVATE
            ${SDK_HARMONY_DEVICE_PROVIDER_TEST_OPENCV_LIB}
        )
    elseif(APPLE)
        target_link_libraries(sdk_harmony_device_provider_contract_test PRIVATE
            "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH}/libopencv_world.4.5.0.dylib"
        )
        set_target_properties(sdk_harmony_device_provider_contract_test PROPERTIES
            BUILD_RPATH "@loader_path;${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH};${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}"
        )
        add_custom_command(TARGET sdk_harmony_device_provider_contract_test POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_THIRD_PARTY_ROOT}/libjpeg-turbo/lib/${LIB_ARCH}/libturbojpeg.0.4.0.dylib"
                "$<TARGET_FILE_DIR:sdk_harmony_device_provider_contract_test>/libturbojpeg.0.dylib"
            VERBATIM
        )
    elseif(UNIX)
        target_link_libraries(sdk_harmony_device_provider_contract_test PRIVATE
            "${SDK_PRIVATE_OPENCV_RUNTIME_LIB}"
        )
    endif()
    add_test(NAME sdk_private.harmony_device_provider_contract
             COMMAND sdk_harmony_device_provider_contract_test)
endif()

add_executable(sdk_private_capture_timeout_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_capture_timeout_smoke_test.cpp"
)
target_include_directories(sdk_private_capture_timeout_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)

add_executable(sdk_private_image_write_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_image_write_smoke_test.cpp"
)
target_include_directories(sdk_private_image_write_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)
target_link_libraries(sdk_private_image_write_smoke_test PRIVATE
    sdk_private_providers
)
if(APPLE)
    # The test calls cv::imread/imwrite/imencode directly. sdk_private_providers
    # uses OpenCV privately, so the executable must link its own direct symbols.
    target_link_libraries(sdk_private_image_write_smoke_test PRIVATE
        "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH}/libopencv_world.4.5.0.dylib"
    )
endif()

add_executable(sdk_private_image_write_benchmark
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_image_write_benchmark.cpp"
)
target_include_directories(sdk_private_image_write_benchmark PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)
target_link_libraries(sdk_private_image_write_benchmark PRIVATE
    sdk_private_providers
)

add_executable(sdk_open_upload_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_open_upload_smoke_test.cpp"
)
target_include_directories(sdk_open_upload_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/application"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
    "${SDK_OPEN_SOURCE_ROOT}/providers/mock"
    "${SDK_OPEN_SOURCE_ROOT}/runtime"
)
target_link_libraries(sdk_open_upload_smoke_test PRIVATE
    sdk_private_providers
    sdk_open_runtime
    sdk_mock_providers
)

add_executable(png_compression_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/png_compression_smoke_test.cpp"
)
target_include_directories(png_compression_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)
target_link_libraries(png_compression_smoke_test PRIVATE
    sdk_private_providers
)

add_executable(sdk_private_sane_provider_smoke_test
    "${SDK_OPEN_SOURCE_ROOT}/test/private/sdk_private_sane_provider_smoke_test.cpp"
)
target_include_directories(sdk_private_sane_provider_smoke_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_OPEN_SOURCE_ROOT}"
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
)
target_link_libraries(sdk_private_sane_provider_smoke_test PRIVATE
    sdk_private_providers
)

if(OS_HARMONYOS)
    add_custom_target(sdk_private_runtime_deps ALL
        COMMAND "${CMAKE_COMMAND}" -E echo "HarmonyOS: sdk_private uses czcv/czocr stubs; skipping czcv/czocr runtime and model copy."
    )
elseif(NOT WIN32 AND NOT APPLE)
    if(NOT EXISTS "${SDK_PRIVATE_OPENCV_RUNTIME_LIB}")
        message(FATAL_ERROR "Missing OpenCV runtime library: ${SDK_PRIVATE_OPENCV_RUNTIME_LIB}")
    endif()
    if(NOT EXISTS "${SDK_PRIVATE_TIFF_RUNTIME_LIB}")
        message(FATAL_ERROR "Missing TIFF runtime library: ${SDK_PRIVATE_TIFF_RUNTIME_LIB}")
    endif()
    foreach(runtime_lib
            SDK_PRIVATE_CZCV_RUNTIME_LIB
            SDK_PRIVATE_ZBAR_RUNTIME_LIB
            SDK_PRIVATE_ZXING_RUNTIME_LIB
            SDK_PRIVATE_OCR_RUNTIME_LIB
            SDK_PRIVATE_JPEG_RUNTIME_LIB
            SDK_PRIVATE_TURBOJPEG_RUNTIME_LIB
            SDK_PRIVATE_PNG16_RUNTIME_LIB
            SDK_PRIVATE_PNG12_RUNTIME_LIB
            SDK_PRIVATE_FREEIMAGE_RUNTIME_LIB
            SDK_PRIVATE_LIBEXIF_RUNTIME_LIB
            SDK_PRIVATE_LIBZIP_RUNTIME_LIB
            SDK_PRIVATE_ICUUC_RUNTIME_LIB
            SDK_PRIVATE_ICUDATA_RUNTIME_LIB
            SDK_PRIVATE_ICUI18N_RUNTIME_LIB)
        if(NOT EXISTS "${${runtime_lib}}")
            message(FATAL_ERROR "Missing SDK private runtime library: ${${runtime_lib}}")
        endif()
    endforeach()

    set(SDK_PRIVATE_OPENSSL3_RUNTIME_COPY_COMMANDS)
    if(EXISTS "${SDK_PRIVATE_OPENSSL3_SSL_LIB}" AND EXISTS "${SDK_PRIVATE_OPENSSL3_CRYPTO_LIB}")
        list(APPEND SDK_PRIVATE_OPENSSL3_RUNTIME_COPY_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_PRIVATE_OPENSSL3_SSL_LIB}"
                "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libssl.so.3"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_PRIVATE_OPENSSL3_CRYPTO_LIB}"
                "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libcrypto.so.3"
            COMMAND "${CMAKE_COMMAND}" -E create_symlink
                "libssl.so.3"
                "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libssl.so"
            COMMAND "${CMAKE_COMMAND}" -E create_symlink
                "libcrypto.so.3"
                "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libcrypto.so"
        )
    endif()

    add_custom_target(sdk_private_runtime_deps ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_CZCV_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/${SDK_PRIVATE_CZCV_RUNTIME_NAME}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_OPENCV_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libopencv_world.so.4.5.0"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_ZBAR_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libzbar.so.0.2.0"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_ZXING_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libZXingCore.so.1.0.7"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_OCR_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libczcv_ocr_api.so"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_JPEG_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libjpeg.so.62.4.0"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_TURBOJPEG_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libturbojpeg.so.0.4.0"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_PNG16_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libpng16.so.16.50.0"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_PNG12_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libpng12.so.0.59.0"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_TIFF_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libtiff.so.6.0.2"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_FREEIMAGE_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libfreeimage-3.18.0.so"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_LIBEXIF_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libexif.so.12.3.4"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_LIBZIP_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libzip.so.5.5"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_ICUUC_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libicuuc.so.55.1"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_ICUDATA_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libicudata.so.55.1"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_PRIVATE_ICUI18N_RUNTIME_LIB}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libicui18n.so.55.1"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "${SDK_PRIVATE_CZCV_RUNTIME_NAME}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libczcv_scan.so.1"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "${SDK_PRIVATE_CZCV_RUNTIME_NAME}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libczcv_scan.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libopencv_world.so.4.5.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libopencv_world.so.4.5"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libopencv_world.so.4.5.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libopencv_world.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libzbar.so.0.2.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libzbar.so.0"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libzbar.so.0.2.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libzbar.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libZXingCore.so.1.0.7"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libZXingCore.so.1"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libZXingCore.so.1.0.7"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libZXingCore.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libczcv_ocr_api.so"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libczcv_ocr_api.so.1"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libjpeg.so.62.4.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libjpeg.so.62"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libjpeg.so.62.4.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libjpeg.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libturbojpeg.so.0.4.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libturbojpeg.so.0"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libturbojpeg.so.0.4.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libturbojpeg.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libpng16.so.16.50.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libpng16.so.16"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libpng16.so.16.50.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libpng16.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libpng12.so.0.59.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libpng12.so.0"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libpng12.so.0.59.0"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libpng12.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libtiff.so.6.0.2"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libtiff.so.6"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libtiff.so.6.0.2"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libtiff.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libfreeimage-3.18.0.so"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libfreeimage.so.3"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libfreeimage-3.18.0.so"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libfreeimage.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libexif.so.12.3.4"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libexif.so.12"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libexif.so.12.3.4"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libexif.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libzip.so.5.5"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libzip.so.5"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libzip.so.5.5"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libzip.so"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libicuuc.so.55.1"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libicuuc.so.55"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libicudata.so.55.1"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libicudata.so.55"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "libicui18n.so.55.1"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/libicui18n.so.55"
        ${SDK_PRIVATE_OPENSSL3_RUNTIME_COPY_COMMANDS}
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${SDK_PRIVATE_CZCV_MODEL_DIR}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/czcv_model"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${SDK_PRIVATE_OCR_MODEL_DIR}"
            "${SDK_PRIVATE_RUNTIME_OUTPUT_DIR}/models"
        COMMENT "Copy sdk_private graphic runtime libraries"
    )
endif()

if(TARGET sdk_private_runtime_deps)
    add_dependencies(sdk_private_providers sdk_private_runtime_deps)
endif()

if(SDK_PRIVATE_USE_OPENSSL3 AND NOT WIN32 AND NOT APPLE)
    set_target_properties(sdk_auth_tool PROPERTIES
        BUILD_RPATH "${SDK_PRIVATE_OPENSSL3_ROOT}/lib/${LIB_ARCH}"
    )
endif()

if(TARGET auth_lib_private_ossl3 AND NOT SDK_PRIVATE_DEFAULT_USES_OPENSSL3)
    add_sdk_private_variant("_ossl3" "${SDK_PRIVATE_OPENSSL3_INCLUDE_DIR}" "${SDK_PRIVATE_OPENSSL3_SSL_LIB}" "${SDK_PRIVATE_OPENSSL3_CRYPTO_LIB}" auth_lib_private_ossl3)
    if(TARGET sdk_private_runtime_deps)
        add_dependencies(sdk_private_providers_ossl3 sdk_private_runtime_deps)
    endif()
    if(NOT WIN32 AND NOT APPLE)
        set_target_properties(sdk_auth_tool_ossl3 PROPERTIES
            BUILD_RPATH "${SDK_PRIVATE_OPENSSL3_ROOT}/lib/${LIB_ARCH}"
        )
    endif()
endif()

# These tests call OpenCV directly; macOS requires a direct link.
if(APPLE)
    foreach(_sdk_opencv_tool
        sdk_private_image_enhance_provider_smoke_test
        sdk_private_image_write_benchmark
        png_compression_smoke_test
    )
        target_link_libraries(${_sdk_opencv_tool} PRIVATE
            "${SDK_OPEN_THIRD_PARTY_ROOT}/opencv/lib/${LIB_ARCH}/libopencv_world.4.5.0.dylib"
        )
    endforeach()
endif()
