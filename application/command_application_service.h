// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "admin_application_service.h"
#include "authorization_service.h"
#include "capture_task_service.h"
#include "device_facade.h"
#include "graphic_facade.h"
#include "image_enhance_task_service.h"
#include "ocr_facade.h"
#include "ofd_facade.h"
#include "recognition_facade.h"
#include "runtime_config_service.h"
#include "sane_facade.h"
#include "sdk_config.h"
#include "sdk_json_utils.h"
#include "sdk_provider_bundle.h"
#include "video_session_service.h"

namespace editor {
namespace sdk {

class CommandApplicationService {
public:
    using StatusSupplier = std::function<Json()>;
    using VideoFrameSink = std::function<void(const SdkVideoFrame&)>;
    using VideoStreamClosedSink = std::function<void(const std::string&)>;
    using CommandEventSink = std::function<void(const std::string&, const Json&)>;

    struct MethodDescriptor {
        std::string method;
        bool requires_session = false;
        std::string summary;
    };

    struct AssetAccessResult {
        int code = ToCode(SdkStatusCode::Ok);
        std::string message = "ok";
        SdkCaptureAsset asset;
    };

    struct ImageUploadResult {
        int code = ToCode(SdkStatusCode::Ok);
        std::string message = "ok";
        std::string upload_id;
        SdkCaptureAsset asset;
    };

    explicit CommandApplicationService(const SdkConfig& config,
                                       const ProviderBundle& providers,
                                       std::shared_ptr<RuntimeConfigService> runtime_config = std::shared_ptr<RuntimeConfigService>());

    void SetProviderNames(const Json& provider_names);
    void SetStatusSupplier(StatusSupplier supplier);
    void SetVideoFrameSink(VideoFrameSink sink);
    void SetVideoStreamClosedSink(VideoStreamClosedSink sink);
    void SetCommandEventSink(CommandEventSink sink);
    Json HandleRequest(const std::string& connection_id, const Json& request_json);
    void OnConnectionClosed(const std::string& connection_id);
    Json BuildCapabilitiesJson() const;
    VideoSessionService::ValidationResult ValidateVideoStream(const std::string& session_token,
                                                              const std::string& stream_id) const;
    AssetAccessResult ResolveAsset(const std::string& session_token,
                                   const std::string& task_id,
                                   const std::string& asset_id) const;
    ImageUploadResult UploadImage(const std::string& session_token,
                                  const std::string& filename,
                                  const std::string& content_type,
                                  const std::string& content);
    std::size_t ActiveSessionCount() const;
    std::size_t ActiveStreamCount() const;
    Json BuildAdminAuthJson() const;
    Json ActivateOfflineForAdmin(const std::string& connection_id, const Json& request);

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
    Json HandleDeviceClose(const std::string& connection_id, const Request& request);
    Json HandleCaptureTake(const std::string& connection_id, const Request& request);
    Json HandleCaptureGet(const std::string& connection_id, const Request& request);
    Json HandleVideoStart(const std::string& connection_id, const Request& request);
    Json HandleVideoStop(const std::string& connection_id, const Request& request);
    Json HandleVideoSetFormat(const std::string& connection_id, const Request& request);
    Json HandleVideoSetProfile(const std::string& connection_id, const Request& request);
    Json HandleImageProcess(const std::string& connection_id, const Request& request);
    Json HandleImageProcessPage(const std::string& connection_id, const Request& request);
    Json HandleImageApplyColorMode(const std::string& connection_id, const Request& request);
    Json HandleImageEnhanceCapabilities(const std::string& connection_id, const Request& request);
    Json HandleImageEnhance(const std::string& connection_id, const Request& request);
    Json HandleImageEnhanceGet(const std::string& connection_id, const Request& request);
    Json HandleImageEnhanceCancel(const std::string& connection_id, const Request& request);
    Json HandleImageEnhanceWorkflowList(const std::string& connection_id, const Request& request);
    Json HandleImageEnhanceWorkflowGet(const std::string& connection_id, const Request& request);
    Json HandleImageEnhanceWorkflowSave(const std::string& connection_id, const Request& request);
    Json HandleImageEnhanceWorkflowDelete(const std::string& connection_id, const Request& request);
    Json HandleOcrRecognize(const std::string& connection_id, const Request& request);
    Json HandleOcrGet(const std::string& connection_id, const Request& request);
    Json HandleOcrCancel(const std::string& connection_id, const Request& request);
    Json HandleOcrExtractText(const std::string& connection_id, const Request& request);
    Json HandleBarcodeDetect(const std::string& connection_id, const Request& request);
    Json HandleFileConvert(const std::string& connection_id, const Request& request);
    Json HandleSaneStatus(const std::string& connection_id, const Request& request);
    Json HandleSaneList(const std::string& connection_id, const Request& request);
    Json HandleSaneWatchStart(const std::string& connection_id, const Request& request);
    Json HandleSaneWatchStop(const std::string& connection_id, const Request& request);
    Json HandleSaneOpen(const std::string& connection_id, const Request& request);
    Json HandleSaneClose(const std::string& connection_id, const Request& request);
    Json HandleSaneGetOptions(const std::string& connection_id, const Request& request);
    Json HandleSaneSetOptions(const std::string& connection_id, const Request& request);
    Json HandleSaneProfileList(const std::string& connection_id, const Request& request);
    Json HandleSaneProfileSave(const std::string& connection_id, const Request& request);
    Json HandleSaneProfileApply(const std::string& connection_id, const Request& request);
    Json HandleSaneProfileDelete(const std::string& connection_id, const Request& request);
    Json HandleSaneScan(const std::string& connection_id, const Request& request);
    Json HandleSaneScanGet(const std::string& connection_id, const Request& request);
    Json HandleSaneScanCancel(const std::string& connection_id, const Request& request);

    AuthorizationService::SessionResult RequireCapability(const std::string& connection_id,
                                                          const std::string& capability) const;
    AuthorizationService::SessionResult ConsumeQuota(const std::string& connection_id,
                                                     const std::string& capability,
                                                     const std::string& request_id,
                                                     int units = 1);
    Json BuildSessionJson(const AuthorizationService::SessionResult& session_result) const;
    Json BuildAdminSessionJson(const AuthorizationService::SessionResult& session_result) const;
    Json BuildAuthContextJson(const AuthContext& auth_context) const;
    Json BuildDeviceJson(const SdkDeviceDescriptor& device) const;
    const MethodDescriptor* FindMethod(const std::string& method) const;
    void DispatchSaneDeviceEvent(const SdkSaneDeviceEvent& event);
    void DispatchSaneScanTaskEvent(const SdkSaneScanTaskEvent& event);
    void RememberOpenedDevice(const std::string& connection_id, const std::string& device_id);
    void ForgetOpenedDevice(const std::string& connection_id, const std::string& device_id);
    std::vector<std::string> ClearOpenedDevices(const std::string& connection_id);
    std::string NextImageTaskId();
    SdkCaptureAsset AttachImageAssetUrls(const std::string& task_id, const SdkCaptureAsset& asset) const;
    void RegisterImageAsset(const std::string& connection_id, const std::string& task_id, const SdkCaptureAsset& asset);
    bool ResolveImageInputAsset(const std::string& connection_id,
                                const std::string& input_upload_id,
                                const std::string& request_id,
                                const char* method_name,
                                std::string* input_path,
                                std::string* source_extension,
                                Json* error_response) const;
    bool ResolveImageInputPath(const std::string& connection_id,
                               const std::string& input_upload_id,
                               const std::string& request_id,
                               const char* method_name,
                               std::string* input_path,
                               Json* error_response) const;
    AssetAccessResult ResolveImageAsset(const std::string& connection_id,
                                        const std::string& task_id,
                                        const std::string& asset_id) const;

    SdkConfig config_;
    std::shared_ptr<RuntimeConfigService> runtime_config_;
    mutable std::mutex command_event_sink_mu_;
    CommandEventSink command_event_sink_;
    ProviderBundle providers_;
    Json provider_names_;
    AuthorizationService authorization_service_;
    VideoSessionService video_session_service_;
    DeviceFacade device_facade_;
    GraphicFacade graphic_facade_;
    OcrFacade ocr_facade_;
    OfdFacade ofd_facade_;
    RecognitionFacade recognition_facade_;
    SaneFacade sane_facade_;
    CaptureTaskService capture_task_service_;
    ImageEnhanceTaskService image_enhance_task_service_;
    StatusSupplier status_supplier_;
    VideoFrameSink video_frame_sink_;
    VideoStreamClosedSink video_stream_closed_sink_;
    std::vector<MethodDescriptor> methods_;
    std::atomic<uint64_t> next_image_task_seq_;
    mutable std::mutex image_assets_mu_;
    std::map<std::string, std::string> image_asset_connection_by_task_;
    std::map<std::string, std::map<std::string, SdkCaptureAsset> > image_assets_by_task_;
    mutable std::mutex sane_tasks_mu_;
    std::map<std::string, SdkSaneScanTask> sane_tasks_;
    std::map<std::string, SdkImageEnhancePipeline> sane_pipelines_by_task_;
    std::map<std::string, std::string> sane_online_api_keys_by_task_;
    std::map<std::string, std::string> sane_online_base_urls_by_task_;
    mutable std::mutex opened_devices_mu_;
    std::map<std::string, std::set<std::string> > opened_devices_by_connection_;
};

} // namespace sdk
} // namespace editor
