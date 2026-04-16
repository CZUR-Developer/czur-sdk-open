// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "admin_application_service.h"
#include "authorization_service.h"
#include "device_facade.h"
#include "graphic_facade.h"
#include "ocr_facade.h"
#include "ofd_facade.h"
#include "sdk_config.h"
#include "sdk_json_utils.h"
#include "sdk_provider_bundle.h"
#include "video_session_service.h"

namespace editor {
namespace sdk {

class CommandApplicationService {
public:
    using StatusSupplier = std::function<Json()>;

    struct MethodDescriptor {
        std::string method;
        bool requires_session = false;
        std::string summary;
    };

    explicit CommandApplicationService(const SdkConfig& config, const ProviderBundle& providers);

    void SetStatusSupplier(StatusSupplier supplier);
    Json HandleRequest(const std::string& connection_id, const Json& request_json);
    void OnConnectionClosed(const std::string& connection_id);
    Json BuildCapabilitiesJson() const;
    VideoSessionService::ValidationResult ValidateVideoStream(const std::string& session_token,
                                                              const std::string& stream_id) const;
    std::size_t ActiveSessionCount() const;
    std::size_t ActiveStreamCount() const;

private:
    struct Request {
        std::string request_id;
        std::string method;
        Json params = Json::object();
        Json client = Json::object();
    };

    Json HandleSystemPing(const Request& request) const;
    Json HandleSystemInfo(const Request& request) const;
    Json HandleSystemCapabilities(const Request& request) const;
    Json HandleAuthCreateSession(const std::string& connection_id, const Request& request);
    Json HandleAuthGetContext(const std::string& connection_id, const Request& request);
    Json HandleAuthRefreshSession(const std::string& connection_id, const Request& request);
    Json HandleAuthActivateOffline(const std::string& connection_id, const Request& request);
    Json HandleAuthDestroySession(const std::string& connection_id, const Request& request);
    Json HandleDeviceList(const std::string& connection_id, const Request& request);
    Json HandleDeviceGet(const std::string& connection_id, const Request& request);
    Json HandleDeviceOpen(const std::string& connection_id, const Request& request);
    Json HandleCaptureTake(const std::string& connection_id, const Request& request);
    Json HandleVideoStart(const std::string& connection_id, const Request& request);
    Json HandleVideoStop(const std::string& connection_id, const Request& request);
    Json HandleVideoSetFormat(const std::string& connection_id, const Request& request);
    Json HandleImageProcess(const std::string& connection_id, const Request& request);
    Json HandleOcrRecognize(const std::string& connection_id, const Request& request);
    Json HandleFileConvert(const std::string& connection_id, const Request& request);

    AuthorizationService::SessionResult RequireCapability(const std::string& connection_id,
                                                          const std::string& capability) const;
    AuthorizationService::SessionResult ConsumeQuota(const std::string& connection_id,
                                                     const std::string& capability,
                                                     const std::string& request_id,
                                                     int units = 1);
    Json BuildSessionJson(const AuthorizationService::SessionResult& session_result) const;
    Json BuildAuthContextJson(const AuthContext& auth_context) const;
    const MethodDescriptor* FindMethod(const std::string& method) const;

    SdkConfig config_;
    ProviderBundle providers_;
    AuthorizationService authorization_service_;
    VideoSessionService video_session_service_;
    DeviceFacade device_facade_;
    GraphicFacade graphic_facade_;
    OcrFacade ocr_facade_;
    OfdFacade ofd_facade_;
    StatusSupplier status_supplier_;
    std::vector<MethodDescriptor> methods_;
};

} // namespace sdk
} // namespace editor
