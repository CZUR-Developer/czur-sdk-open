// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <string>

namespace editor {
namespace sdk {

// Keep this list in sync with the enabled entries in
// views/src/features/ocr/czOcrLanguages.ts. The explicit array shape is also
// consumed by the SDK Open demo parity test.
static const char* const kSupportedSdkOcrLanguages[] = {
    "auto", "ch", "hu", "en", "rs_latin", "fr", "id", "de", "oc", "japan",
    "is", "korean", "lt", "chinese_cht", "mi", "af", "ms", "it", "nl", "es",
    "no", "bs", "pl", "pt", "sk", "cs", "sl", "cy", "sq", "da", "sv", "et",
    "sw", "ga", "tl", "hr", "tr", "uz", "la", "ru", "be", "uk", "th", "el",
    "ku", "lv", "mt", "ro", "fi", "eu", "gl", "lb", "rm", "ca", "qu", "te",
    "rs_cyrillic", "bg", "mn", "ady", "kbd", "av", "inh", "ce", "lki", "lez",
    "kk", "ky", "tg", "mk", "tt", "ba", "udm", "kv", "bua", "xal", "tyv",
    "sah", "kaa", "ar", "hi", "mr", "ne", "bh", "mai", "bho", "mah", "new",
    "gom", "ta",
};

inline bool IsSupportedSdkOcrLanguage(const std::string& language) {
    const std::size_t count = sizeof(kSupportedSdkOcrLanguages) / sizeof(kSupportedSdkOcrLanguages[0]);
    for (std::size_t i = 0; i < count; ++i) {
        if (language == kSupportedSdkOcrLanguages[i]) {
            return true;
        }
    }
    return false;
}

inline std::string SdkOcrEngineLanguage(const std::string& language) {
    return language == "auto" ? "ch" : language;
}

// Reads the public request.params.language contract without silently accepting
// invalid JSON types. An omitted field defaults to auto.
template <typename JsonObject>
inline bool ReadSdkOcrLanguage(const JsonObject& params,
                               std::string* language,
                               std::string* error) {
    if (language == NULL) {
        return false;
    }
    typename JsonObject::const_iterator it = params.find("language");
    if (it == params.end()) {
        *language = "auto";
        return true;
    }
    if (!it->is_string()) {
        if (error != NULL) {
            *error = "language must be a string";
        }
        return false;
    }
    const std::string value = it->template get<std::string>();
    if (value.empty()) {
        if (error != NULL) {
            *error = "language must not be empty";
        }
        return false;
    }
    if (!IsSupportedSdkOcrLanguage(value)) {
        if (error != NULL) {
            *error = "unsupported ocr language";
        }
        return false;
    }
    *language = value;
    return true;
}

} // namespace sdk
} // namespace editor
