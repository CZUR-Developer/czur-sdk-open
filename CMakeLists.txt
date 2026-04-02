set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SDK_OPEN_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(SDK_OPEN_CPP_HTTPLIB_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/cpp-httplib")
set(SDK_OPEN_WEBSOCKETPP_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/websocketpp")
set(SDK_OPEN_ASIO_DIR "${SDK_OPEN_THIRD_PARTY_DIR}/asio/asio/include")
set(SDK_OPEN_NLOHMANN_JSON_DIR "${SDK_OPEN_THIRD_PARTY_DIR}")

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

add_library(sdk_open_third_party INTERFACE)
target_include_directories(sdk_open_third_party INTERFACE
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

set(SDK_OPEN_RUNTIME_SRCS
    runtime/sdk_app.cpp
    runtime/sdk_http_server.cpp
    runtime/sdk_json_utils.cpp
    runtime/sdk_ws_command_server.cpp
    runtime/sdk_ws_video_server.cpp
    runtime/sdk_config.cpp
)

add_library(sdk_open_runtime STATIC ${SDK_OPEN_RUNTIME_SRCS})
target_include_directories(sdk_open_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/interfaces"
)
target_link_libraries(sdk_open_runtime PUBLIC
    sdk_open_interfaces
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
target_link_libraries(sdk_mock_providers PUBLIC sdk_open_interfaces)

add_executable(sdk_open_app
    runtime/sdk_open_main.cpp
)
target_include_directories(sdk_open_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/providers/mock"
    "${CMAKE_CURRENT_SOURCE_DIR}/interfaces"
)
target_link_libraries(sdk_open_app PRIVATE
    sdk_open_runtime
    sdk_mock_providers
)

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
