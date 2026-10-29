// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include <iostream>
#include <string>

#include "private_provider_factory.h"

namespace {

bool Contains(const std::string& value, const std::string& needle) {
    return value.find(needle) != std::string::npos;
}

int ExpectContains(const std::string& value, const std::string& needle) {
    if (!Contains(value, needle)) {
        std::cerr << "expected response to contain: " << needle << "\nresponse: " << value << std::endl;
        return 1;
    }
    return 0;
}

typedef const char* (*OcrJsonEntryPoint)(const char*);

int ExpectOcrLanguageInvalid(OcrJsonEntryPoint entry_point,
                             const char* entry_name,
                             const char* request_json,
                             const char* expected_message) {
    const char* raw_response = entry_point(request_json);
    if (raw_response == NULL) {
        std::cerr << entry_name << " returned null" << std::endl;
        return 1;
    }
    const std::string response(raw_response);
    czur_sdk_private_providers_free_string(raw_response);
    if (ExpectContains(response, "\"code\":1002") != 0 ||
        ExpectContains(response, expected_message) != 0) {
        std::cerr << entry_name << " did not reject OCR language before provider execution" << std::endl;
        return 1;
    }
    return 0;
}

int TestOcrLanguageBoundary(OcrJsonEntryPoint entry_point, const char* entry_name) {
    struct InvalidLanguageCase {
        const char* request_json;
        const char* expected_message;
    };
    const InvalidLanguageCase cases[] = {
        {"{\"language\":\"\"}", "language must not be empty"},
        {"{\"language\":1}", "language must be a string"},
        {"{\"language\":\"unknown\"}", "unsupported ocr language"},
        {"{\"language\":\"az\"}", "unsupported ocr language"},
        {"{\"language\":\"dar\"}", "unsupported ocr language"},
    };
    const std::size_t case_count = sizeof(cases) / sizeof(cases[0]);
    for (std::size_t i = 0; i < case_count; ++i) {
        if (ExpectOcrLanguageInvalid(entry_point,
                                     entry_name,
                                     cases[i].request_json,
                                     cases[i].expected_message) != 0) {
            return 1;
        }
    }
    return 0;
}

} // namespace

int main() {
    if (TestOcrLanguageBoundary(&czur_sdk_private_ocr_recognize_json, "ocr.recognize") != 0 ||
        TestOcrLanguageBoundary(&czur_sdk_private_ocr_extract_text_json, "ocr.extract_text") != 0) {
        return 1;
    }
    return 0;
}
