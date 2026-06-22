// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_app.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#include <vector>
#else
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#include "sdk_logger.h"
#include "sdk_runtime_paths.h"
#include "sdk_open_version.h"

namespace editor {
namespace sdk {

namespace {
#if SDK_OPEN_ENABLE_HTTP_SERVER
constexpr bool kSdkOpenHttpServerEnabled = true;
#else
constexpr bool kSdkOpenHttpServerEnabled = false;
#endif

std::string BuildAssetBaseUrl(const SdkConfig& config) {
    if (!config.asset_base_url.empty()) {
        return config.asset_base_url;
    }
    const std::string host = config.bind_host == "0.0.0.0" ? "127.0.0.1" : config.bind_host;
    return "http://" + host + ":" + std::to_string(config.asset_http_port);
}

bool IsEnvConfigured(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

int CurrentProcessId() {
#if defined(_WIN32)
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

std::string CurrentExecutablePath() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return length == 0 ? std::string() : std::string(buffer, length);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    if (size == 0) {
        return "";
    }
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return "";
    }
    return std::string(buffer.data());
#else
    char buffer[4096] = {0};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    return length <= 0 ? std::string() : std::string(buffer, static_cast<std::size_t>(length));
#endif
}

uint64_t FileSize(const std::string& path) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

std::int64_t FileMtime(const std::string& path) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return static_cast<std::int64_t>(st.st_mtime);
}

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string ReadFileTail(const std::string& path, std::size_t tail_bytes) {
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        return "";
    }
    input.seekg(0, std::ios::end);
    const std::ifstream::pos_type end_pos = input.tellg();
    const std::streamoff size = end_pos < 0 ? 0 : static_cast<std::streamoff>(end_pos);
    const std::streamoff offset = size > static_cast<std::streamoff>(tail_bytes)
                                      ? size - static_cast<std::streamoff>(tail_bytes)
                                      : 0;
    input.seekg(offset, std::ios::beg);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

Json LogDescriptor(const std::string& id, const std::string& label, const std::string& path) {
    return Json{{"id", id},
                {"label", label},
                {"path", path},
                {"exists", FileExists(path)},
                {"size", FileSize(path)},
                {"mtime", FileMtime(path)}};
}

std::string LogPathForId(const std::string& id) {
    const std::string log_dir = GetSdkOpenLogDir();
    if (id == "sdk_open_runtime") {
        return GetSdkOpenLogPath();
    }
    if (id == "sdk_private_main") {
        return JoinPath(log_dir, "sdk_private_main.log");
    }
    if (id == "sdk_private_devices") {
        return JoinPath(log_dir, "sdk_private_devices.log");
    }
    if (id == "sdk_private_auth_manager") {
        return JoinPath(log_dir, "sdk_private_auth_manager.log");
    }
    if (id == "czcv_sdk") {
        return JoinPath(log_dir, "czcv_sdk.log");
    }
    return "";
}

std::string ReadFirstCpuModel() {
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line)) {
        const std::string key = "model name";
        if (line.find(key) != 0) {
            continue;
        }
        const std::string::size_type colon = line.find(':');
        if (colon != std::string::npos && colon + 1 < line.size()) {
            std::string model = line.substr(colon + 1);
            while (!model.empty() && model[0] == ' ') {
                model.erase(model.begin());
            }
            return model;
        }
    }
    return "";
}

Json ObjectField(const Json& obj, const char* key) {
    if (!obj.is_object()) {
        return Json::object();
    }
    const Json::const_iterator it = obj.find(key);
    return it != obj.end() && it->is_object() ? *it : Json::object();
}

std::string StringField(const Json& obj, const char* key) {
    if (!obj.is_object()) {
        return "";
    }
    const Json::const_iterator it = obj.find(key);
    if (it != obj.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return "";
}

std::string StringOrUnknown(const std::string& value) {
    return value.empty() ? "unknown" : value;
}

std::string JoinNonEmpty(const std::vector<std::string>& values, const std::string& separator) {
    std::string joined;
    for (std::vector<std::string>::const_iterator it = values.begin(); it != values.end(); ++it) {
        if (it->empty()) {
            continue;
        }
        if (!joined.empty()) {
            joined += separator;
        }
        joined += *it;
    }
    return joined;
}

std::string UrlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char ch = static_cast<unsigned char>(*it);
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded << static_cast<char>(ch);
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return encoded.str();
}

std::string BuildActivationQrUrl(const Json& session, const Json& system) {
    const std::string machine_code = StringField(session, "machineCode");
    if (machine_code.empty()) {
        return "";
    }

    const Json software = ObjectField(system, "software");
    const Json process = ObjectField(system, "process");
    const Json os = ObjectField(system, "os");
    const Json hardware = ObjectField(system, "hardware");
    const std::string os_label = JoinNonEmpty({StringField(os, "sysname"),
                                               StringField(os, "release"),
                                               StringField(os, "version")},
                                              " ");
    Json extra = Json{
        {"source", "sdk_open_admin"},
        {"platform", "SDK开放平台"},
        {"runtime", "sdk_open"},
        {"hostname", StringField(os, "nodename")},
        {"apiKeyTier", StringField(session, "accountType")},
        {"apiKeyTierCode", session.value("accountTypeCode", -1)},
        {"licensedTier", StringField(session, "licensedAccountType")},
        {"licensedTierCode", session.value("licensedAccountTypeCode", -1)},
        {"hardware",
         Json{{"cpuModel", StringField(hardware, "cpuModel")},
              {"cpuCores", hardware.value("cpuCores", 0)},
              {"memoryTotalBytes", hardware.value("memoryTotalBytes", static_cast<uint64_t>(0))}}},
        {"process",
         Json{{"pid", process.value("pid", 0)},
              {"executablePath", StringField(process, "executablePath")}}},
    };

    return std::string("https://rainbow.czur.com/mp/auth/code") +
           "?code=" + UrlEncode(machine_code) +
           "&arch=" + UrlEncode(StringOrUnknown(StringField(os, "machine"))) +
           "&os=" + UrlEncode(StringOrUnknown(os_label)) +
           "&kernel=" + UrlEncode(StringOrUnknown(StringField(os, "release"))) +
           "&sv=" + UrlEncode(StringOrUnknown(StringField(software, "version"))) +
           "&tier=" + UrlEncode(StringOrUnknown(StringField(session, "accountType"))) +
           "&tier_code=" + UrlEncode(std::to_string(session.value("accountTypeCode", -1))) +
           "&extra=" + UrlEncode(extra.dump());
}

} // namespace

SdkApp::SdkApp(const SdkConfig& config, ProviderBundle providers)
    : config_(config),
      providers_(std::move(providers)),
      runtime_config_service_(new RuntimeConfigService(config_.online_image_enhance_base_url,
                                                       config_.authz_base_url)),
      runtime_record_service_(new RuntimeRecordService()),
      command_application_service_(new CommandApplicationService(config_, providers_, runtime_config_service_)),
      admin_application_service_(runtime_config_service_),
      admin_http_server_(
          "admin-site", config_.bind_host, config_.admin_http_port, config_.web_root + "/admin", config_.auth_token),
      demo_http_server_(
          "demo-site", config_.bind_host, config_.demo_http_port, config_.web_root + "/demo", config_.auth_token),
      asset_http_server_(
          "asset-api", config_.bind_host, config_.asset_http_port, "", config_.auth_token, false),
      command_ws_server_(config_.bind_host, config_.command_ws_port),
      video_ws_server_(config_.bind_host, config_.video_ws_port),
      running_(false),
      start_time_(std::chrono::steady_clock::now()) {
#if defined(SDK_USE_PRIVATE_PROVIDER) && defined(_WIN32)
    provider_names_ = Json{{"device", "czur-device-provider"},
                           {"graphic", "czur-graphic-provider"},
                           {"imageEnhance", "czur-image-enhance-provider"},
                           {"ocr", "czur-ocr-provider"},
                           {"ofd", "czur-ofd-provider"},
                           {"auth", "czur-sdk-auth-provider"},
                           {"recognition", "czur-recognition-provider"},
                           {"sane", "czur-sane-provider"}};
#else
    provider_names_ = Json{{"device", providers_.device_provider ? providers_.device_provider->ProviderName() : ""},
                           {"graphic", providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : ""},
                           {"imageEnhance",
                            providers_.image_enhance_provider ? providers_.image_enhance_provider->ProviderName() : ""},
                           {"ocr", providers_.ocr_provider ? providers_.ocr_provider->ProviderName() : ""},
                           {"ofd", providers_.ofd_provider ? providers_.ofd_provider->ProviderName() : ""},
                           {"auth", providers_.auth_provider ? providers_.auth_provider->ProviderName() : ""},
                           {"recognition",
                            providers_.recognition_provider ? providers_.recognition_provider->ProviderName() : ""},
                           {"sane", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}};
#endif
    command_application_service_->SetProviderNames(provider_names_);
    admin_application_service_.SetStatusSupplier([this]() { return BuildStatusJson(); });
    admin_application_service_.SetSystemSupplier([this]() { return BuildSystemJson(); });
    admin_application_service_.SetAuthSupplier([this]() { return BuildAuthJson(); });
    admin_application_service_.SetOfflineActivationHandler(
        [this](const std::string& connection_id, const Json& request) {
            return ActivateOfflineSessionJson(connection_id, request);
        });
    admin_application_service_.SetLogsSupplier([this]() { return BuildLogsJson(); });
    admin_application_service_.SetLogReadHandler([this](const std::string& log_id, std::size_t tail_bytes) {
        return BuildLogReadJson(log_id, tail_bytes);
    });
    admin_application_service_.SetRecordsSupplier([this]() { return BuildRecordsJson(); });
    if (kSdkOpenHttpServerEnabled) {
        admin_http_server_.SetHealthSupplier([this]() { return admin_application_service_.BuildHealthJson(); });
        admin_http_server_.SetStatusSupplier([this]() { return admin_application_service_.BuildStatusJson(); });
        admin_http_server_.SetSystemSupplier([this]() { return admin_application_service_.BuildSystemJson(); });
        admin_http_server_.SetAuthSupplier([this]() { return admin_application_service_.BuildAuthJson(); });
        admin_http_server_.SetOfflineActivationHandler([this](const std::string& connection_id, const Json& request) {
            return admin_application_service_.ActivateOfflineSessionJson(connection_id, request);
        });
        admin_http_server_.SetLogsSupplier([this]() { return admin_application_service_.BuildLogsJson(); });
        admin_http_server_.SetLogReadHandler([this](const std::string& log_id, std::size_t tail_bytes) {
            return admin_application_service_.BuildLogReadJson(log_id, tail_bytes);
        });
        admin_http_server_.SetRecordsSupplier([this]() { return admin_application_service_.BuildRecordsJson(); });
        admin_http_server_.SetConfigSupplier([this]() { return admin_application_service_.BuildConfigJson(); });
        admin_http_server_.SetConfigUpdateHandler([this](const Json& request) {
            return admin_application_service_.UpdateConfigJson(request);
        });
        demo_http_server_.SetHealthSupplier([this]() { return admin_application_service_.BuildHealthJson(); });
        demo_http_server_.SetStatusSupplier([this]() { return admin_application_service_.BuildStatusJson(); });
    }
    asset_http_server_.SetHealthSupplier([this]() { return admin_application_service_.BuildHealthJson(); });
    SdkHttpServer::AssetResolver asset_resolver =
            [this](const std::string& session_token, const std::string& task_id, const std::string& asset_id) {
                SdkHttpServer::AssetResult result;
                if (!command_application_service_) {
                    result.code = ToCode(SdkStatusCode::InternalError);
                    result.message = "command service not ready";
                    return result;
                }
                const CommandApplicationService::AssetAccessResult asset_result =
                        command_application_service_->ResolveAsset(session_token, task_id, asset_id);
                result.code = asset_result.code;
                result.message = asset_result.message;
                result.asset = asset_result.asset;
                return result;
            };
    asset_http_server_.SetAssetResolver(asset_resolver);
    asset_http_server_.SetImageUploadHandler(
        [this](const std::string& session_token,
               const std::string& filename,
               const std::string& content_type,
               const std::string& content) {
            SdkHttpServer::UploadResult result;
            if (!command_application_service_) {
                result.code = ToCode(SdkStatusCode::InternalError);
                result.message = "command service not ready";
                return result;
            }
            const CommandApplicationService::ImageUploadResult upload_result =
                command_application_service_->UploadImage(session_token, filename, content_type, content);
            result.code = upload_result.code;
            result.message = upload_result.message;
            result.body = Json{{"upload_id", upload_result.upload_id},
                               {"asset",
                                Json{{"asset_id", upload_result.asset.asset_id},
                                     {"kind", upload_result.asset.kind},
                                     {"path", upload_result.asset.path},
                                     {"url", upload_result.asset.url},
                                     {"download_url", upload_result.asset.download_url},
                                     {"content_type", upload_result.asset.content_type},
                                     {"width", upload_result.asset.width},
                                     {"height", upload_result.asset.height},
                                     {"size", upload_result.asset.size}}}};
            return result;
        });
    command_application_service_->SetStatusSupplier([this]() { return BuildStatusJson(); });
    command_application_service_->SetVideoFrameSink([this](const SdkVideoFrame& frame) {
        if (runtime_record_service_) {
            runtime_record_service_->RecordRuntimeEvent("video_ws",
                                                        "",
                                                        "stream.frame",
                                                        "video frame published",
                                                        0,
                                                        Json{{"stream_id", frame.stream_id},
                                                             {"frame_seq", frame.frame_seq},
                                                             {"bytes", frame.payload.size()}});
        }
        video_ws_server_.PublishFrame(frame);
    });
    command_application_service_->SetVideoStreamClosedSink([this](const std::string& stream_id) {
        if (runtime_record_service_) {
            runtime_record_service_->RecordRuntimeEvent("video_ws",
                                                        "",
                                                        "stream.closed",
                                                        "video stream closed",
                                                        0,
                                                        Json{{"stream_id", stream_id}});
        }
        video_ws_server_.CloseStream(stream_id);
    });
    command_application_service_->SetCommandEventSink([this](const std::string& connection_id, const Json& event) {
        if (runtime_record_service_) {
            runtime_record_service_->RecordRuntimeEvent("command_ws",
                                                        connection_id,
                                                        event.value("event", "event"),
                                                        event.value("message", "ok"),
                                                        event.value("code", 0),
                                                        event);
        }
        command_ws_server_.SendEvent(connection_id, event);
    });
    command_ws_server_.SetOpenHandler([this](const std::string& connection_id) {
        if (runtime_record_service_) {
            runtime_record_service_->RecordRuntimeEvent("command_ws",
                                                        connection_id,
                                                        "connection.open",
                                                        "command websocket connected");
        }
    });
    command_ws_server_.SetRequestHandler(
        [this](const std::string& connection_id, const Json& request) {
            const auto begin = std::chrono::steady_clock::now();
            Json response = command_application_service_->HandleRequest(connection_id, request);
            const auto elapsed = std::chrono::steady_clock::now() - begin;
            const uint64_t duration_ms =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
            if (runtime_record_service_) {
                runtime_record_service_->RecordCommandRequest(connection_id, request, response, duration_ms);
            }
            return response;
        });
    command_ws_server_.SetCloseHandler([this](const std::string& connection_id) {
        if (runtime_record_service_) {
            runtime_record_service_->RecordRuntimeEvent("command_ws",
                                                        connection_id,
                                                        "connection.close",
                                                        "command websocket disconnected");
        }
        if (command_application_service_) {
            command_application_service_->OnConnectionClosed(connection_id);
        }
    });
    video_ws_server_.SetConnectionAuthHandler([this](const std::string& session_token, const std::string& stream_id) {
        SdkWsVideoServer::ConnectionAuthResult result;
        const VideoSessionService::ValidationResult validation =
            command_application_service_->ValidateVideoStream(session_token, stream_id);
        result.authorized = validation.authorized;
        result.code = validation.code;
        result.message = validation.message;
        result.connection_id = validation.binding.connection_id;
        result.stream_id = validation.binding.stream_id;
        if (runtime_record_service_) {
            runtime_record_service_->RecordRuntimeEvent("video_ws",
                                                        result.connection_id,
                                                        result.authorized ? "connection.authorized" : "connection.rejected",
                                                        result.message,
                                                        result.code,
                                                        Json{{"stream_id", stream_id}});
        }
        return result;
    });
}

bool SdkApp::Start() {
    if (running_.load()) {
        return true;
    }

    SDK_OPEN_LOG_INFO("[sdk_app] starting...");
    if (kSdkOpenHttpServerEnabled) {
        if (!admin_http_server_.Start()) {
            return false;
        }
        if (!demo_http_server_.Start()) {
            admin_http_server_.Stop();
            return false;
        }
    } else {
        SDK_OPEN_LOG_INFO("[sdk_app] embedded http server disabled by SDK_OPEN_ENABLE_HTTP_SERVER=0");
    }
    if (!asset_http_server_.Start()) {
        demo_http_server_.Stop();
        admin_http_server_.Stop();
        return false;
    }
    if (!command_ws_server_.Start()) {
        asset_http_server_.Stop();
        if (kSdkOpenHttpServerEnabled) {
            demo_http_server_.Stop();
            admin_http_server_.Stop();
        }
        return false;
    }
    if (!video_ws_server_.Start()) {
        command_ws_server_.Stop();
        asset_http_server_.Stop();
        if (kSdkOpenHttpServerEnabled) {
            demo_http_server_.Stop();
            admin_http_server_.Stop();
        }
        return false;
    }

    start_time_ = std::chrono::steady_clock::now();
    running_.store(true);
    SDK_OPEN_LOG_INFO("[sdk_app] started");
    return true;
}

void SdkApp::Stop() {
    if (!running_.load()) {
        return;
    }

    SDK_OPEN_LOG_INFO("[sdk_app] stopping...");
    video_ws_server_.Stop();
    command_ws_server_.Stop();
    if (kSdkOpenHttpServerEnabled) {
        asset_http_server_.Stop();
        demo_http_server_.Stop();
        admin_http_server_.Stop();
    }
    running_.store(false);
    SDK_OPEN_LOG_INFO("[sdk_app] stopped");
}

bool SdkApp::IsRunning() const {
    return running_.load();
}

uint64_t SdkApp::UptimeSeconds() const {
    if (!running_.load()) {
        return 0;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start_time_;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
}

Json SdkApp::BuildStatusJson() const {
    const SdkWsCommandServer::Stats command_stats = command_ws_server_.GetStats();
    const SdkWsVideoServer::Stats video_stats = video_ws_server_.GetStats();

    return Json{
        {"running", running_.load()},
        {"uptimeSec", UptimeSeconds()},
        {"bindHost", config_.bind_host},
        {"process",
         {
             {"pid", CurrentProcessId()},
             {"executablePath", CurrentExecutablePath()},
         }},
        {"http",
         {
             {"enabled", kSdkOpenHttpServerEnabled},
             {"sites",
              {
                  {"admin", config_.admin_http_port},
                  {"demo", config_.demo_http_port},
                  {"asset", config_.asset_http_port},
              }},
             {"assetBaseUrl", BuildAssetBaseUrl(config_)},
         }},
        {"ports",
         {
             {"adminHttp", config_.admin_http_port},
             {"demoHttp", config_.demo_http_port},
             {"assetHttp", config_.asset_http_port},
             {"commandWs", config_.command_ws_port},
             {"videoWs", config_.video_ws_port},
         }},
        {"providers", provider_names_},
        {"authDiagnostics",
         {
             {"authzBaseUrlConfigured", IsEnvConfigured("CZUR_SDK_AUTHZ_BASE_URL")},
             {"imageEnhanceBaseUrlConfigured", IsEnvConfigured("CZUR_SDK_IMAGE_ENHANCE_BASE_URL")},
         }},
        {"ws",
         {
             {"command",
              {
                  {"activeConnections", command_stats.active_connections},
                  {"authFailed", command_stats.auth_failed},
                  {"requestCount", command_stats.request_count},
              }},
             {"video",
              {
                  {"activeConnections", video_stats.active_connections},
                  {"authFailed", video_stats.auth_failed},
                  {"binaryFrames", video_stats.binary_frames},
                  {"binaryBytes", video_stats.binary_bytes},
              }},
         }},
        {"application",
         {
             {"boundSessions", command_application_service_ ? command_application_service_->ActiveSessionCount() : 0},
             {"activeStreams", command_application_service_ ? command_application_service_->ActiveStreamCount() : 0},
         }},
    };
}

Json SdkApp::BuildSystemJson() const {
    Json os = Json::object();
    Json hardware = Json::object();
#if defined(__linux__)
    struct utsname uts;
    if (::uname(&uts) == 0) {
        os = Json{{"sysname", uts.sysname},
                  {"release", uts.release},
                  {"version", uts.version},
                  {"machine", uts.machine},
                  {"nodename", uts.nodename}};
    }
    struct sysinfo info;
    if (::sysinfo(&info) == 0) {
        hardware["memoryTotalBytes"] = static_cast<uint64_t>(info.totalram) * info.mem_unit;
        hardware["memoryFreeBytes"] = static_cast<uint64_t>(info.freeram) * info.mem_unit;
        hardware["uptimeSec"] = static_cast<uint64_t>(info.uptime);
    }
    hardware["cpuModel"] = ReadFirstCpuModel();
    hardware["cpuCores"] = static_cast<int>(::sysconf(_SC_NPROCESSORS_ONLN));
    struct statvfs vfs;
    if (::statvfs(GetSdkOpenWorkDir().c_str(), &vfs) == 0) {
        hardware["workDirDiskTotalBytes"] = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
        hardware["workDirDiskFreeBytes"] = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
    }
#endif

    return Json{
        {"software",
         Json{{"name", "sdk_open"},
              {"version", SDK_OPEN_VERSION_STRING},
              {"build", SDK_OPEN_BUILD_NUMBER},
              {"stage", SDK_OPEN_STAGE},
              {"interval", SDK_OPEN_INTERVAL},
              {"protocolVersion", "2.0.0"},
              {"buildDate", SDK_OPEN_BUILD_DATE},
              {"buildTime", SDK_OPEN_BUILD_TIME},
              {"gitCommit", SDK_OPEN_GIT_COMMIT_HASH}}},
        {"process",
         Json{{"pid", CurrentProcessId()},
              {"executablePath", CurrentExecutablePath()},
              {"uptimeSec", UptimeSeconds()}}},
        {"os", os},
        {"hardware", hardware},
        {"runtime",
         Json{{"workDir", GetSdkOpenWorkDir()},
              {"logDir", GetSdkOpenLogDir()},
              {"webRoot", config_.web_root},
              {"httpEnabled", kSdkOpenHttpServerEnabled}}},
        {"ports",
         Json{{"adminHttp", config_.admin_http_port},
              {"demoHttp", config_.demo_http_port},
              {"assetHttp", config_.asset_http_port},
              {"commandWs", config_.command_ws_port},
              {"videoWs", config_.video_ws_port}}},
    };
}

Json SdkApp::BuildAuthJson() const {
    Json auth = command_application_service_ ? command_application_service_->BuildAdminAuthJson()
                                             : Json{{"providers", Json::object()}, {"sessions", Json::array()}};
    Json system = BuildSystemJson();
    Json::iterator sessions_it = auth.find("sessions");
    if (sessions_it == auth.end() || !sessions_it->is_array()) {
        return auth;
    }
    for (Json::iterator session_it = sessions_it->begin(); session_it != sessions_it->end(); ++session_it) {
        if (!session_it->is_object() || !session_it->value("offlineActivationRequired", false)) {
            continue;
        }
        (*session_it)["activationQrUrl"] = BuildActivationQrUrl(*session_it, system);
    }
    return auth;
}

Json SdkApp::ActivateOfflineSessionJson(const std::string& connection_id, const Json& request) {
    return command_application_service_ ? command_application_service_->ActivateOfflineForAdmin(connection_id, request)
                                        : BuildErrorBody(SdkStatusCode::InternalError, "command service not ready");
}

Json SdkApp::BuildLogsJson() const {
    Json logs = Json::array();
    logs.push_back(LogDescriptor("sdk_open_runtime", "SDK Open Runtime", LogPathForId("sdk_open_runtime")));
    logs.push_back(LogDescriptor("sdk_private_main", "SDK Private Main", LogPathForId("sdk_private_main")));
    logs.push_back(LogDescriptor("sdk_private_devices", "SDK Private Devices", LogPathForId("sdk_private_devices")));
    logs.push_back(LogDescriptor("sdk_private_auth_manager",
                                 "SDK Private Auth Manager",
                                 LogPathForId("sdk_private_auth_manager")));
    return Json{{"logDir", GetSdkOpenLogDir()}, {"logs", logs}};
}

Json SdkApp::BuildLogReadJson(const std::string& log_id, std::size_t tail_bytes) const {
    const std::string path = LogPathForId(log_id);
    if (path.empty()) {
        return BuildErrorBody(SdkStatusCode::InvalidParams, "unknown log id");
    }
    if (!FileExists(path)) {
        return BuildErrorBody(SdkStatusCode::InvalidParams, "log file not found", Json{{"id", log_id}, {"path", path}});
    }
    return Json{{"code", ToCode(SdkStatusCode::Ok)},
                {"message", "ok"},
                {"id", log_id},
                {"path", path},
                {"size", FileSize(path)},
                {"mtime", FileMtime(path)},
                {"tailBytes", tail_bytes},
                {"content", ReadFileTail(path, tail_bytes)}};
}

Json SdkApp::BuildRecordsJson() const {
    return runtime_record_service_ ? runtime_record_service_->BuildRecordsJson("", 100)
                                   : Json{{"records", Json::array()}};
}

} // namespace sdk
} // namespace editor
