include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/Dependencies.cmake")

set(SDK_APP_SRCS
    sdk_main.cpp
)

add_executable(sdk_app ${SDK_APP_SRCS})
target_compile_features(sdk_app PRIVATE cxx_std_11)

target_include_directories(sdk_app PRIVATE
    "${SDK_OPEN_SOURCE_ROOT}/interfaces"
    "${SDK_OPEN_SOURCE_ROOT}/runtime"
)

target_link_libraries(sdk_app PRIVATE
    sdk_open_runtime
)

if(BUILD_SDK_PRIVATE)
    if(SDK_OPEN_USE_PRIVATE_PROVIDER)
        target_compile_definitions(sdk_app PRIVATE SDK_USE_PRIVATE_PROVIDER)
        target_link_libraries(sdk_app PRIVATE sdk_private_providers)
    else()
        target_link_libraries(sdk_app PRIVATE sdk_mock_providers)
    endif()
else()
    target_link_libraries(sdk_app PRIVATE sdk_mock_providers)
endif()

if(BUILD_SDK_WEB AND TARGET sdk_web_assets)
    add_dependencies(sdk_app sdk_web_assets)
    set(SDK_WEB_ADMIN_DIST_DIR "${SDK_OPEN_SOURCE_ROOT}/frontend/admin-site/dist")
    set(SDK_WEB_DEMO_DIST_DIR "${SDK_OPEN_SOURCE_ROOT}/frontend/demo-site/dist")
    add_custom_command(TARGET sdk_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdk_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_ADMIN_DIST_DIR}" "$<TARGET_FILE_DIR:sdk_app>/web/admin"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:sdk_app>/web/demo"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_app>/web/demo"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDK_WEB_DEMO_DIST_DIR}" "$<TARGET_FILE_DIR:sdk_app>/web/demo"
        COMMENT "Copy sdk web assets into sdk_app runtime directory"
    )
endif()

if(SDK_OPEN_ENABLE_TLS)
    set(SDK_APP_OPENSSL3_DIR "${SDK_OPEN_THIRD_PARTY_ROOT}/openssl3")
    if(WIN32)
        if(LIB_ARCH STREQUAL "windows32")
            set(SDK_APP_OPENSSL3_CRYPTO_DLL "libcrypto-3.dll")
            set(SDK_APP_OPENSSL3_SSL_DLL "libssl-3.dll")
        else()
            set(SDK_APP_OPENSSL3_CRYPTO_DLL "libcrypto-3-x64.dll")
            set(SDK_APP_OPENSSL3_SSL_DLL "libssl-3-x64.dll")
        endif()
        add_custom_command(TARGET sdk_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_APP_OPENSSL3_DIR}/lib/${LIB_ARCH}/${SDK_APP_OPENSSL3_CRYPTO_DLL}"
                "$<TARGET_FILE_DIR:sdk_app>/${SDK_APP_OPENSSL3_CRYPTO_DLL}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_APP_OPENSSL3_DIR}/lib/${LIB_ARCH}/${SDK_APP_OPENSSL3_SSL_DLL}"
                "$<TARGET_FILE_DIR:sdk_app>/${SDK_APP_OPENSSL3_SSL_DLL}"
            COMMENT "Copy OpenSSL 3 runtime DLLs for sdk_app TLS"
        )
    elseif(UNIX AND NOT APPLE)
        set_target_properties(sdk_app PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            BUILD_RPATH "$ORIGIN"
            INSTALL_RPATH "$ORIGIN"
            INSTALL_RPATH_USE_LINK_PATH FALSE
        )
        add_custom_command(TARGET sdk_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_APP_OPENSSL3_DIR}/lib/${LIB_ARCH}/libssl.so.3"
                "$<TARGET_FILE_DIR:sdk_app>/libssl.so.3"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDK_APP_OPENSSL3_DIR}/lib/${LIB_ARCH}/libcrypto.so.3"
                "$<TARGET_FILE_DIR:sdk_app>/libcrypto.so.3"
            COMMENT "Copy OpenSSL 3 runtime libraries for sdk_app TLS"
        )
    endif()
endif()

if(BUILD_SDK_PRIVATE)
    add_custom_command(TARGET sdk_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sdk_app>/configs"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDK_OPEN_HOST_SOURCE_ROOT}/src/module/devices/device_config_compact.conf"
            "$<TARGET_FILE_DIR:sdk_app>/configs/device_config_compact.conf"
        COMMENT "Copy device config into sdk_app runtime directory"
    )
endif()
