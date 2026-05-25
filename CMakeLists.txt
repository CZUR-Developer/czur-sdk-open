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
)

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
target_link_libraries(sdk_open_cgraph PUBLIC pthread)

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
target_include_directories(sdk_open_logging PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
)
target_link_libraries(sdk_open_logging PUBLIC
    sdk_open_third_party
)
target_link_libraries(sdk_open_logging PUBLIC pthread)

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
target_link_libraries(sdk_open_runtime PUBLIC pthread)

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

if(BUILD_SDK_PRIVATE)
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
endif()

if(BUILD_SDK_WEB)
    message(STATUS "ENV PATH=$ENV{PATH}")
    find_program(PNPM_EXECUTABLE pnpm)
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
        COMMAND "${CMAKE_COMMAND}" -E chdir "${SDK_WEB_ADMIN_PROJECT_DIR}" "${PNPM_EXECUTABLE}" install --no-frozen-lockfile
        COMMAND "${CMAKE_COMMAND}" -E chdir "${SDK_WEB_ADMIN_PROJECT_DIR}" "${PNPM_EXECUTABLE}" build
        COMMENT "Build admin-site with pnpm"
    )

    add_custom_target(sdk_web_demo_build
        COMMAND "${CMAKE_COMMAND}" -E chdir "${SDK_WEB_DEMO_PROJECT_DIR}" "${PNPM_EXECUTABLE}" install --no-frozen-lockfile
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
