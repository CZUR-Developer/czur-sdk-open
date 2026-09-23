// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <stdexcept>
#include <string>

#include "sdk_app.h"
#include "sdk_open_version.h"

namespace editor {
namespace sdk {

class SdkAppStatusTest {
public:
    static Json BuildStatus(const SdkApp& app) {
        return app.BuildStatusJson();
    }
};

} // namespace sdk
} // namespace editor

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        editor::sdk::SdkConfig config;
        editor::sdk::ProviderBundle providers;
        const editor::sdk::SdkApp app(config, providers);
        const editor::sdk::Json status = editor::sdk::SdkAppStatusTest::BuildStatus(app);
        const editor::sdk::Json::const_iterator software = status.find("software");

        Require(software != status.end() && software->is_object(), "system.info must expose software");
        Require(software->size() == 3, "system.info software must remain a minimal projection");
        Require(software->value("version", "") == SDK_OPEN_VERSION_STRING,
                "system.info software.version must match the Runtime release version");
        Require(software->value("protocolVersion", "") == "2.0.0",
                "system.info software.protocolVersion must match the SDK Open protocol");

        const editor::sdk::Json::const_iterator build = software->find("build");
        Require(build != software->end() && build->is_number_integer(), "system.info software.build must be an integer");
        Require(build->get<long long>() >= 0, "system.info software.build must be non-negative");

        std::cout << "sdk_open_status_software_test passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk_open_status_software_test failed: " << error.what() << std::endl;
        return 1;
    }
}
