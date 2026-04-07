// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "sdk_auth_types.h"
#include "sdk_config.h"
#include "sdk_json_utils.h"
#include "sdk_provider_bundle.h"

namespace editor {
namespace sdk {

enum class SdkAuthScope {
    Anonymous = 0,
    Authenticated = 1,
    Entitled = 2,
};

enum class SdkDeviceScopePolicy {
    None = 0,
    TargetDevice = 1,
    AllBoundDevices = 2,
};

struct SdkMethodDescriptor {
    std::string method;
    SdkAuthScope auth_scope = SdkAuthScope::Anonymous;
    SdkDeviceScopePolicy device_scope = SdkDeviceScopePolicy::None;
    std::string status = "ga";
    std::string summary;
    std::vector<std::string> legacy_aliases;
};

struct SdkCommandRequest {
    std::string request_id;
    std::string original_method;
    std::string method;
    Json params = Json::object();
    Json auth = Json::object();
    Json client = Json::object();
};

class SdkCommandDispatcher {
public:
    using StatusSupplier = std::function<Json()>;

    SdkCommandDispatcher(const SdkConfig& config, const ProviderBundle& providers);

    void SetStatusSupplier(StatusSupplier supplier);
    Json Dispatch(const Json& request_json);
    Json BuildCapabilitiesJson() const;

private:
    bool MethodRequiresSession(const SdkMethodDescriptor& descriptor) const;
    bool EnsureMethodAuthorized(const SdkCommandRequest& request,
                                const SdkMethodDescriptor& descriptor,
                                AuthContext* auth_context,
                                Json* failure_response) const;
    bool HasCapability(const AuthContext& auth_context, const std::string& capability) const;
    const SdkMethodDescriptor* FindMethodDescriptor(const std::string& method) const;
    std::string ResolveMethod(const std::string& method) const;
    Json HandleSystemPing(const SdkCommandRequest& request) const;
    Json HandleSystemInfo(const SdkCommandRequest& request) const;
    Json HandleSystemCapabilities(const SdkCommandRequest& request) const;
    Json HandleAuthValidate(const SdkCommandRequest& request) const;
    Json HandleAuthRefresh(const SdkCommandRequest& request) const;
    Json HandleAuthGetContext(const SdkCommandRequest& request) const;
    Json HandleDeviceList(const SdkCommandRequest& request) const;
    Json HandleDeviceGet(const SdkCommandRequest& request) const;
    Json HandleDeviceOpen(const SdkCommandRequest& request) const;
    Json HandleCaptureTake(const SdkCommandRequest& request, bool include_base64) const;
    Json HandleImageProcess(const SdkCommandRequest& request) const;
    Json HandleOcrRecognize(const SdkCommandRequest& request) const;
    Json HandleFileConvert(const SdkCommandRequest& request) const;
    Json HandleRecognitionBarcode(const SdkCommandRequest& request) const;
    Json BuildAuthContextJson(const AuthContext& auth_context) const;
    Json BuildMethodDescriptorJson(const SdkMethodDescriptor& descriptor) const;
    std::int64_t NowTs() const;

    SdkConfig config_;
    ProviderBundle providers_;
    StatusSupplier status_supplier_;
    std::vector<SdkMethodDescriptor> method_descriptors_;
};

} // namespace sdk
} // namespace editor
