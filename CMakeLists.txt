set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(SDK_OPEN_ENABLE_HTTP_SERVER "Start sdk_open embedded admin/demo HTTP servers" ON)
option(SDK_OPEN_USE_PREBUILT_WEB "Use prebuilt sdk_open/web assets without running pnpm" OFF)
option(SDK_OPEN_ENABLE_TLS "Enable HTTPS/WSS listeners for sdk_open transport services" ON)

set(SDK_OPEN_VERSION_FILE_CANDIDATES
    "${CMAKE_SOURCE_DIR}/version/sdk-open/version.ini"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../version/sdk-open/version.ini"
    "${CMAKE_CURRENT_SOURCE_DIR}/version/sdk-open/version.ini"
)
set(SDK_OPEN_VERSION_FILE "")
foreach(_sdk_open_version_file IN LISTS SDK_OPEN_VERSION_FILE_CANDIDATES)
    if(EXISTS "${_sdk_open_version_file}")
        get_filename_component(SDK_OPEN_VERSION_FILE "${_sdk_open_version_file}" ABSOLUTE)
        break()
    endif()
endforeach()

if(NOT EXISTS "${SDK_OPEN_VERSION_FILE}")
    message(FATAL_ERROR "SDK Open版本源文件不存在: version/sdk-open/version.ini")
endif()

file(READ "${SDK_OPEN_VERSION_FILE}" SDK_OPEN_VERSION_CONTENT)

set(SDK_OPEN_VERSION "0.0.0")
set(SDK_OPEN_BUILD "0")
set(SDK_OPEN_STAGE "Unknown")
set(SDK_OPEN_INTERVAL "0")

string(REGEX MATCH "version=([^\r\n]+)" SDK_OPEN_VERSION_MATCH "${SDK_OPEN_VERSION_CONTENT}")
if(SDK_OPEN_VERSION_MATCH)
    set(SDK_OPEN_VERSION "${CMAKE_MATCH_1}")
endif()

string(REGEX MATCH "build=([^\r\n]+)" SDK_OPEN_BUILD_MATCH "${SDK_OPEN_VERSION_CONTENT}")
if(SDK_OPEN_BUILD_MATCH)
    set(SDK_OPEN_BUILD "${CMAKE_MATCH_1}")
endif()

string(REGEX MATCH "stage=([^\r\n]+)" SDK_OPEN_STAGE_MATCH "${SDK_OPEN_VERSION_CONTENT}")
if(SDK_OPEN_STAGE_MATCH)
    set(SDK_OPEN_STAGE "${CMAKE_MATCH_1}")
endif()

string(REGEX MATCH "interval=([^\r\n]+)" SDK_OPEN_INTERVAL_MATCH "${SDK_OPEN_VERSION_CONTENT}")
if(SDK_OPEN_INTERVAL_MATCH)
    set(SDK_OPEN_INTERVAL "${CMAKE_MATCH_1}")
endif()

string(TIMESTAMP SDK_OPEN_BUILD_TIMESTAMP "%Y%m%d%H%M%S" UTC)
string(TIMESTAMP SDK_OPEN_BUILD_DATE "%Y-%m-%d" UTC)
string(TIMESTAMP SDK_OPEN_BUILD_TIME "%H:%M:%S" UTC)

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE SDK_OPEN_GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE SDK_OPEN_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(SDK_OPEN_GIT_COMMIT_HASH "unknown")
    set(SDK_OPEN_GIT_BRANCH "unknown")
endif()

if(NOT SDK_OPEN_GIT_COMMIT_HASH)
    set(SDK_OPEN_GIT_COMMIT_HASH "unknown")
endif()
if(NOT SDK_OPEN_GIT_BRANCH)
    set(SDK_OPEN_GIT_BRANCH "unknown")
endif()

set(SDK_OPEN_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/sdk_open/include")
file(MAKE_DIRECTORY "${SDK_OPEN_GENERATED_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/sdk_open_version.h.in"
    "${SDK_OPEN_GENERATED_INCLUDE_DIR}/sdk_open_version.h"
    @ONLY
)
if(APPLE)
    set(SDK_OPEN_MAC_INFO_PLIST
        "${CMAKE_CURRENT_BINARY_DIR}/generated/sdk-open-Info.plist")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/sdk_open-Info.plist.in"
        "${SDK_OPEN_MAC_INFO_PLIST}"
        @ONLY
    )
endif()

message(STATUS "SDK Open版本信息:")
message(STATUS "  版本: ${SDK_OPEN_VERSION}")
message(STATUS "  构建号: ${SDK_OPEN_BUILD}")
message(STATUS "  阶段: ${SDK_OPEN_STAGE}")
message(STATUS "  Interval: ${SDK_OPEN_INTERVAL}")
message(STATUS "  版本源文件: ${SDK_OPEN_VERSION_FILE}")

set(SDK_OPEN_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(SDK_OPEN_CPP_HTTPLIB_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/cpp-httplib")
set(SDK_OPEN_WEBSOCKETPP_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/websocketpp")
set(SDK_OPEN_ASIO_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/asio/asio/include")
set(SDK_OPEN_NLOHMANN_JSON_DIR "${SDK_OPEN_THIRD_PARTY_DIR}")
set(SDK_OPEN_SPDLOG_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/spdlog")
set(SDK_OPEN_CGRAPH_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/CGraph")
set(SDK_OPEN_OPENSSL3_DIR "${CMAKE_SOURCE_DIR}/third_party_repo/openssl3")

add_library(sdk_open_tls_deps INTERFACE)
if(SDK_OPEN_ENABLE_TLS)
    if(APPLE)
        set(SDK_OPEN_OPENSSL3_INCLUDE_DIR "${SDK_OPEN_OPENSSL3_DIR}/include/mac")
        set(SDK_OPEN_OPENSSL3_SSL_LIB "${SDK_OPEN_OPENSSL3_DIR}/lib/${LIB_ARCH}/libssl.3.dylib")
        set(SDK_OPEN_OPENSSL3_CRYPTO_LIB "${SDK_OPEN_OPENSSL3_DIR}/lib/${LIB_ARCH}/libcrypto.3.dylib")
    elseif(WIN32)
        set(SDK_OPEN_OPENSSL3_INCLUDE_DIR "${SDK_OPEN_OPENSSL3_DIR}/include/windows")
        set(SDK_OPEN_OPENSSL3_SSL_LIB "${SDK_OPEN_OPENSSL3_DIR}/lib/${LIB_ARCH}/libssl.lib")
        set(SDK_OPEN_OPENSSL3_CRYPTO_LIB "${SDK_OPEN_OPENSSL3_DIR}/lib/${LIB_ARCH}/libcrypto.lib")
    elseif(UNIX)
        set(SDK_OPEN_OPENSSL3_INCLUDE_DIR "${SDK_OPEN_OPENSSL3_DIR}/include/linux")
        set(SDK_OPEN_OPENSSL3_SSL_LIB "${SDK_OPEN_OPENSSL3_DIR}/lib/${LIB_ARCH}/libssl.so.3")
        set(SDK_OPEN_OPENSSL3_CRYPTO_LIB "${SDK_OPEN_OPENSSL3_DIR}/lib/${LIB_ARCH}/libcrypto.so.3")
    else()
        message(FATAL_ERROR "SDK_OPEN_ENABLE_TLS=ON has no OpenSSL 3 configuration for this platform")
    endif()

    foreach(_sdk_open_tls_file
            "${SDK_OPEN_OPENSSL3_INCLUDE_DIR}/openssl/ssl.h"
            "${SDK_OPEN_OPENSSL3_SSL_LIB}"
            "${SDK_OPEN_OPENSSL3_CRYPTO_LIB}")
        if(NOT EXISTS "${_sdk_open_tls_file}")
            message(FATAL_ERROR "SDK Open TLS dependency is missing: ${_sdk_open_tls_file}")
        endif()
    endforeach()
    target_include_directories(sdk_open_tls_deps BEFORE INTERFACE "${SDK_OPEN_OPENSSL3_INCLUDE_DIR}")
    target_link_libraries(sdk_open_tls_deps INTERFACE
        "${SDK_OPEN_OPENSSL3_SSL_LIB}"
        "${SDK_OPEN_OPENSSL3_CRYPTO_LIB}"
    )
    target_compile_definitions(sdk_open_tls_deps INTERFACE
        SDK_OPEN_ENABLE_TLS=1
        CPPHTTPLIB_OPENSSL_SUPPORT
    )
else()
    target_compile_definitions(sdk_open_tls_deps INTERFACE SDK_OPEN_ENABLE_TLS=0)
endif()

if(NOT EXISTS "${SDK_OPEN_CPP_HTTPLIB_DIR}/httplib.h")
    message(FATAL_ERROR "Missing third_party/cpp-httplib/httplib.h")
endif()
if(NOT EXISTS "${SDK_OPEN_WEBSOCKETPP_DIR}/websocketpp/server.hpp")
    message(FATAL_ERROR "Missing third_party/websocketpp/websocketpp/server.hpp")
endif()
if(NOT EXISTS "${SDK_OPEN_ASIO_DIR}/asio.hpp")
    message(FATAL_ERROR "Missing third_party/asio/asio/include/asio.hpp")
endif()
if(NOT EXISTS "${SDK_OPEN_NLOHMANN_JSON_DIR}/nlohmann/json.hpp")
    message(FATAL_ERROR "Missing third_party/nlohmann/json.hpp")
endif()
if(NOT EXISTS "${SDK_OPEN_SPDLOG_DIR}/spdlog.h")
    message(FATAL_ERROR "Missing third_party/spdlog/spdlog.h")
endif()
if(NOT EXISTS "${SDK_OPEN_CGRAPH_DIR}/src/CGraph.h")
    message(FATAL_ERROR "Missing third_party/CGraph/src/CGraph.h")
endif()

add_library(sdk_open_third_party INTERFACE)
target_include_directories(sdk_open_third_party BEFORE INTERFACE
    "${SDK_OPEN_THIRD_PARTY_DIR}"
    "${SDK_OPEN_CPP_HTTPLIB_DIR}"
    "${SDK_OPEN_WEBSOCKETPP_DIR}"
    "${SDK_OPEN_ASIO_DIR}"
    "${SDK_OPEN_NLOHMANN_JSON_DIR}"
)
target_compile_definitions(sdk_open_third_party INTERFACE
    ASIO_STANDALONE
    _WEBSOCKETPP_CPP11_STL_
)
if(MSVC)
    target_compile_options(sdk_open_third_party INTERFACE
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/runtime/sdk_windows_compat.h"
    )
endif()

add_library(sdk_open_interfaces INTERFACE)
target_include_directories(sdk_open_interfaces INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/interfaces"
)

add_library(sdk_open_entitlement_policy STATIC
    interfaces/sdk_entitlement_policy.cpp
)
target_link_libraries(sdk_open_entitlement_policy PUBLIC
    sdk_open_interfaces
)

file(GLOB_RECURSE SDK_OPEN_CGRAPH_SRCS
    "${SDK_OPEN_CGRAPH_DIR}/src/*.cpp"
)

add_library(sdk_open_cgraph STATIC ${SDK_OPEN_CGRAPH_SRCS})
target_include_directories(sdk_open_cgraph PUBLIC
    "${SDK_OPEN_CGRAPH_DIR}/src"
)
if(NOT WIN32)
    target_link_libraries(sdk_open_cgraph PUBLIC pthread)
endif()

set(SDK_OPEN_RUNTIME_SRCS
    runtime/sdk_app.cpp
    runtime/sdk_json_utils.cpp
    runtime/sdk_config.cpp
    transport/sdk_http_server.cpp
    transport/sdk_tls_utils.cpp
    transport/sdk_ws_command_server.cpp
    transport/sdk_ws_video_server.cpp
    application/admin_application_service.cpp
    application/authorization_service.cpp
    application/capture_pipeline_service.cpp
    application/capture_task_service.cpp
    application/command_application_service.cpp
    application/image_enhance_task_service.cpp
    application/runtime_record_service.cpp
    application/runtime_config_service.cpp
    application/video_session_service.cpp
    facade/device_facade.cpp
    facade/graphic_facade.cpp
    facade/ocr_facade.cpp
    facade/ofd_facade.cpp
    facade/recognition_facade.cpp
    facade/sane_facade.cpp
    facade/storage_facade.cpp
    facade/twain_facade.cpp
)

add_library(sdk_open_logging STATIC
    runtime/sdk_logger.cpp
    runtime/sdk_runtime_paths.cpp
)
if(MSVC)
    target_compile_options(sdk_open_logging PRIVATE
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/runtime/sdk_windows_compat.h"
    )
endif()
target_include_directories(sdk_open_logging PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
    "${SDK_OPEN_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(sdk_open_logging PUBLIC
    sdk_open_third_party
)
if(NOT WIN32)
    target_link_libraries(sdk_open_logging PUBLIC pthread)
endif()

add_library(sdk_open_runtime STATIC ${SDK_OPEN_RUNTIME_SRCS})
target_include_directories(sdk_open_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
    "${SDK_OPEN_GENERATED_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport"
    "${CMAKE_CURRENT_SOURCE_DIR}/application"
    "${CMAKE_CURRENT_SOURCE_DIR}/facade"
    "${CMAKE_CURRENT_SOURCE_DIR}/interfaces"
)
if(SDK_OPEN_ENABLE_TLS)
    # 父工程全局 include path 中同时存在 OpenSSL 1.1；cpp-httplib 仅支持 OpenSSL 3，
    # 因此必须从本目标移除旧路径并显式把 OpenSSL 3 放到最前面。
    get_target_property(SDK_OPEN_RUNTIME_INCLUDE_DIRS sdk_open_runtime INCLUDE_DIRECTORIES)
    list(REMOVE_ITEM SDK_OPEN_RUNTIME_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/third_party_repo/openssl/include/${THIRD_PARTY_SYSTEM}"
    )
    set_property(TARGET sdk_open_runtime PROPERTY INCLUDE_DIRECTORIES "${SDK_OPEN_RUNTIME_INCLUDE_DIRS}")
    target_include_directories(sdk_open_runtime BEFORE PRIVATE "${SDK_OPEN_OPENSSL3_INCLUDE_DIR}")
endif()
target_compile_definitions(sdk_open_runtime PUBLIC
    SDK_OPEN_ENABLE_HTTP_SERVER=$<IF:$<BOOL:${SDK_OPEN_ENABLE_HTTP_SERVER}>,1,0>
)
target_link_libraries(sdk_open_runtime PUBLIC
    sdk_open_interfaces
    sdk_open_entitlement_policy
    sdk_open_cgraph
    sdk_open_logging
    sdk_open_third_party
    sdk_open_tls_deps
)
if(SDK_OPEN_USE_PRIVATE_PROVIDER)
    target_compile_definitions(sdk_open_runtime PRIVATE SDK_USE_PRIVATE_PROVIDER)
endif()
if(MSVC)
    # WSS 的双 listener 模板实例会超过 MSVC 默认 COFF section 数量。
    target_compile_options(sdk_open_runtime PRIVATE /bigobj)
endif()
if(NOT WIN32)
    target_link_libraries(sdk_open_runtime PUBLIC pthread)
endif()

add_library(sdk_mock_providers STATIC
    providers/mock/mock_provider_factory.cpp
)
target_include_directories(sdk_mock_providers PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/providers/mock"
    "${CMAKE_CURRENT_SOURCE_DIR}/interfaces"
)
target_link_libraries(sdk_mock_providers PUBLIC
    sdk_open_interfaces
    sdk_open_logging
)

# 这些静态库会被 sdk_private 共享库复用，必须生成位置无关代码。
set_target_properties(
    sdk_open_entitlement_policy
    sdk_open_cgraph
    sdk_open_logging
    sdk_open_runtime
    sdk_mock_providers
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

add_executable(sdk_open_app
    runtime/sdk_open_main.cpp
)
target_include_directories(sdk_open_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
    "${SDK_OPEN_GENERATED_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport"
    "${CMAKE_CURRENT_SOURCE_DIR}/application"
    "${CMAKE_CURRENT_SOURCE_DIR}/facade"
    "${CMAKE_CURRENT_SOURCE_DIR}/providers/private_windows"
    "${CMAKE_CURRENT_SOURCE_DIR}/providers/mock"
    "${CMAKE_CURRENT_SOURCE_DIR}/interfaces"
)
target_link_libraries(sdk_open_app PRIVATE
    sdk_open_runtime
)
set_target_properties(sdk_open_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CZUR_TARGET_OUTPUT_DIR}"
)
if(WIN32)
    # Windows 保留 PE image version；可执行文件名固定为 sdk_open_app.exe。
    set_target_properties(sdk_open_app PROPERTIES
        VERSION "${SDK_OPEN_VERSION}.${SDK_OPEN_BUILD}"
    )
endif()

if(BUILD_SDK_OPEN_TESTS)
    add_executable(sdk_open_capture_task_service_test
        test/capture_task_service_test.cpp
    )
    target_link_libraries(sdk_open_capture_task_service_test PRIVATE
        sdk_open_runtime
    )
    add_test(NAME sdk_open.capture_task_service
             COMMAND sdk_open_capture_task_service_test)
endif()
if(WIN32 AND SDK_OPEN_USE_PRIVATE_PROVIDER)
    target_sources(sdk_open_app PRIVATE
        providers/private_windows/private_windows_provider_factory.cpp
    )
    target_link_libraries(sdk_open_app PRIVATE
        Advapi32
    )
endif()

function(czur_configure_sdk_open_twain_runtime target_name)
    if(NOT WIN32 OR NOT SDK_OPEN_USE_PRIVATE_PROVIDER OR
       (NOT LIB_ARCH STREQUAL "windows64" AND NOT LIB_ARCH STREQUAL "windows32"))
        return()
    endif()
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "SDK Open TWAIN runtime target does not exist: ${target_name}")
    endif()

    set(_twain_dsm_x64_dir "${CMAKE_SOURCE_DIR}/third_party_repo/twain/windows/x64")
    set(_twain_dsm_x86_dir "${CMAKE_SOURCE_DIR}/third_party_repo/twain/windows/x86")
    set(_twain_dsm_x86_compat_230_dir "${_twain_dsm_x86_dir}/compat/2.3.0")
    set(_twain_dsm_x64 "${_twain_dsm_x64_dir}/TWAINDSM.dll")
    set(_twain_dsm_x86 "${_twain_dsm_x86_dir}/TWAINDSM.dll")
    set(_twain_dsm_x86_compat_230 "${_twain_dsm_x86_compat_230_dir}/TWAINDSM.dll")
    set(_twain_dsm_x64_license "${_twain_dsm_x64_dir}/LICENSE-LGPL-2.1.txt")
    set(_twain_dsm_x86_license "${_twain_dsm_x86_dir}/LICENSE-LGPL-2.1.txt")
    set(_twain_dsm_x86_compat_230_license
        "${_twain_dsm_x86_compat_230_dir}/LICENSE-LGPL-2.1.txt")
    set(_twain_dsm_x64_manifest
        "${CMAKE_SOURCE_DIR}/packaging/windows/twain/TWAINDSM-x64-VERSION-SHA256.txt")
    set(_twain_dsm_x86_manifest
        "${CMAKE_SOURCE_DIR}/packaging/windows/twain/TWAINDSM-x86-VERSION-SHA256.txt")
    set(_twain_dsm_x86_compat_230_manifest
        "${_twain_dsm_x86_compat_230_dir}/TWAINDSM-x86-2.3.0-VERSION-SHA256.txt")

    set(_twain_runtime_files
        "${_twain_dsm_x86}"
        "${_twain_dsm_x86_license}"
        "${_twain_dsm_x86_manifest}"
        "${_twain_dsm_x86_compat_230}"
        "${_twain_dsm_x86_compat_230_license}"
        "${_twain_dsm_x86_compat_230_manifest}")
    if(LIB_ARCH STREQUAL "windows64")
        list(APPEND _twain_runtime_files
            "${_twain_dsm_x64}"
            "${_twain_dsm_x64_license}"
            "${_twain_dsm_x64_manifest}")
    endif()
    foreach(_twain_runtime_file IN LISTS _twain_runtime_files)
        if(NOT EXISTS "${_twain_runtime_file}")
            message(FATAL_ERROR "SDK Open TWAIN runtime file is missing: ${_twain_runtime_file}")
        endif()
    endforeach()

    if(LIB_ARCH STREQUAL "windows32")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DTWAIN_VERIFY_INPUTS=ON
                -DTWAIN_REQUIRE_X86_RUNTIME=ON
                -DTWAIN_SDK_OPEN_ARCH=x86
                "-DTWAIN_DSM_X86=${_twain_dsm_x86}"
                "-DTWAIN_DSM_X86_LICENSE=${_twain_dsm_x86_license}"
                "-DTWAIN_DSM_X86_MANIFEST=${_twain_dsm_x86_manifest}"
                "-DTWAIN_DSM_X86_COMPAT_230=${_twain_dsm_x86_compat_230}"
                "-DTWAIN_DSM_X86_COMPAT_230_LICENSE=${_twain_dsm_x86_compat_230_license}"
                "-DTWAIN_DSM_X86_COMPAT_230_MANIFEST=${_twain_dsm_x86_compat_230_manifest}"
                -P "${CMAKE_SOURCE_DIR}/cmake/VerifyTwainRuntime.cmake"
            COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "$<TARGET_FILE_DIR:${target_name}>/twain"
            COMMAND "${CMAKE_COMMAND}" -E rm -f
                "$<TARGET_FILE_DIR:${target_name}>/TWAINDSM.dll"
                "$<TARGET_FILE_DIR:${target_name}>/TWAINDSM-x64-LICENSE.txt"
                "$<TARGET_FILE_DIR:${target_name}>/TWAINDSM-x64-VERSION-SHA256.txt"
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/TWAINDSM.dll"
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain_helper_x64.exe"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_twain_dsm_x86}"
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/TWAINDSM.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_twain_dsm_x86_license}"
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/TWAINDSM-x86-LICENSE.txt"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_twain_dsm_x86_manifest}"
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/TWAINDSM-x86-VERSION-SHA256.txt"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/compat/2.3.0"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_twain_dsm_x86_compat_230}"
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/compat/2.3.0/TWAINDSM.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_twain_dsm_x86_compat_230_license}"
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/compat/2.3.0/LICENSE-LGPL-2.1.txt"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_twain_dsm_x86_compat_230_manifest}"
                "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/compat/2.3.0/TWAINDSM-x86-2.3.0-VERSION-SHA256.txt"
            COMMAND "${CMAKE_COMMAND}"
                "-DTWAIN_RUNTIME_DIR=$<TARGET_FILE_DIR:${target_name}>"
                -DTWAIN_SDK_OPEN_ISOLATED_RUNTIME=ON
                -DTWAIN_SDK_OPEN_ARCH=x86
                -P "${CMAKE_SOURCE_DIR}/cmake/VerifyTwainRuntime.cmake"
            COMMENT "Copy and verify the Win32 sdk_open TWAIN runtime"
            VERBATIM
        )
        return()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DTWAIN_VERIFY_INPUTS=ON
            -DTWAIN_REQUIRE_X86_RUNTIME=ON
            "-DTWAIN_DSM_X64=${_twain_dsm_x64}"
            "-DTWAIN_DSM_X86=${_twain_dsm_x86}"
            "-DTWAIN_DSM_X64_LICENSE=${_twain_dsm_x64_license}"
            "-DTWAIN_DSM_X86_LICENSE=${_twain_dsm_x86_license}"
            "-DTWAIN_DSM_X64_MANIFEST=${_twain_dsm_x64_manifest}"
            "-DTWAIN_DSM_X86_MANIFEST=${_twain_dsm_x86_manifest}"
            "-DTWAIN_DSM_X86_COMPAT_230=${_twain_dsm_x86_compat_230}"
            "-DTWAIN_DSM_X86_COMPAT_230_LICENSE=${_twain_dsm_x86_compat_230_license}"
            "-DTWAIN_DSM_X86_COMPAT_230_MANIFEST=${_twain_dsm_x86_compat_230_manifest}"
            -P "${CMAKE_SOURCE_DIR}/cmake/VerifyTwainRuntime.cmake"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "$<TARGET_FILE_DIR:${target_name}>/twain"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x64}"
            "$<TARGET_FILE_DIR:${target_name}>/TWAINDSM.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x64_license}"
            "$<TARGET_FILE_DIR:${target_name}>/TWAINDSM-x64-LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x64_manifest}"
            "$<TARGET_FILE_DIR:${target_name}>/TWAINDSM-x64-VERSION-SHA256.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x64}"
            "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/TWAINDSM.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x86}"
            "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/TWAINDSM.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x86_license}"
            "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/TWAINDSM-x86-LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x86_manifest}"
            "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/TWAINDSM-x86-VERSION-SHA256.txt"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/compat/2.3.0"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x86_compat_230}"
            "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/compat/2.3.0/TWAINDSM.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x86_compat_230_license}"
            "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/compat/2.3.0/LICENSE-LGPL-2.1.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_twain_dsm_x86_compat_230_manifest}"
            "$<TARGET_FILE_DIR:${target_name}>/twain_helper_runtime/twain/x86/compat/2.3.0/TWAINDSM-x86-2.3.0-VERSION-SHA256.txt"
        COMMAND "${CMAKE_COMMAND}"
            "-DTWAIN_RUNTIME_DIR=$<TARGET_FILE_DIR:${target_name}>"
            -DTWAIN_REQUIRE_X86_RUNTIME=ON
            -DTWAIN_SDK_OPEN_ISOLATED_RUNTIME=ON
            -DTWAIN_SDK_OPEN_ARCH=x64
            -P "${CMAKE_SOURCE_DIR}/cmake/VerifyTwainRuntime.cmake"
        COMMENT "Copy and verify the sdk_open TWAIN runtime"
        VERBATIM
    )
endfunction()

if(SDK_OPEN_USE_PRIVATE_PROVIDER)
    target_compile_definitions(sdk_open_app PRIVATE SDK_USE_PRIVATE_PROVIDER)
    if(WIN32)
        # sdk_open 只负责运行时装配；private provider/auth C API 由 sdk_private 构建，TWAIN helper 由 module/twain 构建。
        add_dependencies(sdk_open_app sdk_private_auth_c_api sdk_private_providers)
        if(TARGET twain_helper)
            add_dependencies(sdk_open_app twain_helper)
        endif()
        if(TARGET twain_helper)
            add_custom_command(TARGET sdk_open_app POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:twain_helper>"
                    "$<TARGET_FILE_DIR:sdk_open_app>/$<TARGET_FILE_NAME:twain_helper>"
                COMMENT "Copy TWAIN helper into sdk_open_app runtime directory"
            )
            if(TWAIN_HELPER_X86_EXE AND EXISTS "${TWAIN_HELPER_X86_EXE}")
                add_custom_command(TARGET sdk_open_app POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${TWAIN_HELPER_X86_EXE}"
                        "$<TARGET_FILE_DIR:sdk_open_app>/twain_helper_x86.exe"
                    COMMENT "Copy prebuilt x86 TWAIN helper into sdk_open_app runtime directory"
                )
            endif()
            if(LIB_ARCH STREQUAL "windows64" AND TWAIN_HELPER_X64_EXE AND EXISTS "${TWAIN_HELPER_X64_EXE}")
                add_custom_command(TARGET sdk_open_app POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${TWAIN_HELPER_X64_EXE}"
                        "$<TARGET_FILE_DIR:sdk_open_app>/twain_helper_x64.exe"
                    COMMENT "Copy prebuilt x64 TWAIN helper into sdk_open_app runtime directory"
                )
            endif()
        endif()
    else()
        target_link_libraries(sdk_open_app PRIVATE sdk_private_providers)
        if(UNIX AND NOT APPLE AND TARGET docx)
            # Linux private provider 间接依赖 libdocx.so，随 sdk_open_app 进入同一运行目录。
            add_custom_target(sdk_open_docx_runtime_dep
                COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "${CZUR_TARGET_OUTPUT_DIR}"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:docx>"
                    "${CZUR_TARGET_OUTPUT_DIR}/$<TARGET_FILE_NAME:docx>"
                DEPENDS docx
                COMMENT "Copy libdocx into sdk_open_app runtime directory"
            )
            add_dependencies(sdk_open_app sdk_open_docx_runtime_dep)
        endif()
    endif()
else()
    target_link_libraries(sdk_open_app PRIVATE sdk_mock_providers)
endif()

czur_configure_sdk_open_twain_runtime(sdk_open_app)

if(UNIX AND NOT APPLE)
    set_target_properties(sdk_open_app PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()
if(APPLE)
    set_target_properties(sdk_open_app PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        BUILD_RPATH "@executable_path"
        INSTALL_RPATH "@executable_path"
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
    target_link_options(sdk_open_app PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${SDK_OPEN_MAC_INFO_PLIST}"
    )
endif()

if(SDK_OPEN_ENABLE_TLS)
    if(WIN32)
        if(LIB_ARCH STREQUAL "windows32")
            set(SDK_OPEN_TLS_CRYPTO_DLL_NAME "libcrypto-3.dll")
            set(SDK_OPEN_TLS_SSL_DLL_NAME "libssl-3.dll")
        else()
            set(SDK_OPEN_TLS_CRYPTO_DLL_NAME "libcrypto-3-x64.dll")
            set(SDK_OPEN_TLS_SSL_DLL_NAME "libssl-3-x64.dll")
        endif()
        add_custom_command(TARGET sdk_open_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL3_DIR}/lib/${LIB_ARCH}/${SDK_OPEN_TLS_CRYPTO_DLL_NAME}"
                "$<TARGET_FILE_DIR:sdk_open_app>/${SDK_OPEN_TLS_CRYPTO_DLL_NAME}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL3_DIR}/lib/${LIB_ARCH}/${SDK_OPEN_TLS_SSL_DLL_NAME}"
                "$<TARGET_FILE_DIR:sdk_open_app>/${SDK_OPEN_TLS_SSL_DLL_NAME}"
            COMMENT "Copy OpenSSL 3 runtime DLLs for sdk_open TLS"
        )
    elseif(APPLE)
        add_custom_command(TARGET sdk_open_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL3_SSL_LIB}"
                "$<TARGET_FILE_DIR:sdk_open_app>/libssl.3.dylib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL3_CRYPTO_LIB}"
                "$<TARGET_FILE_DIR:sdk_open_app>/libcrypto.3.dylib"
            COMMENT "Copy OpenSSL 3 runtime libraries for sdk_open TLS"
        )
    elseif(UNIX AND NOT APPLE)
        add_custom_command(TARGET sdk_open_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL3_SSL_LIB}"
                "$<TARGET_FILE_DIR:sdk_open_app>/libssl.so.3"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL3_CRYPTO_LIB}"
                "$<TARGET_FILE_DIR:sdk_open_app>/libcrypto.so.3"
            COMMENT "Copy OpenSSL 3 runtime libraries for sdk_open TLS"
        )
    endif()
endif()

if(BUILD_SDK_PRIVATE)
    if(WIN32)
        set(SDK_OPEN_LIBXML2_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/libxml2/lib/win_debug/libxml2d.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/libxml2/lib/${LIB_ARCH}/libxml2.dll>")
        set(SDK_OPEN_LIBXML2_DLL_DST "$<$<CONFIG:Debug>:$<TARGET_FILE_DIR:sdk_open_app>/libxml2d.dll>$<$<NOT:$<CONFIG:Debug>>:$<TARGET_FILE_DIR:sdk_open_app>/libxml2.dll>")
        set(SDK_OPEN_ZLIB_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/zlib/lib/win_debug/zlib.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/zlib/lib/${LIB_ARCH}/zlib.dll>")
        set(SDK_OPEN_ZLIB1_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/zlib/lib/win_debug/zlib1.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/zlib/lib/${LIB_ARCH}/zlib1.dll>")
        set(SDK_OPEN_OPENCV_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/opencv/lib/win_debug/opencv_world450d.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/opencv/lib/${LIB_ARCH}/opencv_world450.dll>")
        set(SDK_OPEN_OPENCV_DLL_DST "$<$<CONFIG:Debug>:$<TARGET_FILE_DIR:sdk_open_app>/opencv_world450d.dll>$<$<NOT:$<CONFIG:Debug>>:$<TARGET_FILE_DIR:sdk_open_app>/opencv_world450.dll>")
        set(SDK_OPEN_CAIRO_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/cairo/lib/win_debug/cairo.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/cairo/lib/${LIB_ARCH}/cairo.dll>")
        set(SDK_OPEN_FONTCONFIG_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/fontconfig/lib/win_debug/fontconfig.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/fontconfig/lib/${LIB_ARCH}/fontconfig.dll>")
        set(SDK_OPEN_ICONV_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/libiconv/lib/${LIB_ARCH}/libiconvD.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/libiconv/lib/${LIB_ARCH}/libiconv.dll>")
        set(SDK_OPEN_ICONV_DLL_DST "$<$<CONFIG:Debug>:$<TARGET_FILE_DIR:sdk_open_app>/libiconvD.dll>$<$<NOT:$<CONFIG:Debug>>:$<TARGET_FILE_DIR:sdk_open_app>/libiconv.dll>")
        set(SDK_OPEN_FREEIMAGE_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/freeimage/lib/win_debug/FreeImaged.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/freeimage/lib/${LIB_ARCH}/FreeImage.dll>")
        set(SDK_OPEN_FREEIMAGE_DLL_DST "$<$<CONFIG:Debug>:$<TARGET_FILE_DIR:sdk_open_app>/FreeImaged.dll>$<$<NOT:$<CONFIG:Debug>>:$<TARGET_FILE_DIR:sdk_open_app>/FreeImage.dll>")
        set(SDK_OPEN_TIFF_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/tiff/lib/win_debug/tiffd.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/tiff/lib/${LIB_ARCH}/tiff.dll>")
        set(SDK_OPEN_TIFF_DLL_DST "$<$<CONFIG:Debug>:$<TARGET_FILE_DIR:sdk_open_app>/tiffd.dll>$<$<NOT:$<CONFIG:Debug>>:$<TARGET_FILE_DIR:sdk_open_app>/tiff.dll>")
        set(SDK_OPEN_LIBPNG_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/libpng/lib/win_debug/libpng16d.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/libpng/lib/${LIB_ARCH}/libpng16.dll>")
        set(SDK_OPEN_LIBPNG_DLL_DST "$<$<CONFIG:Debug>:$<TARGET_FILE_DIR:sdk_open_app>/libpng16d.dll>$<$<NOT:$<CONFIG:Debug>>:$<TARGET_FILE_DIR:sdk_open_app>/libpng16.dll>")
        set(SDK_OPEN_JPEG_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/libjpeg-turbo/lib/win_debug/jpeg8.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/libjpeg-turbo/lib/${LIB_ARCH}/jpeg8.dll>")
        set(SDK_OPEN_TURBOJPEG_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/libjpeg-turbo/lib/win_debug/turbojpeg.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/libjpeg-turbo/lib/${LIB_ARCH}/turbojpeg.dll>")
        set(SDK_OPEN_CZCV_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/czcv/lib/win_debug/czcv_scan.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/czcv/lib/${LIB_ARCH}/czcv_scan.dll>")
        set(SDK_OPEN_OPENJPEG_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/openjpeg/lib/win_debug/libopenjp2.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/openjpeg/lib/${LIB_ARCH}/libopenjp2.dll>")
        set(SDK_OPEN_LZMA_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/xz/lib/win_debug/liblzma.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/xz/lib/${LIB_ARCH}/liblzma.dll>")
        set(SDK_OPEN_ZSTD_DLL_SRC "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/third_party_repo/zstd/lib/win_debug/zstd.dll>$<$<NOT:$<CONFIG:Debug>>:${CMAKE_SOURCE_DIR}/third_party_repo/zstd/lib/${LIB_ARCH}/zstd.dll>")
        set(SDK_OPEN_CZCV_MODEL_DIR "${CMAKE_SOURCE_DIR}/third_party_repo/czcv/models/x86-64")
        set(SDK_OPEN_CZOCR_MODEL_DIR "${CMAKE_SOURCE_DIR}/third_party_repo/czocr/models/${LIB_ARCH}")
        if(LIB_ARCH STREQUAL "windows32")
            set(SDK_OPEN_CZOCR_MODEL_DIR "${CMAKE_SOURCE_DIR}/third_party_repo/czocr/models/windows64")
        endif()
        if(LIB_ARCH STREQUAL "windows32")
            set(SDK_OPEN_OPENSSL11_CRYPTO_DLL_NAME "libcrypto-1_1.dll")
            set(SDK_OPEN_OPENSSL11_SSL_DLL_NAME "libssl-1_1.dll")
            set(SDK_OPEN_OPENSSL3_CRYPTO_DLL_NAME "libcrypto-3.dll")
            set(SDK_OPEN_OPENSSL3_SSL_DLL_NAME "libssl-3.dll")
        else()
            set(SDK_OPEN_OPENSSL11_CRYPTO_DLL_NAME "libcrypto-1_1-x64.dll")
            set(SDK_OPEN_OPENSSL11_SSL_DLL_NAME "libssl-1_1-x64.dll")
            set(SDK_OPEN_OPENSSL3_CRYPTO_DLL_NAME "libcrypto-3-x64.dll")
            set(SDK_OPEN_OPENSSL3_SSL_DLL_NAME "libssl-3-x64.dll")
        endif()
        set(SDK_OPEN_OPENSSL11_CRYPTO_DLL_SRC "${CMAKE_SOURCE_DIR}/third_party_repo/openssl/lib/${LIB_ARCH}/${SDK_OPEN_OPENSSL11_CRYPTO_DLL_NAME}")
        set(SDK_OPEN_OPENSSL11_SSL_DLL_SRC "${CMAKE_SOURCE_DIR}/third_party_repo/openssl/lib/${LIB_ARCH}/${SDK_OPEN_OPENSSL11_SSL_DLL_NAME}")
        set(SDK_OPEN_OPENSSL3_CRYPTO_DLL_SRC "${CMAKE_SOURCE_DIR}/third_party_repo/openssl3/lib/${LIB_ARCH}/${SDK_OPEN_OPENSSL3_CRYPTO_DLL_NAME}")
        set(SDK_OPEN_OPENSSL3_SSL_DLL_SRC "${CMAKE_SOURCE_DIR}/third_party_repo/openssl3/lib/${LIB_ARCH}/${SDK_OPEN_OPENSSL3_SSL_DLL_NAME}")
    endif()

    set(SDK_OPEN_COPY_OCR_FONTS_COMMANDS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/ocr/fonts")
        list(APPEND SDK_OPEN_COPY_OCR_FONTS_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_SOURCE_DIR}/resources/ocr/fonts"
                "$<TARGET_FILE_DIR:sdk_open_app>/ocr_fonts")
    endif()

    add_custom_command(TARGET sdk_open_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_open_app>/configs"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/module/devices/device_config_compact.conf"
            "$<TARGET_FILE_DIR:sdk_open_app>/configs/device_config_compact.conf"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/app/process/publicd/sane_open_source_devices.json"
            "$<TARGET_FILE_DIR:sdk_open_app>/configs/sane_open_source_devices.json"
        ${SDK_OPEN_COPY_OCR_FONTS_COMMANDS}
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/ofd/fonts"
            "$<TARGET_FILE_DIR:sdk_open_app>/ofd_fonts"
        COMMENT "Copy config and OCR/OFD font resources into sdk_open_app runtime directory"
    )

    if(WIN32)
        set(SDK_OPEN_PRIVATE_PROVIDER_DLL_COPY_COMMANDS)
        if(SDK_OPEN_USE_PRIVATE_PROVIDER)
            list(APPEND SDK_OPEN_PRIVATE_PROVIDER_DLL_COPY_COMMANDS
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:sdk_private_auth_c_api>"
                    "$<TARGET_FILE_DIR:sdk_open_app>/$<TARGET_FILE_NAME:sdk_private_auth_c_api>"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:sdk_private_providers>"
                    "$<TARGET_FILE_DIR:sdk_open_app>/$<TARGET_FILE_NAME:sdk_private_providers>"
            )
        endif()
        set(SDK_OPEN_OPENJPEG_DLL_COPY_COMMANDS)
        if(EXISTS "${CMAKE_SOURCE_DIR}/third_party_repo/openjpeg/lib/${LIB_ARCH}/libopenjp2.dll")
            list(APPEND SDK_OPEN_OPENJPEG_DLL_COPY_COMMANDS
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${SDK_OPEN_OPENJPEG_DLL_SRC}"
                    "$<TARGET_FILE_DIR:sdk_open_app>/libopenjp2-7.dll"
            )
        endif()

        add_custom_command(TARGET sdk_open_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E echo "Copying sdk_open_app dependency DLLs..."
            ${SDK_OPEN_PRIVATE_PROVIDER_DLL_COPY_COMMANDS}
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${SDK_OPEN_CZCV_MODEL_DIR}"
                "$<TARGET_FILE_DIR:sdk_open_app>/czcv_model"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${SDK_OPEN_CZOCR_MODEL_DIR}"
                "$<TARGET_FILE_DIR:sdk_open_app>/models"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third_party_repo/czocr/models/x86-64/license.lic"
                "$<TARGET_FILE_DIR:sdk_open_app>/models/license.lic"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third_party_repo/czocr/models/x86-64/serial.txt"
                "$<TARGET_FILE_DIR:sdk_open_app>/models/serial.txt"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third_party_repo/czocr/lib/${LIB_ARCH}/czcv_ocr.dll"
                "$<TARGET_FILE_DIR:sdk_open_app>/models/czcv_ocr.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_LIBXML2_DLL_SRC}"
                "${SDK_OPEN_LIBXML2_DLL_DST}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_LIBXML2_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/libxml2.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_ZLIB_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/zlib.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_ZLIB1_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/zlib1.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENCV_DLL_SRC}"
                "${SDK_OPEN_OPENCV_DLL_DST}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_CAIRO_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/cairo.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third_party_repo/libzip/lib/${LIB_ARCH}/zip.dll"
                "$<TARGET_FILE_DIR:sdk_open_app>/zip.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_FONTCONFIG_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/fontconfig.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_ICONV_DLL_SRC}"
                "${SDK_OPEN_ICONV_DLL_DST}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third_party_repo/czocr/lib/${LIB_ARCH}/czcv_ocr_api.dll"
                "$<TARGET_FILE_DIR:sdk_open_app>/czcv_ocr_api.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:docx>"
                "$<TARGET_FILE_DIR:sdk_open_app>/docx.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_FREEIMAGE_DLL_SRC}"
                "${SDK_OPEN_FREEIMAGE_DLL_DST}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third_party_repo/libexif/lib/${LIB_ARCH}/libexif.dll"
                "$<TARGET_FILE_DIR:sdk_open_app>/libexif.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_TIFF_DLL_SRC}"
                "${SDK_OPEN_TIFF_DLL_DST}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_CZCV_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/czcv_scan.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_LZMA_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/liblzma.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_ZSTD_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/zstd.dll"
            ${SDK_OPEN_OPENJPEG_DLL_COPY_COMMANDS}
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_LIBPNG_DLL_SRC}"
                "${SDK_OPEN_LIBPNG_DLL_DST}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_LIBPNG_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/libpng16.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_JPEG_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/jpeg8.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_TURBOJPEG_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/turbojpeg.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL11_CRYPTO_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/${SDK_OPEN_OPENSSL11_CRYPTO_DLL_NAME}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL11_SSL_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/${SDK_OPEN_OPENSSL11_SSL_DLL_NAME}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL3_CRYPTO_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/${SDK_OPEN_OPENSSL3_CRYPTO_DLL_NAME}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENSSL3_SSL_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/${SDK_OPEN_OPENSSL3_SSL_DLL_NAME}"
            COMMENT "Copy sdk_open_app dependency DLLs into runtime directory"
        )
    endif()
endif()

set(SDK_WEB_PREBUILT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/web")
set(SDK_WEB_PREBUILT_ADMIN_DIR "${SDK_WEB_PREBUILT_ROOT}/admin")
set(SDK_WEB_PREBUILT_DEMO_DIR "${SDK_WEB_PREBUILT_ROOT}/demo")

if(SDK_OPEN_USE_PREBUILT_WEB)
    if(NOT EXISTS "${SDK_WEB_PREBUILT_ADMIN_DIR}/index.html")
        message(FATAL_ERROR "SDK_OPEN_USE_PREBUILT_WEB=ON requires ${SDK_WEB_PREBUILT_ADMIN_DIR}/index.html")
    endif()
    if(NOT EXISTS "${SDK_WEB_PREBUILT_DEMO_DIR}/index.html")
        message(FATAL_ERROR "SDK_OPEN_USE_PREBUILT_WEB=ON requires ${SDK_WEB_PREBUILT_DEMO_DIR}/index.html")
    endif()

    add_custom_command(TARGET sdk_open_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_PREBUILT_ADMIN_DIR}" "$<TARGET_FILE_DIR:sdk_open_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/demo"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/demo"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_PREBUILT_DEMO_DIR}" "$<TARGET_FILE_DIR:sdk_open_app>/web/demo"
        COMMENT "Copy prebuilt sdk web assets into sdk_open_app runtime directory"
    )
elseif(BUILD_SDK_WEB)
    message(STATUS "ENV PATH=$ENV{PATH}")
    if(WIN32)
        find_program(PNPM_CMD_EXECUTABLE pnpm.cmd)
        if(PNPM_CMD_EXECUTABLE)
            set(PNPM_EXECUTABLE "${PNPM_CMD_EXECUTABLE}" CACHE FILEPATH "pnpm executable" FORCE)
        endif()
    endif()
    if(NOT PNPM_EXECUTABLE)
        find_program(PNPM_EXECUTABLE pnpm)
    endif()
    if(NOT PNPM_EXECUTABLE)
        message(FATAL_ERROR "BUILD_SDK_WEB=ON requires pnpm in PATH")
    endif()

    set(SDK_WEB_ADMIN_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frontend/admin-site")
    set(SDK_WEB_DEMO_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frontend/demo-site")
    set(SDK_WEB_ADMIN_DIST_DIR "${SDK_WEB_ADMIN_PROJECT_DIR}/dist")
    set(SDK_WEB_DEMO_DIST_DIR "${SDK_WEB_DEMO_PROJECT_DIR}/dist")
    set(SDK_WEB_ADMIN_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web/admin")
    set(SDK_WEB_DEMO_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web/demo")

    add_custom_target(sdk_web_admin_build
        COMMAND "${CMAKE_COMMAND}" -E chdir "${SDK_WEB_ADMIN_PROJECT_DIR}" "${PNPM_EXECUTABLE}" install --force --no-frozen-lockfile
        COMMAND "${CMAKE_COMMAND}" -E chdir "${SDK_WEB_ADMIN_PROJECT_DIR}" "${PNPM_EXECUTABLE}" build
        COMMENT "Build admin-site with pnpm"
    )

    add_custom_target(sdk_web_demo_build
        COMMAND "${CMAKE_COMMAND}" -E chdir "${SDK_WEB_DEMO_PROJECT_DIR}" "${PNPM_EXECUTABLE}" install --force --no-frozen-lockfile
        COMMAND "${CMAKE_COMMAND}" -E chdir "${SDK_WEB_DEMO_PROJECT_DIR}" "${PNPM_EXECUTABLE}" build
        COMMENT "Build demo-site with pnpm"
    )

    add_custom_target(sdk_web_assets
        DEPENDS sdk_web_admin_build sdk_web_demo_build
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${SDK_WEB_ADMIN_TARGET_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SDK_WEB_ADMIN_TARGET_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_ADMIN_DIST_DIR}" "${SDK_WEB_ADMIN_TARGET_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${SDK_WEB_DEMO_TARGET_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SDK_WEB_DEMO_TARGET_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_DEMO_DIST_DIR}" "${SDK_WEB_DEMO_TARGET_DIR}"
        COMMENT "Build sdk web assets and copy dist into web/admin|demo"
    )

    add_dependencies(sdk_open_app sdk_web_assets)
    add_custom_command(TARGET sdk_open_app POST_BUILD
        # 运行目录复制必须挂在 sdk_open_app POST_BUILD，避免 sdk_web_assets 反向依赖 sdk_open_app 形成 CMake 环。
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_ADMIN_DIST_DIR}" "$<TARGET_FILE_DIR:sdk_open_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/demo"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/demo"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_DEMO_DIST_DIR}" "$<TARGET_FILE_DIR:sdk_open_app>/web/demo"
        COMMENT "Copy sdk web assets into sdk_open_app runtime directory"
    )
endif()
