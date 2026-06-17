set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SDK_OPEN_ENABLE_HTTP_SERVER "Start sdk_open embedded admin/demo HTTP servers" ON)

set(SDK_OPEN_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(SDK_OPEN_CPP_HTTPLIB_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/cpp-httplib")
set(SDK_OPEN_WEBSOCKETPP_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/websocketpp")
set(SDK_OPEN_ASIO_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/asio/asio/include")
set(SDK_OPEN_NLOHMANN_JSON_DIR "${SDK_OPEN_THIRD_PARTY_DIR}")
set(SDK_OPEN_SPDLOG_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/spdlog")
set(SDK_OPEN_CGRAPH_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/CGraph")

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
    "${CMAKE_CURRENT_SOURCE_DIR}/transport"
    "${CMAKE_CURRENT_SOURCE_DIR}/application"
    "${CMAKE_CURRENT_SOURCE_DIR}/facade"
    "${CMAKE_CURRENT_SOURCE_DIR}/interfaces"
)
target_compile_definitions(sdk_open_runtime PUBLIC
    SDK_OPEN_ENABLE_HTTP_SERVER=$<IF:$<BOOL:${SDK_OPEN_ENABLE_HTTP_SERVER}>,1,0>
)
target_link_libraries(sdk_open_runtime PUBLIC
    sdk_open_interfaces
    sdk_open_cgraph
    sdk_open_logging
    sdk_open_third_party
)
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

add_executable(sdk_open_app
    runtime/sdk_open_main.cpp
)
target_include_directories(sdk_open_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport"
    "${CMAKE_CURRENT_SOURCE_DIR}/application"
    "${CMAKE_CURRENT_SOURCE_DIR}/facade"
    "${CMAKE_CURRENT_SOURCE_DIR}/providers/mock"
    "${CMAKE_CURRENT_SOURCE_DIR}/interfaces"
)
target_link_libraries(sdk_open_app PRIVATE
    sdk_open_runtime
)

if(SDK_OPEN_USE_PRIVATE_PROVIDER)
    target_compile_definitions(sdk_open_app PRIVATE SDK_USE_PRIVATE_PROVIDER)
    target_link_libraries(sdk_open_app PRIVATE sdk_private_providers)
else()
    target_link_libraries(sdk_open_app PRIVATE sdk_mock_providers)
endif()

if(UNIX AND NOT APPLE)
    set_target_properties(sdk_open_app PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
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
    endif()

    add_custom_command(TARGET sdk_open_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_open_app>/configs"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/module/devices/device_config_compact.conf"
            "$<TARGET_FILE_DIR:sdk_open_app>/configs/device_config_compact.conf"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/app/process/publicd/sane_open_source_devices.json"
            "$<TARGET_FILE_DIR:sdk_open_app>/configs/sane_open_source_devices.json"
        COMMENT "Copy device config into sdk_open_app runtime directory"
    )

    if(WIN32)
        add_custom_command(TARGET sdk_open_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E echo "Copying sdk_open_app dependency DLLs..."
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${SDK_OPEN_CZCV_MODEL_DIR}"
                "$<TARGET_FILE_DIR:sdk_open_app>/czcv_model"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${SDK_OPEN_CZOCR_MODEL_DIR}"
                "$<TARGET_FILE_DIR:sdk_open_app>/models"
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
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_OPEN_OPENJPEG_DLL_SRC}"
                "$<TARGET_FILE_DIR:sdk_open_app>/libopenjp2-7.dll"
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
                "${CMAKE_SOURCE_DIR}/third_party_repo/openssl/lib/${LIB_ARCH}/libcrypto-1_1-x64.dll"
                "$<TARGET_FILE_DIR:sdk_open_app>/libcrypto-1_1-x64.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third_party_repo/openssl/lib/${LIB_ARCH}/libssl-1_1-x64.dll"
                "$<TARGET_FILE_DIR:sdk_open_app>/libssl-1_1-x64.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third_party_repo/openssl3/lib/${LIB_ARCH}/libcrypto-3-x64.dll"
                "$<TARGET_FILE_DIR:sdk_open_app>/libcrypto-3-x64.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/third_party_repo/openssl3/lib/${LIB_ARCH}/libssl-3-x64.dll"
                "$<TARGET_FILE_DIR:sdk_open_app>/libssl-3-x64.dll"
            COMMENT "Copy sdk_open_app dependency DLLs into runtime directory"
        )
    endif()
endif()

if(BUILD_SDK_WEB)
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
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_ADMIN_DIST_DIR}" "$<TARGET_FILE_DIR:sdk_open_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/demo"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_open_app>/web/demo"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_DEMO_DIST_DIR}" "$<TARGET_FILE_DIR:sdk_open_app>/web/demo"
        COMMENT "Copy sdk web assets into sdk_open_app runtime directory"
    )
endif()
