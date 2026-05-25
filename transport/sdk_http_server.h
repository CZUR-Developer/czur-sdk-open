// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "sdk_provider_types.h"
#include "sdk_json_utils.h"

namespace httplib {
class Server;
}

namespace editor {
namespace sdk {

class SdkHttpServer {
public:
    using JsonSupplier = std::function<Json()>;
    struct AssetResult {
        int code = ToCode(SdkStatusCode::Ok);
        std::string message = "ok";
        SdkCaptureAsset asset;
    };
    struct UploadResult {
        int code = ToCode(SdkStatusCode::Ok);
        std::string message = "ok";
        Json body = Json::object();
    };
    using AssetResolver = std::function<AssetResult(const std::string&, const std::string&, const std::string&)>;
    using ImageUploadHandler = std::function<UploadResult(const std::string&, const std::string&, const std::string&, const std::string&)>;
    using JsonUpdateHandler = std::function<Json(const Json&)>;
    using OfflineActivationHandler = std::function<Json(const std::string&, const Json&)>;
    using LogReadHandler = std::function<Json(const std::string&, std::size_t)>;

    SdkHttpServer(const std::string& site_name,
                  const std::string& host,
                  int port,
                  const std::string& document_root,
                  const std::string& auth_token,
                  bool mount_static_site = true);
    ~SdkHttpServer();

    void SetHealthSupplier(JsonSupplier supplier);
    void SetStatusSupplier(JsonSupplier supplier);
    void SetSystemSupplier(JsonSupplier supplier);
    void SetAuthSupplier(JsonSupplier supplier);
    void SetLogsSupplier(JsonSupplier supplier);
    void SetLogReadHandler(LogReadHandler handler);
    void SetRecordsSupplier(JsonSupplier supplier);
    void SetConfigSupplier(JsonSupplier supplier);
    void SetConfigUpdateHandler(JsonUpdateHandler handler);
    void SetOfflineActivationHandler(OfflineActivationHandler handler);
    void SetAssetResolver(AssetResolver resolver);
    void SetImageUploadHandler(ImageUploadHandler handler);
    bool Start();
    void Stop();

private:
    bool IsAuthorized(const std::string& authorization) const;
    bool ConfigureRoutes();

    std::string site_name_;
    std::string host_;
    int port_;
    std::string document_root_;
    std::string auth_token_;
    bool mount_static_site_;
    JsonSupplier health_supplier_;
    JsonSupplier status_supplier_;
    JsonSupplier system_supplier_;
    JsonSupplier auth_supplier_;
    JsonSupplier logs_supplier_;
    LogReadHandler log_read_handler_;
    JsonSupplier records_supplier_;
    JsonSupplier config_supplier_;
    JsonUpdateHandler config_update_handler_;
    OfflineActivationHandler offline_activation_handler_;
    AssetResolver asset_resolver_;
    ImageUploadHandler image_upload_handler_;
    std::atomic<bool> running_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
};

} // namespace sdk
} // namespace editor
