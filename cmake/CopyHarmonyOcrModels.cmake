# Share the Harmony model layout between the main app and SDK Open.
foreach(required_var MODEL_SOURCE_DIR OCR_RUNTIME_LIB MODEL_OUTPUT_DIR)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "Missing ${required_var}")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${MODEL_SOURCE_DIR}" OR NOT EXISTS "${OCR_RUNTIME_LIB}")
    message(FATAL_ERROR "Harmony OCR model directory or runtime library is missing")
endif()

file(MAKE_DIRECTORY "${MODEL_OUTPUT_DIR}")
file(COPY "${MODEL_SOURCE_DIR}/" DESTINATION "${MODEL_OUTPUT_DIR}"
    PATTERN "libczcv_ocr.so" EXCLUDE
    PATTERN "libczcv_ocr_naive.so" EXCLUDE)
file(REMOVE "${MODEL_OUTPUT_DIR}/libczcv_ocr_naive.so")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${OCR_RUNTIME_LIB}" "${MODEL_OUTPUT_DIR}/libczcv_ocr.so"
    RESULT_VARIABLE copy_result)
if(NOT copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to copy Harmony OCR runtime library: ${copy_result}")
endif()
