// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "mock_provider_factory.h"

#include <memory>
#include <utility>

namespace editor {
namespace sdk {
namespace mock {

namespace {

class MockDeviceProvider : public ISdkDeviceProvider {
public:
    std::string ProviderName() const override { return "mock-device-provider"; }
    std::vector<std::string> ListDevices() const override { return {"mock-device-01"}; }
};

class MockGraphicProvider : public ISdkGraphicProvider {
public:
    std::string ProviderName() const override { return "mock-graphic-provider"; }
    bool ProcessSampleTask(const std::string&, const std::string&) override { return true; }
};

class MockOcrProvider : public ISdkOcrProvider {
public:
    std::string ProviderName() const override { return "mock-ocr-provider"; }
    std::string SubmitTask(const std::vector<std::string>&, const std::string&) override { return "mock-ocr-task-1"; }
};

class MockOfdProvider : public ISdkOfdProvider {
public:
    std::string ProviderName() const override { return "mock-ofd-provider"; }
    bool OpenDocument(const std::string&) override { return true; }
};

} // namespace

ProviderBundle CreateProviderBundle() {
    ProviderBundle bundle;
    bundle.device_provider = std::make_shared<MockDeviceProvider>();
    bundle.graphic_provider = std::make_shared<MockGraphicProvider>();
    bundle.ocr_provider = std::make_shared<MockOcrProvider>();
    bundle.ofd_provider = std::make_shared<MockOfdProvider>();
    return bundle;
}

} // namespace mock
} // namespace sdk
} // namespace editor
