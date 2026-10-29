set(SDK_PRIVATE_AUTH_CORE_SRCS
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/auth/private_sdk_auth_service.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/auth/private_sdk_crypto_utils.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/auth/private_sdk_key_codec.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/auth/private_sdk_master_secret.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/auth/private_sdk_session_service.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/auth/private_sdk_support.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/support/private_sdk_executable_path.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/support/private_sdk_logger.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/private_auth_provider.cpp"
)

if(APPLE)
    list(APPEND SDK_PRIVATE_AUTH_CORE_SRCS
        "${SDK_OPEN_SOURCE_ROOT}/providers/private/support/private_sdk_app_runner_compat.cpp"
    )
endif()

set(SDK_PRIVATE_PROVIDER_SRCS
    "${SDK_OPEN_HOST_SOURCE_ROOT}/src/app/process/graphic/czcv_cfg_compat.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/private_device_provider.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/private_graphic_provider.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/private_image_enhance_provider.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/online_image_enhance_client.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/private_provider_factory.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/private_recognition_provider.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/private_sane_provider.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/private_storage_cleanup.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/graphic/color_mode_processor.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/graphic/curved_book_processor.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/graphic/format_convert_processor.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/graphic/graphic_algorithm_runtime.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/graphic/image_codec_lock.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/graphic/single_page_processor.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/graphic/thumbnail_processor.cpp"
    "${SDK_OPEN_SOURCE_ROOT}/providers/private/image_write_utils.cpp"
)
