// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "private_sdk_auth_service.h"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "auth_manager.h"
#if !defined(SDK_OPEN_HARMONY)
#include "device_manager.h"
#endif
#include "private_sdk_support.h"
#include "support/private_sdk_executable_path.h"
#include "support/private_sdk_logger.h"
#include "sdk_entitlement_policy.h"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

const char kPluginScene[] = "plugin";
const char kOfflineApiKeyMode[] = "offline_api_key";
const char kOnlineApiKeyMode[] = "online_api_key";
const char kOfflineEntitlementLimited[] = "offline_limited";
const char kOfflineEntitlementTrial[] = "offline_trial";
const char kOfflineEntitlementUnlocked[] = "offline_unlocked";
const char kOnlineEntitlementValidated[] = "online_validated";
const char kOnlineEntitlementTrial[] = "online_trial";
const char kTrialVipQuotaBucket[] = "trial_vip";
const char kHostAuthModeActivationRequired[] = "activation_required";
const char kOfflineStoreFileName[] = "offline_entitlements.json";
const char kAuthzBaseUrlEnv[] = "CZUR_SDK_AUTHZ_BASE_URL";
const char kOnlineAuthTimeoutEnv[] = "CZUR_SDK_ONLINE_AUTH_TIMEOUT_MS";
const char kAuthClientDevicesJsonEnv[] = "CZUR_SDK_AUTH_CLIENT_DEVICES_JSON";
const char kDefaultAuthzBaseUrl[] = "https://gateway.czur.com";
const char kAuthzValidatePath[] = "/api/v1/capeditor/open/authz/validate";
const char kAuthzConsumeConfirmPath[] = "/api/v1/capeditor/open/authz/consume-confirm";
const char kSessionAuthzCapability[] = "image.enhance";
const char kClientMetadataVersion[] = "capeditor.open.authz.client.v1";
const int kDefaultQuotaLimit = 100;
const int kTrialQuotaLimit = 200;

struct OfflineQuotaUsage {
    int used = 0;
    std::set<std::string> request_ids;
};

struct OfflineEntitlementRecord {
    std::map<std::string, OfflineQuotaUsage> quota_usage;
    bool host_unlocked = false;
    int host_auth_type = -1;
    SdkAccountType host_account_type = SdkAccountType::Unknown;
};

struct HiddenPluginAuthState {
    bool unlocked = false;
    int auth_type = -1;
    SdkAccountType account_type = SdkAccountType::Unknown;
};

struct OnlineCallResult {
    int code = ToCode(SdkStatusCode::OnlineAuthUnavailable);
    std::string message = "online auth unavailable";
    bool allowed = false;
    nlohmann::json body = nlohmann::json::object();
};

std::string GetOfflineStorePath() {
    return GetSdkRuntimeDir() + "/" + kOfflineStoreFileName;
}

bool IsOfflineMode(const std::string& license_mode) {
    return license_mode == kOfflineApiKeyMode;
}

bool IsOnlineMode(const std::string& license_mode) {
    return license_mode == kOnlineApiKeyMode;
}

bool RequiresHostActivation(const AuthContext& auth_context) {
    return auth_context.host_auth_mode == kHostAuthModeActivationRequired;
}

std::string DefaultOfflineEntitlementState(const ParsedApiKey& parsed) {
    return RequiresHostActivation(parsed.auth_context) ? kOfflineEntitlementTrial : kOfflineEntitlementLimited;
}

std::string CapabilityToQuotaBucket(const std::string& capability) {
    return CapabilityToEntitlementQuotaBucket(capability);
}

int DefaultQuotaLimitForBucket(SdkAccountType account_type, const std::string& bucket) {
    if (bucket == kTrialVipQuotaBucket || account_type == SdkAccountType::Trial) {
        return kTrialQuotaLimit;
    }
    return kDefaultQuotaLimit;
}

bool IsTrialVipCapability(const std::string& capability) {
    return capability == kTrialVipQuotaBucket ||
           capability == "ocr.recognize" || capability == "ocr.extract_text" ||
           capability == "image.process.vip" || capability == "image.process.svip" ||
           capability == "image.process.svip_plus" || capability == "image.enhance.vip" ||
           capability == "image.enhance.svip" || capability == "image.enhance.svip_plus" ||
           capability == "doc_crop_enhance" || capability == "remove_handwriting" ||
           capability == "doc_repair" || capability == "remove_moire" ||
           capability == "file.convert.vip" || capability == "file.convert.svip" ||
           capability == "file.convert.svip_plus";
}

SdkAccountType EffectiveOfflineQuotaAccountType(const ParsedApiKey& parsed, bool formally_unlocked) {
    if (RequiresHostActivation(parsed.auth_context) && !formally_unlocked) {
        return SdkAccountType::Trial;
    }
    return parsed.auth_context.licensed_account_type == SdkAccountType::Unknown ? parsed.auth_context.account_type
                                                                                : parsed.auth_context.licensed_account_type;
}

bool DeviceTierCanAuthorize(SdkAccountType device_tier, SdkAccountType licensed_tier) {
    return device_tier != SdkAccountType::Unknown && licensed_tier != SdkAccountType::Unknown &&
           IsEntitlementAtLeast(device_tier, licensed_tier);
}

SdkAccountType ResolveAuthTierFromAuthType(int auth_type) {
    using editor::auth::AuthType;
    switch (static_cast<AuthType>(auth_type)) {
        case AuthType::TYPE_PERMANENT_DURATION_SVIPLUS_CLIENT:
        case AuthType::TYPE_PERMANENT_DURATION_SVIPLUS_PLUGIN:
        case AuthType::TYPE_PERMANENT_DURATION_SVIPLUS_BOTH:
        case AuthType::TYPE_LIMITED_DURATION_SVIPLUS_CLIENT:
        case AuthType::TYPE_LIMITED_DURATION_SVIPLUS_PLUGIN:
        case AuthType::TYPE_LIMITED_DURATION_SVIPLUS_BOTH:
        case AuthType::TYPE_TRIAL_SVIPLUS_CLIENT:
        case AuthType::TYPE_TRIAL_SVIPLUS_PLUGIN:
        case AuthType::TYPE_TRIAL_SVIPLUS_BOTH:
        case AuthType::TYPE_INVITE_90_DAYS_SVIPLUS_BOTH:
        case AuthType::TYPE_CUSTOM_KEY_WORDS_DETECT:
            return SdkAccountType::SvipPlus;
        case AuthType::TYPE_PERMANENT_DURATION_SVIP_CLIENT:
        case AuthType::TYPE_PERMANENT_DURATION_SVIP_PLUGIN:
        case AuthType::TYPE_PERMANENT_DURATION_SVIP_BOTH:
        case AuthType::TYPE_LIMITED_DURATION_SVIP_CLIENT:
        case AuthType::TYPE_LIMITED_DURATION_SVIP_PLUGIN:
        case AuthType::TYPE_LIMITED_DURATION_SVIP_BOTH:
        case AuthType::TYPE_TRIAL_SVIP_CLIENT:
        case AuthType::TYPE_TRIAL_SVIP_PLUGIN:
        case AuthType::TYPE_TRIAL_SVIP_BOTH:
            return SdkAccountType::Svip;
        case AuthType::TYPE_PERMANENT_DURATION_VIP_CLIENT:
        case AuthType::TYPE_PERMANENT_DURATION_VIP_PLUGIN:
        case AuthType::TYPE_PERMANENT_DURATION_VIP_BOTH:
        case AuthType::TYPE_LIMITED_DURATION_VIP_CLIENT:
        case AuthType::TYPE_LIMITED_DURATION_VIP_PLUGIN:
        case AuthType::TYPE_LIMITED_DURATION_VIP_BOTH:
        case AuthType::TYPE_TRIAL_VIP_CLIENT:
        case AuthType::TYPE_TRIAL_VIP_PLUGIN:
        case AuthType::TYPE_TRIAL_VIP_BOTH:
            return SdkAccountType::Vip;
        default:
            return SdkAccountType::Unknown;
    }
}

bool EnsureAuthorizedSubsystem(std::string* err) {
    static std::mutex auth_manager_mu;
    static bool initialized = false;
    std::lock_guard<std::mutex> lock(auth_manager_mu);
    if (initialized) {
        return true;
    }

    const std::string log_dir = GetSdkRuntimeLogDir();
    if (!EnsureDirectoryRecursive(log_dir)) {
        if (err != NULL) {
            *err = "failed to create sdk runtime log dir";
        }
        return false;
    }

    editor::auth::AuthManager& auth_manager = editor::auth::AuthManager::GetInstance();
    const std::string log_path = log_dir + "/sdk_private_auth_manager.log";
    if (!auth_manager.Initialize(log_path, editor::auth::MachineUuidType::UUID_SSHORT_DMI_PRODUCT_ID)) {
        if (err != NULL) {
            *err = auth_manager.GetLastError();
        }
        return false;
    }

    initialized = true;
    return true;
}

bool GetLocalMachineCode(std::string* machine_code, std::string* err) {
    if (machine_code == NULL) {
        if (err != NULL) {
            *err = "missing machine_code output";
        }
        return false;
    }
    if (!EnsureAuthorizedSubsystem(err)) {
        return false;
    }

    editor::auth::AuthManager& auth_manager = editor::auth::AuthManager::GetInstance();
    *machine_code = auth_manager.GenerateMachineCode(editor::auth::LICENSE_KEY,
                                                     editor::auth::MachineUuidType::UUID_SSHORT_DMI_PRODUCT_ID);
    if (machine_code->empty()) {
        if (err != NULL) {
            *err = auth_manager.GetLastError();
        }
        return false;
    }
    return true;
}

std::string ReadOsReleaseValue(const std::string& key) {
    std::ifstream input("/etc/os-release");
    if (!input.is_open()) {
        return "";
    }
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(input, line)) {
        if (line.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        std::string value = line.substr(prefix.size());
        if (value.size() >= 2 && value[0] == '"' && value[value.size() - 1] == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }
    return "";
}

void AddStringIfPresent(nlohmann::json* object_json, const char* key, const std::string& value) {
    if (object_json != NULL && key != NULL && !value.empty()) {
        (*object_json)[key] = value;
    }
}

#if defined(_WIN32)
std::string WindowsWideToUtf8(const wchar_t* value, int length) {
    if (value == NULL || length <= 0) {
        return std::string();
    }
    const int required = ::WideCharToMultiByte(CP_UTF8, 0, value, length, NULL, 0, NULL, NULL);
    if (required <= 0) {
        return std::string();
    }
    std::string utf8(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(CP_UTF8, 0, value, length, &utf8[0], required, NULL, NULL) != required) {
        return std::string();
    }
    return utf8;
}
#endif

std::string ResolveAuthDeviceConfigPath();
int JsonIntOr(const nlohmann::json& object_json, const char* key, int fallback);
bool JsonBoolOr(const nlohmann::json& object_json, const char* key, bool fallback);

#if !defined(SDK_OPEN_HARMONY)
bool DeviceConfigHasBoolFeature(const std::shared_ptr<editor::devices::DeviceConfigData>& config,
                                const std::string& feature) {
    if (!config) {
        return false;
    }
    const std::map<std::string, bool>::const_iterator it = config->boolFeatures.find(feature);
    return it != config->boolFeatures.end() && it->second;
}

std::string UsbIdToUpperHex(int value) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << (value & 0xFFFF);
    return stream.str();
}

bool IsThirdPartyDeviceConfig(const std::shared_ptr<editor::devices::DeviceConfigData>& config) {
    return !config || config->deviceName == "3rdDevice";
}

bool IsCzurOwnedDeviceConfig(const std::shared_ptr<editor::devices::DeviceConfigData>& config) {
    return config && !IsThirdPartyDeviceConfig(config);
}

SdkAccountType DeviceConfigEntitlementTier(const std::shared_ptr<editor::devices::DeviceConfigData>& config) {
    if (!IsCzurOwnedDeviceConfig(config)) {
        return SdkAccountType::Unknown;
    }
    return DeviceConfigHasBoolFeature(config, "SpecialAuthorization") ? SdkAccountType::SvipPlus
                                                                        : SdkAccountType::Svip;
}

std::shared_ptr<editor::devices::DeviceConfigData> FindExactDeviceConfigByVidPid(int vid, int pid) {
    if (vid <= 0 || pid <= 0) {
        return std::shared_ptr<editor::devices::DeviceConfigData>();
    }

    editor::devices::DeviceConfigReader reader;
    if (!reader.loadConfig(ResolveAuthDeviceConfigPath())) {
        SDK_PRIVATE_LOG_WARN("auth.validate_api_key client_metadata device_config_unavailable reason=load_failed");
        return std::shared_ptr<editor::devices::DeviceConfigData>();
    }

    return reader.getDeviceConfigByVidPid(UsbIdToUpperHex(vid), UsbIdToUpperHex(pid));
}

bool DeviceMetadataHasCzurOwnedAuthorizationHint(const nlohmann::json& device_json) {
    if (!device_json.is_object()) {
        return false;
    }
    if (JsonBoolOr(device_json, "commercialAuthorizationHint", false) &&
        JsonBoolOr(device_json, "czurDevice", false) &&
        !JsonBoolOr(device_json, "thirdParty", true)) {
        return true;
    }

    const int vid = JsonIntOr(device_json, "vid", 0);
    const int pid = JsonIntOr(device_json, "pid", 0);
    return IsCzurOwnedDeviceConfig(FindExactDeviceConfigByVidPid(vid, pid));
}

SdkAccountType DeviceMetadataEntitlementTier(const nlohmann::json& device_json) {
    if (!device_json.is_object()) {
        return SdkAccountType::Unknown;
    }

    const int vid = JsonIntOr(device_json, "vid", 0);
    const int pid = JsonIntOr(device_json, "pid", 0);
    if (JsonBoolOr(device_json, "commercialAuthorizationHint", false) &&
        JsonBoolOr(device_json, "czurDevice", false) && !JsonBoolOr(device_json, "thirdParty", true)) {
        return JsonBoolOr(device_json, "svipplus", JsonBoolOr(device_json, "svipPlus", false))
                   ? SdkAccountType::SvipPlus
                   : SdkAccountType::Svip;
    }
    const std::shared_ptr<editor::devices::DeviceConfigData> config = FindExactDeviceConfigByVidPid(vid, pid);
    const SdkAccountType config_tier = DeviceConfigEntitlementTier(config);
    if (config_tier != SdkAccountType::Unknown) {
        return config_tier;
    }
    return SdkAccountType::Unknown;
}

void AnnotateDeviceMetadataAuthorization(nlohmann::json* device_json) {
    if (device_json == NULL || !device_json->is_object()) {
        return;
    }
    const bool czur_owned_device = DeviceMetadataHasCzurOwnedAuthorizationHint(*device_json);
    const SdkAccountType device_tier = DeviceMetadataEntitlementTier(*device_json);
    (*device_json)["czurDevice"] = czur_owned_device;
    (*device_json)["thirdParty"] = !czur_owned_device;
    (*device_json)["commercialAuthorizationHint"] = czur_owned_device;
    (*device_json)["svipplus"] = device_tier == SdkAccountType::SvipPlus;
}

SdkAccountType DevicesMetadataEntitlementTier(const nlohmann::json& devices_json) {
    if (!devices_json.is_array()) {
        return SdkAccountType::Unknown;
    }
    SdkAccountType highest_tier = SdkAccountType::Unknown;
    for (nlohmann::json::const_iterator it = devices_json.begin(); it != devices_json.end(); ++it) {
        const SdkAccountType device_tier = DeviceMetadataEntitlementTier(*it);
        if (IsEntitlementAtLeast(device_tier, highest_tier)) {
            highest_tier = device_tier;
        }
    }
    return highest_tier;
}

std::string DeviceDisplayName(const editor::devices::DeviceInfo& info,
                              const std::shared_ptr<editor::devices::DeviceConfigData>& config) {
    const std::string model = config && !config->deviceName.empty() ? config->deviceName : info.product;
    if (info.manufacturer.empty()) {
        return model;
    }
    if (model.empty()) {
        return info.manufacturer;
    }
    return info.manufacturer + " " + model;
}
#else
SdkAccountType DeviceMetadataEntitlementTier(const nlohmann::json&) {
    return SdkAccountType::Unknown;
}

SdkAccountType DevicesMetadataEntitlementTier(const nlohmann::json&) {
    return SdkAccountType::Unknown;
}
#endif

bool LocalFileExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
#if defined(_WIN32)
    const DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

std::string PathJoin(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    const char last = left[left.size() - 1];
    if (last == '/' || last == '\\') {
        return left + right;
    }
#if defined(_WIN32)
    return left + "\\" + right;
#else
    return left + "/" + right;
#endif
}

std::string ResolveAuthDeviceConfigPath() {
    const char* config_path = std::getenv("CZUR_SDK_DEVICE_CONFIG_PATH");
    if (config_path != NULL && config_path[0] != '\0') {
        return std::string(config_path);
    }

    const std::string exe_config_path = PathJoin(GetSdkExecutableDir(), "configs/device_config_compact.conf");
    if (LocalFileExists(exe_config_path)) {
        return exe_config_path;
    }

#ifdef SDK_PRIVATE_DEVICE_CONFIG_SOURCE_PATH
    const std::string source_config_path = SDK_PRIVATE_DEVICE_CONFIG_SOURCE_PATH;
    if (LocalFileExists(source_config_path)) {
        return source_config_path;
    }
#endif

    return exe_config_path;
}

nlohmann::json BuildHostMetadataJson() {
    nlohmann::json host_json = nlohmann::json::object();
    nlohmann::json os_json = nlohmann::json::object();

#if defined(_WIN32)
    wchar_t hostname[256] = {0};
    DWORD hostname_size = static_cast<DWORD>(sizeof(hostname) / sizeof(hostname[0]));
    if (::GetComputerNameW(hostname, &hostname_size) != 0) {
        AddStringIfPresent(&host_json,
                           "hostname",
                           WindowsWideToUtf8(hostname, static_cast<int>(hostname_size)));
    }
    os_json["sysname"] = "Windows";
#else
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        host_json["hostname"] = std::string(hostname);
    }

    struct utsname uts;
    if (uname(&uts) == 0) {
        AddStringIfPresent(&os_json, "sysname", uts.sysname);
        AddStringIfPresent(&os_json, "release", uts.release);
        AddStringIfPresent(&os_json, "version", uts.version);
        AddStringIfPresent(&os_json, "machine", uts.machine);
        AddStringIfPresent(&os_json, "nodename", uts.nodename);
    }
    AddStringIfPresent(&os_json, "id", ReadOsReleaseValue("ID"));
    AddStringIfPresent(&os_json, "versionId", ReadOsReleaseValue("VERSION_ID"));
    AddStringIfPresent(&os_json, "prettyName", ReadOsReleaseValue("PRETTY_NAME"));
#endif

    host_json["os"] = os_json;
    return host_json;
}

#if !defined(SDK_OPEN_HARMONY)
nlohmann::json BuildDeviceMetadataJson(const std::shared_ptr<editor::devices::Device>& device) {
    nlohmann::json device_json = nlohmann::json::object();
    if (!device) {
        return device_json;
    }

    const editor::devices::DeviceInfo info = device->getDeviceInfo();
    const std::shared_ptr<editor::devices::DeviceConfigData> config = device->getDeviceConfig();
    const bool czur_owned_device = IsCzurOwnedDeviceConfig(config);
    const SdkAccountType device_tier = DeviceConfigEntitlementTier(config);
    device_json["deviceId"] = device->getDeviceId();
    device_json["model"] = config && !config->deviceName.empty() ? config->deviceName : info.product;
    device_json["displayName"] = DeviceDisplayName(info, config);
    device_json["vid"] = info.vid;
    device_json["pid"] = info.pid;
    device_json["status"] = device->isStreaming() ? "streaming" : (device->isOpened() ? "opened" : "online");
    device_json["supportsVideo"] = true;
    device_json["imageTransferProtocol"] = DeviceConfigHasBoolFeature(config, "ImageTransferProtocol");
    device_json["czurDevice"] = czur_owned_device;
    device_json["thirdParty"] = !czur_owned_device;
    device_json["commercialAuthorizationHint"] = czur_owned_device;
    device_json["svipplus"] = device_tier == SdkAccountType::SvipPlus;
    return device_json;
}
#endif

nlohmann::json BuildDevicesMetadataJson() {
#if defined(SDK_OPEN_HARMONY)
    // Harmony has no SANE/TWAIN/device-manager backend.  Keep auth client
    // metadata truthful rather than attempting to initialize desktop USB
    // discovery during service authentication.
    return nlohmann::json::array();
#else
    const char* override_json = std::getenv(kAuthClientDevicesJsonEnv);
    if (override_json != NULL && override_json[0] != '\0') {
        nlohmann::json parsed = nlohmann::json::parse(override_json, NULL, false);
        if (parsed.is_array()) {
            for (nlohmann::json::iterator it = parsed.begin(); it != parsed.end(); ++it) {
                AnnotateDeviceMetadataAuthorization(&(*it));
            }
            return parsed;
        }
        SDK_PRIVATE_LOG_WARN("auth.validate_api_key client_metadata devices_override_ignored reason=json_invalid");
    }

    nlohmann::json devices_json = nlohmann::json::array();
    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
    if (manager == NULL) {
        SDK_PRIVATE_LOG_WARN("auth.validate_api_key client_metadata devices_unavailable reason=manager_null");
        return devices_json;
    }

    if (!EnsureDirectoryRecursive(GetSdkRuntimeLogDir())) {
        SDK_PRIVATE_LOG_WARN("auth.validate_api_key client_metadata devices_unavailable reason=log_dir_failed");
        return devices_json;
    }

    if (!manager->initialize(GetSdkRuntimeLogDir() + "/sdk_private_auth_device_manager.log",
                             ResolveAuthDeviceConfigPath())) {
        SDK_PRIVATE_LOG_WARN("auth.validate_api_key client_metadata devices_unavailable reason=manager_init_failed");
        return devices_json;
    }

    manager->updateDeviceList();
    const std::vector<std::shared_ptr<editor::devices::Device> > devices = manager->getDevices();
    for (std::vector<std::shared_ptr<editor::devices::Device> >::const_iterator it = devices.begin();
         it != devices.end();
         ++it) {
        if (*it) {
            devices_json.push_back(BuildDeviceMetadataJson(*it));
        }
    }
    return devices_json;
#endif
}

nlohmann::json BuildValidateClientJson() {
    nlohmann::json client_json = nlohmann::json::object();
    client_json["metadataVersion"] = kClientMetadataVersion;

    std::string machine_code;
    std::string machine_code_error;
    if (GetLocalMachineCode(&machine_code, &machine_code_error)) {
        client_json["machineCode"] = machine_code;
    } else {
        SDK_PRIVATE_LOG_WARN("auth.validate_api_key client_metadata machine_code_unavailable reason={}",
                             machine_code_error);
    }

    client_json["host"] = BuildHostMetadataJson();
    client_json["devices"] = BuildDevicesMetadataJson();
    const SdkAccountType device_entitlement_tier = DevicesMetadataEntitlementTier(client_json["devices"]);
    client_json["hasCzurOwnedDevice"] = device_entitlement_tier != SdkAccountType::Unknown;
    client_json["deviceEntitlementTier"] = ToAccountTypeString(device_entitlement_tier);
    return client_json;
}

OfflineEntitlementRecord ParseOfflineRecord(const nlohmann::json& record_json) {
    OfflineEntitlementRecord record;
    record.host_unlocked = record_json.value("host_unlocked", false);
    record.host_auth_type = record_json.value("host_auth_type", -1);
    record.host_account_type = AccountTypeFromString(record_json.value("host_account_type", std::string()));
    if (record.host_account_type == SdkAccountType::Unknown) {
        record.host_unlocked = false;
        record.host_auth_type = -1;
    }
    const nlohmann::json quotas_json = record_json.value("quotas", nlohmann::json::object());
    static const char* kBuckets[] = {"capture", "image", "file", "trial_vip"};
    for (std::size_t index = 0; index < sizeof(kBuckets) / sizeof(kBuckets[0]); ++index) {
        const std::string bucket = kBuckets[index];
        OfflineQuotaUsage usage;
        if (quotas_json.contains(bucket) && quotas_json[bucket].is_object()) {
            usage.used = quotas_json[bucket].value("used", 0);
            const nlohmann::json request_ids_json = quotas_json[bucket].value("request_ids", nlohmann::json::array());
            if (request_ids_json.is_array()) {
                for (nlohmann::json::const_iterator request_it = request_ids_json.begin();
                     request_it != request_ids_json.end();
                     ++request_it) {
                    if (request_it->is_string()) {
                        usage.request_ids.insert(request_it->get<std::string>());
                    }
                }
            }
        }
        if (usage.used < 0) {
            usage.used = 0;
        }
        record.quota_usage[bucket] = usage;
    }
    return record;
}

nlohmann::json BuildOfflineRecordJson(const OfflineEntitlementRecord& record) {
    nlohmann::json quotas_json = nlohmann::json::object();
    for (std::map<std::string, OfflineQuotaUsage>::const_iterator it = record.quota_usage.begin();
         it != record.quota_usage.end();
         ++it) {
        nlohmann::json request_ids_json = nlohmann::json::array();
        for (std::set<std::string>::const_iterator request_it = it->second.request_ids.begin();
             request_it != it->second.request_ids.end();
             ++request_it) {
            request_ids_json.push_back(*request_it);
        }
        quotas_json[it->first] = nlohmann::json{{"used", it->second.used}, {"request_ids", request_ids_json}};
    }

    return nlohmann::json{
        {"quotas", quotas_json},
        {"host_unlocked", record.host_unlocked},
        {"host_auth_type", record.host_auth_type},
        {"host_account_type", ToAccountTypeString(record.host_account_type)},
    };
}

OfflineEntitlementRecord LoadOfflineEntitlement(const ParsedApiKey& parsed, bool* store_dirty) {
    try {
        nlohmann::json store_json;
        const bool loaded = LoadJsonFile(GetOfflineStorePath(), &store_json);
        if (!loaded || !store_json.is_object()) {
            store_json = nlohmann::json::object();
        }
        if (!store_json.contains("records") || !store_json["records"].is_object()) {
            store_json["records"] = nlohmann::json::object();
        }

        nlohmann::json& record_json = store_json["records"][parsed.token_digest];
        const bool existed = record_json.is_object();
        OfflineEntitlementRecord record = ParseOfflineRecord(existed ? record_json : nlohmann::json::object());
        const nlohmann::json sanitized_record_json = BuildOfflineRecordJson(record);
        if (!existed || record_json != sanitized_record_json) {
            if (store_dirty != NULL) {
                *store_dirty = true;
            }
            record_json = sanitized_record_json;
            SaveJsonFile(GetOfflineStorePath(), store_json);
        }
        return record;
    } catch (const std::exception& e) {
        SDK_PRIVATE_LOG_WARN("auth.offline_entitlement load_failed token_digest={} reason={}",
                             parsed.token_digest.empty() ? "empty" : parsed.token_digest.substr(0, 12),
                             e.what());
        if (store_dirty != NULL) {
            *store_dirty = true;
        }
        return ParseOfflineRecord(nlohmann::json::object());
    }
}

bool SaveOfflineEntitlement(const ParsedApiKey& parsed, const OfflineEntitlementRecord& record) {
    nlohmann::json store_json;
    if (!LoadJsonFile(GetOfflineStorePath(), &store_json) || !store_json.is_object()) {
        store_json = nlohmann::json::object();
    }
    if (!store_json.contains("records") || !store_json["records"].is_object()) {
        store_json["records"] = nlohmann::json::object();
    }
    store_json["records"][parsed.token_digest] = BuildOfflineRecordJson(record);
    return SaveJsonFile(GetOfflineStorePath(), store_json);
}

void ApplyOfflineRecordHostActivation(const OfflineEntitlementRecord& record, HiddenPluginAuthState* hidden_state) {
    if (hidden_state == NULL || !record.host_unlocked || record.host_account_type == SdkAccountType::Unknown) {
        return;
    }
    hidden_state->unlocked = true;
    hidden_state->auth_type = record.host_auth_type;
    hidden_state->account_type = record.host_account_type;
}

bool IsPluginCapableGrant(int auth_type);
bool CanGrantTier(SdkAccountType grant_tier, SdkAccountType requested_tier);

HiddenPluginAuthState LoadHiddenPluginAuthState(const ParsedApiKey& parsed) {
    HiddenPluginAuthState state;
    try {
        std::string init_error;
        if (!EnsureAuthorizedSubsystem(&init_error)) {
            return state;
        }

        editor::auth::AuthManager& auth_manager = editor::auth::AuthManager::GetInstance();
        const nlohmann::json status =
            auth_manager.QueryAuthStatusInfo(editor::auth::LICENSE_KEY, editor::auth::FLAG_TIME_LEFT_PLUGIN, true);
        if (status.value("status_code", static_cast<int>(editor::auth::AuthErrorCode::UNKNOWN_ERROR)) !=
            static_cast<int>(editor::auth::AuthErrorCode::SUCCESS)) {
            return state;
        }

        const int auth_type = status.value("auth_type", -1);
        if (!IsPluginCapableGrant(auth_type) || status.value("times_left", 0) <= 0) {
            return state;
        }

        const SdkAccountType grant_tier = ResolveAuthTierFromAuthType(auth_type);
        if (grant_tier == SdkAccountType::Unknown || !CanGrantTier(grant_tier, parsed.auth_context.account_type)) {
            return state;
        }
        state.unlocked = true;
        state.auth_type = auth_type;
        state.account_type = grant_tier;
    } catch (const std::exception& e) {
        SDK_PRIVATE_LOG_WARN("auth.hidden_plugin_state load_failed token_digest={} reason={}",
                             parsed.token_digest.empty() ? "empty" : parsed.token_digest.substr(0, 12),
                             e.what());
        return state;
    }
    return state;
}

std::vector<AuthQuotaBucket> BuildOfflineQuotaBuckets(const ParsedApiKey& parsed,
                                                      const OfflineEntitlementRecord& record,
                                                      bool unlocked) {
    std::vector<AuthQuotaBucket> buckets;
    static const char* kMethodBuckets[][2] = {
        {"capture", "capture.take"},
        {"image", "image.process"},
        {"file", "file.convert"},
        {"trial_vip", "trial_vip"},
    };
    for (std::size_t index = 0; index < sizeof(kMethodBuckets) / sizeof(kMethodBuckets[0]); ++index) {
        const std::string bucket_name = kMethodBuckets[index][0];
        AuthQuotaBucket bucket;
        bucket.bucket = bucket_name;
        bucket.methods.push_back(kMethodBuckets[index][1]);
        if (bucket_name == "trial_vip") {
            bucket.methods.push_back("ocr.recognize");
            bucket.methods.push_back("ocr.extract_text");
            bucket.methods.push_back("image.enhance");
            bucket.methods.push_back("file.convert");
        }
        if (bucket_name == "image") {
            bucket.methods.push_back("image.process_page");
            bucket.methods.push_back("image.apply_color_mode");
            bucket.methods.push_back("image.enhance");
            bucket.methods.push_back("image.enhance_get");
            bucket.methods.push_back("image.enhance_cancel");
            bucket.methods.push_back("image.enhance_capabilities");
            bucket.methods.push_back("image.enhance_workflow_list");
            bucket.methods.push_back("image.enhance_workflow_get");
            bucket.methods.push_back("image.enhance_workflow_save");
            bucket.methods.push_back("image.enhance_workflow_delete");
        }
        if (unlocked && bucket_name != kTrialVipQuotaBucket) {
            bucket.limit = 0;
            bucket.remaining = 0;
            bucket.enforcement = "unlocked";
        } else {
            const int limit = DefaultQuotaLimitForBucket(EffectiveOfflineQuotaAccountType(parsed, unlocked), bucket_name);
            const std::map<std::string, OfflineQuotaUsage>::const_iterator usage_it = record.quota_usage.find(bucket_name);
            const int used = usage_it == record.quota_usage.end() ? 0 : usage_it->second.used;
            bucket.limit = limit;
            bucket.remaining = limit > used ? limit - used : 0;
            bucket.enforcement = bucket_name == kTrialVipQuotaBucket ? "local_trial_quota" : "local_quota";
        }
        buckets.push_back(bucket);
    }
    return buckets;
}

void SafeCopyDeviceScope(std::vector<SdkDeviceGrant>* target,
                         const std::vector<SdkDeviceGrant>& source,
                         const std::string& log_context,
                         const std::string& token_digest = std::string()) {
    if (target == NULL) {
        return;
    }
    target->clear();
    try {
        const std::size_t count = source.size();
        if (count == 0) {
            return;
        }
        if (count > 256) {
            SDK_PRIVATE_LOG_WARN("auth.device_scope copy_skipped context={} token_digest={} reason=count_too_large count={}",
                                 log_context,
                                 token_digest.empty() ? "empty" : token_digest.substr(0, 12),
                                 count);
            return;
        }
        for (std::size_t i = 0; i < count; ++i) {
            target->push_back(source[i]);
        }
    } catch (const std::exception& e) {
        SDK_PRIVATE_LOG_WARN("auth.device_scope copy_failed context={} token_digest={} reason={}",
                             log_context,
                             token_digest.empty() ? "empty" : token_digest.substr(0, 12),
                             e.what());
        target->clear();
    }
}

AuthContext BuildOfflineAuthContext(const ParsedApiKey& parsed,
                                    const std::string& machine_code,
                                    const OfflineEntitlementRecord& record,
                                    const HiddenPluginAuthState& hidden_state,
                                    SdkAccountType device_entitlement_tier) {
    AuthContext auth_context;
    auth_context.is_valid = parsed.auth_context.is_valid;
    auth_context.account_type = parsed.auth_context.account_type;
    auth_context.account_type_code = parsed.auth_context.account_type_code;
    auth_context.licensed_account_type = parsed.auth_context.licensed_account_type;
    auth_context.licensed_account_type_code = parsed.auth_context.licensed_account_type_code;
    auth_context.auth_scene = parsed.auth_context.auth_scene;
    auth_context.license_mode = parsed.auth_context.license_mode;
    auth_context.host_auth_mode = parsed.auth_context.host_auth_mode;
    auth_context.expires_at = parsed.auth_context.expires_at;
    SafeCopyDeviceScope(&auth_context.device_scope,
                        parsed.auth_context.device_scope,
                        "offline_context",
                        parsed.token_digest);
    auth_context.machine_code = machine_code;
    auth_context.commercial_authorized = parsed.auth_context.commercial_authorized;
    const bool host_activated = hidden_state.unlocked;
    const bool device_auto_authorized =
        DeviceTierCanAuthorize(device_entitlement_tier, auth_context.licensed_account_type);
    const bool formally_unlocked = host_activated || device_auto_authorized;
    auth_context.entitlement_state =
        formally_unlocked ? kOfflineEntitlementUnlocked : DefaultOfflineEntitlementState(parsed);
    if (formally_unlocked) {
        auth_context.account_type = parsed.auth_context.account_type;
        auth_context.account_type_code = AccountTypeCode(auth_context.account_type);
        auth_context.licensed_account_type = parsed.auth_context.licensed_account_type;
        auth_context.licensed_account_type_code = AccountTypeCode(auth_context.licensed_account_type);
    } else if (RequiresHostActivation(auth_context)) {
        auth_context.entitlement_state = kOfflineEntitlementTrial;
        auth_context.account_type = SdkAccountType::Trial;
        auth_context.account_type_code = AccountTypeCode(SdkAccountType::Trial);
    }
    auth_context.quota_buckets = BuildOfflineQuotaBuckets(parsed, record, formally_unlocked);
    return auth_context;
}

void AssignFinalizedAuthContextFields(AuthContext* target, const AuthContext& source) {
    if (target == NULL) {
        return;
    }
    target->is_valid = source.is_valid;
    target->account_type = source.account_type;
    target->account_type_code = source.account_type_code;
    target->licensed_account_type = source.licensed_account_type;
    target->licensed_account_type_code = source.licensed_account_type_code;
    target->auth_scene = source.auth_scene;
    target->license_mode = source.license_mode;
    target->host_auth_mode = source.host_auth_mode;
    target->entitlement_state = source.entitlement_state;
    target->commercial_authorized = source.commercial_authorized;
    target->machine_code = source.machine_code;
    target->expires_at = source.expires_at;
    target->capabilities = source.capabilities;
    target->quota_buckets = source.quota_buckets;
    SafeCopyDeviceScope(&target->device_scope, source.device_scope, "assign_finalized");
}

bool ParseDeviceScopeJson(const nlohmann::json& device_scope_json, std::vector<SdkDeviceGrant>* device_scope) {
    if (device_scope == NULL) {
        return false;
    }
    device_scope->clear();
    if (!device_scope_json.is_array()) {
        return false;
    }
    for (nlohmann::json::const_iterator it = device_scope_json.begin(); it != device_scope_json.end(); ++it) {
        if (!it->is_object()) {
            return false;
        }
        SdkDeviceGrant grant;
        grant.vid = it->value("vid", 0);
        grant.pid = it->value("pid", 0);
        if (grant.vid <= 0 || grant.pid <= 0) {
            return false;
        }
        device_scope->push_back(grant);
    }
    return true;
}

const nlohmann::json* FindJsonField(const nlohmann::json& object_json, const char* key) {
    if (!object_json.is_object()) {
        return NULL;
    }
    nlohmann::json::const_iterator it = object_json.find(key);
    return it == object_json.end() ? NULL : &(*it);
}

std::string JsonStringOr(const nlohmann::json& object_json,
                         const char* key,
                         const std::string& fallback = std::string()) {
    const nlohmann::json* value = FindJsonField(object_json, key);
    return value != NULL && value->is_string() ? value->get<std::string>() : fallback;
}

int JsonIntOr(const nlohmann::json& object_json, const char* key, int fallback) {
    const nlohmann::json* value = FindJsonField(object_json, key);
    return value != NULL && value->is_number_integer() ? value->get<int>() : fallback;
}

std::int64_t JsonInt64Or(const nlohmann::json& object_json, const char* key, std::int64_t fallback) {
    const nlohmann::json* value = FindJsonField(object_json, key);
    return value != NULL && value->is_number_integer() ? value->get<std::int64_t>() : fallback;
}

bool JsonBoolOr(const nlohmann::json& object_json, const char* key, bool fallback) {
    const nlohmann::json* value = FindJsonField(object_json, key);
    return value != NULL && value->is_boolean() ? value->get<bool>() : fallback;
}

nlohmann::json JsonObjectOr(const nlohmann::json& object_json, const char* key) {
    const nlohmann::json* value = FindJsonField(object_json, key);
    return value != NULL && value->is_object() ? *value : nlohmann::json::object();
}

nlohmann::json JsonArrayOr(const nlohmann::json& object_json, const char* key) {
    const nlohmann::json* value = FindJsonField(object_json, key);
    return value != NULL && value->is_array() ? *value : nlohmann::json::array();
}

std::vector<AuthQuotaBucket> ParseQuotaBucketsJson(const nlohmann::json& quota_buckets_json) {
    std::vector<AuthQuotaBucket> quota_buckets;
    if (!quota_buckets_json.is_array()) {
        return quota_buckets;
    }
    for (nlohmann::json::const_iterator bucket_it = quota_buckets_json.begin(); bucket_it != quota_buckets_json.end();
         ++bucket_it) {
        if (!bucket_it->is_object()) {
            continue;
        }
        AuthQuotaBucket bucket;
        bucket.bucket = JsonStringOr(*bucket_it, "bucket");
        bucket.limit = JsonIntOr(*bucket_it, "limit", 0);
        bucket.remaining = JsonIntOr(*bucket_it, "remaining", 0);
        bucket.enforcement = JsonStringOr(*bucket_it, "enforcement");
        const nlohmann::json methods_json = JsonArrayOr(*bucket_it, "methods");
        if (methods_json.is_array()) {
            for (nlohmann::json::const_iterator method_it = methods_json.begin(); method_it != methods_json.end();
                 ++method_it) {
                if (method_it->is_string()) {
                    bucket.methods.push_back(method_it->get<std::string>());
                }
            }
        }
        quota_buckets.push_back(bucket);
    }
    return quota_buckets;
}

std::string TrimString(std::string value) {
    const std::string whitespace = " \t\r\n";
    const std::string::size_type begin = value.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return "";
    }
    const std::string::size_type end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

std::string EnvString(const char* name) {
    const char* value = std::getenv(name);
    return value == NULL ? std::string() : TrimString(value);
}

std::string ShortDigestForLog(const std::string& value) {
    if (value.empty()) {
        return "empty";
    }
    const std::string digest = Sha256HexString(value);
    return digest.size() > 12 ? digest.substr(0, 12) : digest;
}

std::string AuthzBaseUrlSource(const std::string& request_base_url) {
    const std::string env_value = EnvString(kAuthzBaseUrlEnv);
    if (!env_value.empty()) {
        return "env";
    }
    const std::string request_value = TrimString(request_base_url);
    return request_value.empty() ? "default" : "request";
}

std::string BoolString(bool value) {
    return value ? "true" : "false";
}

std::string ResolveAuthzBaseUrl(const std::string& request_base_url) {
    const std::string env_value = EnvString(kAuthzBaseUrlEnv);
    if (!env_value.empty()) {
        return env_value;
    }
    const std::string request_value = TrimString(request_base_url);
    return request_value.empty() ? std::string(kDefaultAuthzBaseUrl) : request_value;
}

std::string HttpOrigin(const ParsedHttpUrl& parsed_url) {
    const bool default_port = (parsed_url.https && parsed_url.port == 443) ||
                              (!parsed_url.https && parsed_url.port == 80);
    std::string origin = parsed_url.https ? "https://" : "http://";
    origin += parsed_url.host;
    if (!default_port) {
        origin += ":" + std::to_string(parsed_url.port);
    }
    return origin;
}

int MapAuthzStatusCode(int code) {
    if (code == 200 || code == 0) {
        return ToCode(SdkStatusCode::Ok);
    }
    if (code == 400) {
        return ToCode(SdkStatusCode::InvalidParams);
    }
    if (code == 401) {
        return ToCode(SdkStatusCode::AuthRequired);
    }
    if (code == 403) {
        return ToCode(SdkStatusCode::TokenInvalid);
    }
    if (code == 1104) {
        return ToCode(SdkStatusCode::TokenExpired);
    }
    if (code == 1110 || code == 429) {
        return ToCode(SdkStatusCode::UsageLimitExceeded);
    }
    return ToCode(SdkStatusCode::OnlineAuthUnavailable);
}

OnlineCallResult PostOnlineAuthJson(const std::string& base_url,
                                    const std::string& endpoint,
                                    const nlohmann::json& request_json) {
    OnlineCallResult result;
    const ParsedHttpUrl parsed_url = ParseHttpUrl(base_url);
    if (!parsed_url.valid) {
        result.message = "online auth url invalid";
        SDK_PRIVATE_LOG_WARN("auth.online request_skip reason=url_invalid base_url={}", base_url);
        return result;
    }

    const int timeout_ms = GetIntEnvOrDefault(kOnlineAuthTimeoutEnv, 10000);
    const std::string path = parsed_url.base_path + endpoint;
    const std::string api_key = JsonStringOr(request_json, "apiKey");
    SDK_PRIVATE_LOG_INFO("auth.online request endpoint={} origin={} path={} timeout_ms={} token_digest={} capability={} units={} request_id_present={}",
                         endpoint,
                         HttpOrigin(parsed_url),
                         path,
                         timeout_ms,
                         ShortDigestForLog(api_key),
                         JsonStringOr(request_json, "capability"),
                         JsonIntOr(request_json, "usageAmount", 0),
                         !JsonStringOr(request_json, "idempotencyKey").empty());

    std::unique_ptr<httplib::Client> client(new httplib::Client(HttpOrigin(parsed_url).c_str()));
    if (parsed_url.https) {
        ConfigureSdkHttpsClientTrustStore(client.get());
    }

    client->set_connection_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
    client->set_read_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
    client->set_write_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);

    bool replaced_invalid_utf8 = false;
    const std::string request_body =
        DumpJsonUtf8ReplacingInvalidBytes(request_json, &replaced_invalid_utf8);
    if (replaced_invalid_utf8) {
        SDK_PRIVATE_LOG_WARN("auth.online request_metadata_invalid_utf8_replaced endpoint={} token_digest={}",
                             endpoint,
                             ShortDigestForLog(api_key));
    }
    httplib::Result response = client->Post(path.c_str(), request_body, "application/json");
    if (!response) {
        result.message = "online auth request failed: " + httplib::to_string(response.error());
        SDK_PRIVATE_LOG_WARN("auth.online response_failed endpoint={} token_digest={} error={}",
                             endpoint,
                             ShortDigestForLog(api_key),
                             httplib::to_string(response.error()));
        return result;
    }

    result.body = nlohmann::json::parse(response->body, NULL, false);
    if (result.body.is_discarded()) {
        result.body = nlohmann::json::object();
    }

    if (response->status != 200) {
        const int service_code = JsonIntOr(result.body, "code", response->status);
        result.code = MapAuthzStatusCode(service_code);
        result.message = JsonStringOr(result.body, "message", "online auth http error");
        SDK_PRIVATE_LOG_WARN("auth.online response endpoint={} token_digest={} http_status={} service_code={} mapped_code={} message={}",
                             endpoint,
                             ShortDigestForLog(api_key),
                             response->status,
                             service_code,
                             result.code,
                             result.message);
        if (result.code == ToCode(SdkStatusCode::TokenInvalid)) {
            SDK_PRIVATE_LOG_WARN("auth.online token_invalid reason=authz_rejected_token endpoint={} token_digest={} http_status={} service_code={}",
                                 endpoint,
                                 ShortDigestForLog(api_key),
                                 response->status,
                                 service_code);
        }
        return result;
    }

    const int service_code = JsonIntOr(result.body, "code", 200);
    result.code = MapAuthzStatusCode(service_code);
    result.message = JsonStringOr(result.body, "message", result.code == 0 ? "ok" : "rejected");
    result.body = JsonObjectOr(result.body, "data");
    result.allowed = result.code == 0;
    SDK_PRIVATE_LOG_INFO("auth.online response endpoint={} token_digest={} http_status={} service_code={} mapped_code={} allowed={} accepted={} duplicate={} reason={} remaining={}",
                         endpoint,
                         ShortDigestForLog(api_key),
                         response->status,
                         service_code,
                         result.code,
                         BoolString(JsonBoolOr(result.body, "allowed", result.allowed)),
                         BoolString(JsonBoolOr(result.body, "accepted", result.allowed)),
                         BoolString(JsonBoolOr(result.body, "duplicate", false)),
                         JsonStringOr(result.body, "reason"),
                         JsonIntOr(result.body, "remainingQuota", JsonIntOr(result.body, "remaining", 0)));
    if (result.code == ToCode(SdkStatusCode::TokenInvalid)) {
        SDK_PRIVATE_LOG_WARN("auth.online token_invalid reason=authz_rejected_token endpoint={} token_digest={} http_status={} service_code={}",
                             endpoint,
                             ShortDigestForLog(api_key),
                             response->status,
                             service_code);
    }
    return result;
}

AuthContext BuildOnlineAuthContext(const ParsedApiKey& parsed,
                                   const nlohmann::json& body_json,
                                   SdkAccountType device_entitlement_tier) {
    AuthContext auth_context = parsed.auth_context;
    const std::string response_account_type =
        JsonStringOr(body_json, "accountType", JsonStringOr(body_json, "account_type", JsonStringOr(body_json, "tier")));
    const SdkAccountType remote_account_type = AccountTypeFromString(response_account_type);
    if (remote_account_type != SdkAccountType::Unknown) {
        auth_context.account_type = remote_account_type;
        auth_context.account_type_code = AccountTypeCode(remote_account_type);
        auth_context.licensed_account_type = remote_account_type;
        auth_context.licensed_account_type_code = AccountTypeCode(remote_account_type);
    }

    std::vector<SdkDeviceGrant> remote_scope;
    if (body_json.contains("device_scope") && ParseDeviceScopeJson(body_json["device_scope"], &remote_scope)) {
        auth_context.device_scope = remote_scope;
    }

    const bool server_commercial_authorized =
        JsonBoolOr(body_json, "commercialAuthorized", JsonBoolOr(body_json, "commercial_authorized", false));
    const bool device_auto_authorized =
        DeviceTierCanAuthorize(device_entitlement_tier, auth_context.licensed_account_type);
    const bool formally_authorized = server_commercial_authorized || device_auto_authorized;
    // This field preserves the commercial authorization fact. A CZUR device can
    // make the current session formal without fabricating commercial approval.
    auth_context.commercial_authorized = server_commercial_authorized;
    auth_context.license_mode = kOnlineApiKeyMode;
    auth_context.entitlement_state = formally_authorized ? kOnlineEntitlementValidated : kOnlineEntitlementTrial;
    auth_context.machine_code.clear();
    auth_context.expires_at = JsonInt64Or(body_json, "expires_at", parsed.auth_context.expires_at);
    if (!formally_authorized) {
        if (auth_context.licensed_account_type == SdkAccountType::Unknown) {
            auth_context.licensed_account_type = auth_context.account_type;
            auth_context.licensed_account_type_code = AccountTypeCode(auth_context.licensed_account_type);
        }
        auth_context.account_type = SdkAccountType::Trial;
        auth_context.account_type_code = AccountTypeCode(SdkAccountType::Trial);
        const OfflineEntitlementRecord record = LoadOfflineEntitlement(parsed, NULL);
        HiddenPluginAuthState hidden_state;
        hidden_state.unlocked = false;
        auth_context.quota_buckets = BuildOfflineQuotaBuckets(parsed, record, hidden_state.unlocked);
    } else {
        auth_context.quota_buckets = ParseQuotaBucketsJson(JsonArrayOr(body_json, "quota_buckets"));
    }
    return auth_context;
}

bool IsPluginCapableGrant(int auth_type) {
    return editor::auth::IsPluginOnlyType(auth_type) || editor::auth::IsBothType_CP(auth_type);
}

bool IsValidExpiresDate(const std::string& expires_date) {
    if (expires_date.size() != 8) {
        return false;
    }
    for (std::size_t i = 0; i < expires_date.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(expires_date[i]))) {
            return false;
        }
    }
    return true;
}

bool CanGrantTier(SdkAccountType grant_tier, SdkAccountType requested_tier) {
    return IsEntitlementAtLeast(grant_tier, requested_tier);
}

} // namespace

AuthValidateResult PrivateSdkAuthService::ValidateApiKey(const AuthValidateRequest& request) const {
    AuthValidateResult result;
    ValidateApiKey(request, &result);
    return result;
}

void PrivateSdkAuthService::ValidateApiKey(const AuthValidateRequest& request,
                                           AuthValidateResult* result_output) const {
    if (result_output == NULL) {
        return;
    }
    AuthValidateResult result;
    if (request.token.empty()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "token required";
        *result_output = result;
        return;
    }

    ParsedApiKey parsed;
    key_codec_.ParseAndValidateApiKey(request.token, request.now_ts, &parsed);
    result.code = parsed.code;
    result.message = parsed.message;
    if (parsed.code != 0) {
        SDK_PRIVATE_LOG_WARN("auth.validate_api_key local_parse_failed code={} message={} token_digest={}",
                             parsed.code,
                             parsed.message,
                             ShortDigestForLog(request.token));
        *result_output = result;
        return;
    }

    SDK_PRIVATE_LOG_INFO("auth.validate_api_key local_parse_ok token_digest={} key_id={} license_mode={} tier={} expires_at={} authz_source={}",
                         ShortDigestForLog(request.token),
                         parsed.key_id,
                         parsed.license_mode,
                         ToAccountTypeString(parsed.auth_context.account_type),
                         parsed.auth_context.expires_at,
                         AuthzBaseUrlSource(request.authz_base_url));

    if (IsOnlineMode(parsed.license_mode)) {
        nlohmann::json validate_request = nlohmann::json{
            {"apiKey", request.token},
            {"capability", kSessionAuthzCapability},
        };
        const nlohmann::json client_json = BuildValidateClientJson();
        const SdkAccountType device_entitlement_tier =
            AccountTypeFromString(JsonStringOr(client_json, "deviceEntitlementTier"));
        validate_request["client"] = client_json;
        const std::string authz_base_url = ResolveAuthzBaseUrl(request.authz_base_url);
        SDK_PRIVATE_LOG_INFO("auth.validate_api_key online_validate begin token_digest={} key_id={} authz_source={} authz_base_url={}",
                             ShortDigestForLog(request.token),
                             parsed.key_id,
                             AuthzBaseUrlSource(request.authz_base_url),
                             authz_base_url);
        const OnlineCallResult online_result =
            PostOnlineAuthJson(authz_base_url, kAuthzValidatePath, validate_request);
        result.code = online_result.code;
        result.message = online_result.message;
        const bool allowed = JsonBoolOr(online_result.body, "allowed", online_result.allowed);
        if (online_result.code == 0 && allowed) {
            result.auth_context =
                FinalizeAuthContext(BuildOnlineAuthContext(parsed, online_result.body, device_entitlement_tier));
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                online_auth_contexts_[parsed.token_digest] = result.auth_context;
            }
            SDK_PRIVATE_LOG_INFO("auth.validate_api_key online_validate success token_digest={} key_id={} remaining={}",
                                 ShortDigestForLog(request.token),
                                 parsed.key_id,
                                 JsonIntOr(online_result.body, "remainingQuota", JsonIntOr(online_result.body, "remaining", 0)));
        } else if (online_result.code == 0) {
            result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
            result.message = JsonStringOr(online_result.body, "reason", "online auth rejected");
        }
        if (result.code != 0) {
            SDK_PRIVATE_LOG_WARN("auth.validate_api_key online_validate failed token_digest={} key_id={} code={} message={}",
                                 ShortDigestForLog(request.token),
                                 parsed.key_id,
                                 result.code,
                                 result.message);
        }
        *result_output = result;
        return;
    }

    std::string machine_code;
    std::string machine_code_error;
    if (!GetLocalMachineCode(&machine_code, &machine_code_error)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = machine_code_error.empty() ? "machine code unavailable" : machine_code_error;
        *result_output = result;
        return;
    }

    OfflineEntitlementRecord record;
    HiddenPluginAuthState hidden_state;
    const SdkAccountType device_entitlement_tier =
        DevicesMetadataEntitlementTier(BuildDevicesMetadataJson());
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        record = LoadOfflineEntitlement(parsed, NULL);
        if (RequiresHostActivation(parsed.auth_context)) {
            hidden_state = LoadHiddenPluginAuthState(parsed);
            ApplyOfflineRecordHostActivation(record, &hidden_state);
        }
    }
    const AuthContext offline_context =
        BuildOfflineAuthContext(parsed, machine_code, record, hidden_state, device_entitlement_tier);
    result.auth_context = FinalizeAuthContext(offline_context);
    *result_output = result;
    return;
}

AuthRefreshResult PrivateSdkAuthService::RefreshSession(const AuthRefreshRequest& request) const {
    AuthRefreshResult result;
    RefreshSession(request, &result);
    return result;
}

void PrivateSdkAuthService::RefreshSession(const AuthRefreshRequest& request,
                                           AuthRefreshResult* result_output) const {
    if (result_output == NULL) {
        return;
    }
    AuthRefreshResult result;
    SessionValidateRequest session_request;
    session_request.session_token = request.session_token;
    session_request.now_ts = request.now_ts;
    const SessionValidateResult session_result = ValidateSession(session_request);
    if (session_result.code != 0) {
        result.code = session_result.code;
        result.message = session_result.message;
        *result_output = result;
        return;
    }

    SessionIssueResult session_issue;
    session_service_.IssueSession(session_result.auth_context, request.now_ts, &session_issue);
    result.code = session_issue.code;
    result.message = session_issue.message;
    result.session_token = session_issue.session_token;
    result.expires_in = session_issue.expires_in;
    result.auth_context = FinalizeAuthContext(session_issue.auth_context);
    *result_output = result;
    return;
}

AuthRefreshResult PrivateSdkAuthService::RefreshSession(const AuthValidateRequest& request) const {
    AuthRefreshResult result;
    RefreshSession(request, &result);
    return result;
}

void PrivateSdkAuthService::RefreshSession(const AuthValidateRequest& request,
                                           AuthRefreshResult* result_output) const {
    if (result_output == NULL) {
        return;
    }
    AuthRefreshResult result;
    AuthValidateResult validate_result;
    ValidateApiKey(request, &validate_result);
    if (validate_result.code != 0) {
        result.code = validate_result.code;
        result.message = validate_result.message;
        *result_output = result;
        return;
    }

    SessionIssueResult session_result;
    session_service_.IssueSession(validate_result.auth_context, request.now_ts, &session_result);
    result.code = session_result.code;
    result.message = session_result.message;
    result.session_token = session_result.session_token;
    result.expires_in = session_result.expires_in;
    result.auth_context = FinalizeAuthContext(session_result.auth_context);
    *result_output = result;
    return;
}

SessionValidateResult PrivateSdkAuthService::ValidateSession(const SessionValidateRequest& request) const {
    SessionValidateResult result;
    if (request.session_token.empty()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "auth required";
        return result;
    }

    const SessionParseResult session_result = session_service_.ValidateSession(request.session_token, request.now_ts);
    result.code = session_result.code;
    result.message = session_result.message;
    if (session_result.code == 0) {
        result.auth_context = FinalizeAuthContext(session_result.auth_context);
    }
    return result;
}

AuthContextResult PrivateSdkAuthService::GetAuthContext(const AuthLookupRequest& request) const {
    AuthContextResult result;
    if (!request.session_token.empty()) {
        SessionValidateRequest session_request;
        session_request.session_token = request.session_token;
        session_request.now_ts = request.now_ts;
        const SessionValidateResult session_result = ValidateSession(session_request);
        if (session_result.code == 0) {
            result.code = 0;
            result.message = "ok";
            result.via_session = true;
            result.auth_context = session_result.auth_context;
            return result;
        }
        result.code = session_result.code;
        result.message = session_result.message;
        return result;
    }

    result.code = ToCode(SdkStatusCode::AuthRequired);
    result.message = "session required";
    return result;
}

OfflineActivateResult PrivateSdkAuthService::ActivateOffline(const OfflineActivateRequest& request) const {
    OfflineActivateResult result;
    if (request.token.empty()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "token required";
        return result;
    }
    if (request.auth_code.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "auth_code required";
        return result;
    }

    ParsedApiKey parsed;
    key_codec_.ParseAndValidateApiKey(request.token, request.now_ts, &parsed);
    if (parsed.code != 0) {
        result.code = parsed.code;
        result.message = parsed.message;
        return result;
    }
    if (!IsOfflineMode(parsed.license_mode)) {
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "offline activation requires offline_api_key";
        return result;
    }

    std::string machine_code;
    std::string machine_code_error;
    if (!GetLocalMachineCode(&machine_code, &machine_code_error)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = machine_code_error.empty() ? "machine code unavailable" : machine_code_error;
        return result;
    }

    std::string init_error;
    if (!EnsureAuthorizedSubsystem(&init_error)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = init_error.empty() ? "auth subsystem unavailable" : init_error;
        return result;
    }

    editor::auth::AuthManager& auth_manager = editor::auth::AuthManager::GetInstance();
    const nlohmann::json auth_code_result = auth_manager.ParseAuthCode(editor::auth::LICENSE_KEY, request.auth_code, true);
    if (!auth_code_result.value("success", false)) {
        result.code = ToCode(SdkStatusCode::OfflineAuthCodeInvalid);
        result.message = "offline auth code invalid";
        return result;
    }

    const int auth_type = auth_code_result.value("auth_type", -1);
    if (!IsPluginCapableGrant(auth_type)) {
        result.code = ToCode(SdkStatusCode::OfflineAuthCodeInvalid);
        result.message = "offline auth code does not grant plugin access";
        return result;
    }

    const SdkAccountType grant_tier = ResolveAuthTierFromAuthType(auth_type);
    if (grant_tier == SdkAccountType::Unknown || !CanGrantTier(grant_tier, parsed.auth_context.account_type)) {
        result.code = ToCode(SdkStatusCode::OfflineAuthCodeInvalid);
        result.message = "offline auth code tier is lower than api key tier";
        return result;
    }
    const std::string matched_machine_code = auth_code_result.value("machine_code", std::string());
    if (matched_machine_code.empty() || matched_machine_code != machine_code) {
        result.code = ToCode(SdkStatusCode::OfflineBindingMismatch);
        result.message = "offline auth code is not bound to this machine";
        return result;
    }

    std::tuple<int, int> update_result;
    try {
        update_result = auth_manager.UpdateAuthLicenseInfo(editor::auth::LICENSE_KEY, request.auth_code, "sdk_private");
        if (std::get<0>(update_result) == static_cast<int>(editor::auth::AuthErrorCode::LICENSE_FILE_NOT_FOUND)) {
            if (!auth_manager.InitAuthLicense()) {
                result.code = ToCode(SdkStatusCode::InternalError);
                result.message = auth_manager.GetLastError().empty() ? "failed to initialize local license store" : auth_manager.GetLastError();
                return result;
            }
            update_result = auth_manager.UpdateAuthLicenseInfo(editor::auth::LICENSE_KEY, request.auth_code, "sdk_private");
        }
    } catch (const std::exception&) {
        result.code = ToCode(SdkStatusCode::OfflineAuthCodeInvalid);
        result.message = "offline auth code invalid";
        return result;
    }
    const int update_status = std::get<0>(update_result);
    if (update_status != static_cast<int>(editor::auth::AuthErrorCode::SUCCESS) &&
        update_status != static_cast<int>(editor::auth::AuthErrorCode::NEED_NOT_UPDATE_LICENSE)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "failed to persist offline activation";
        return result;
    }

    OfflineEntitlementRecord record;
    HiddenPluginAuthState hidden_state;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        record = LoadOfflineEntitlement(parsed, NULL);
        hidden_state = LoadHiddenPluginAuthState(parsed);
        ApplyOfflineRecordHostActivation(record, &hidden_state);
        record.host_unlocked = true;
        record.host_auth_type = auth_type;
        record.host_account_type = grant_tier;
        if (!SaveOfflineEntitlement(parsed, record)) {
            result.code = ToCode(SdkStatusCode::InternalError);
            result.message = "failed to persist offline activation";
            return result;
        }
    }
    if (!hidden_state.unlocked) {
        hidden_state.unlocked = true;
        hidden_state.auth_type = auth_type;
        hidden_state.account_type = grant_tier;
    }

    const SdkAccountType device_entitlement_tier =
        DevicesMetadataEntitlementTier(BuildDevicesMetadataJson());
    const AuthContext auth_context = FinalizeAuthContext(
        BuildOfflineAuthContext(parsed, machine_code, record, hidden_state, device_entitlement_tier));
    SessionIssueResult session_issue;
    session_service_.IssueSession(auth_context, request.now_ts, &session_issue);
    result.code = session_issue.code;
    result.message = session_issue.message;
    result.activated = session_issue.code == 0;
    result.session_token = session_issue.session_token;
    result.expires_in = session_issue.expires_in;
    result.auth_context = auth_context;
    return result;
}

QuotaConsumeResult PrivateSdkAuthService::ConsumeQuota(const QuotaConsumeRequest& request) const {
    QuotaConsumeResult result;
    if (request.token.empty()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "token required";
        return result;
    }

    const std::string bucket_name = CapabilityToQuotaBucket(request.capability);
    if (bucket_name.empty()) {
        AuthValidateRequest validate_request;
        validate_request.token = request.token;
        validate_request.now_ts = request.now_ts;
        AuthValidateResult validate_result;
        ValidateApiKey(validate_request, &validate_result);
        result.code = validate_result.code;
        result.message = validate_result.message;
        result.consumed = validate_result.code == 0;
        result.auth_context = validate_result.auth_context;
        return result;
    }

    ParsedApiKey parsed;
    key_codec_.ParseAndValidateApiKey(request.token, request.now_ts, &parsed);
    if (parsed.code != 0) {
        result.code = parsed.code;
        result.message = parsed.message;
        return result;
    }

    if (IsOnlineMode(parsed.license_mode)) {
        AuthContext cached_context;
        {
            std::lock_guard<std::mutex> lock(state_mu_);
            const std::map<std::string, AuthContext>::const_iterator context_it =
                online_auth_contexts_.find(parsed.token_digest);
            if (context_it != online_auth_contexts_.end()) {
                cached_context = context_it->second;
            }
        }
        if (!cached_context.is_valid && !request.session_token.empty()) {
            SessionValidateRequest session_request;
            session_request.session_token = request.session_token;
            session_request.now_ts = request.now_ts;
            const SessionValidateResult session_result = ValidateSession(session_request);
            if (session_result.code == 0 && IsOnlineMode(session_result.auth_context.license_mode)) {
                cached_context = session_result.auth_context;
                std::lock_guard<std::mutex> lock(state_mu_);
                online_auth_contexts_[parsed.token_digest] = cached_context;
            }
        }
        if (IsTrialEntitlementState(cached_context) && IsTrialVipCapability(request.capability)) {
            OfflineEntitlementRecord record;
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                record = LoadOfflineEntitlement(parsed, NULL);
                OfflineQuotaUsage& usage = record.quota_usage[kTrialVipQuotaBucket];
                const int limit = DefaultQuotaLimitForBucket(SdkAccountType::Trial, kTrialVipQuotaBucket);
                result.bucket = kTrialVipQuotaBucket;
                result.limit = limit;
                const bool duplicate = !request.request_id.empty() &&
                                       usage.request_ids.find(request.request_id) != usage.request_ids.end();
                const int requested_units = request.units > 0 ? request.units : 1;
                const int next_used = duplicate ? usage.used : usage.used + requested_units;
                if (next_used > limit) {
                    result.code = ToCode(SdkStatusCode::UsageLimitExceeded);
                    result.message = "trial quota exhausted";
                    result.remaining = limit > usage.used ? limit - usage.used : 0;
                    cached_context.quota_buckets = BuildOfflineQuotaBuckets(parsed, record, false);
                    result.auth_context = FinalizeAuthContext(cached_context);
                    return result;
                }
                if (!duplicate) {
                    usage.used = next_used;
                    if (!request.request_id.empty()) {
                        usage.request_ids.insert(request.request_id);
                    }
                    if (!SaveOfflineEntitlement(parsed, record)) {
                        result.code = ToCode(SdkStatusCode::InternalError);
                        result.message = "failed to persist trial quota";
                        return result;
                    }
                }
                cached_context.quota_buckets = BuildOfflineQuotaBuckets(parsed, record, false);
                online_auth_contexts_[parsed.token_digest] = cached_context;
            }
            result.code = ToCode(SdkStatusCode::Ok);
            result.message = "ok";
            result.consumed = true;
            result.auth_context = FinalizeAuthContext(cached_context);
            const std::vector<AuthQuotaBucket>& quota_buckets = result.auth_context.quota_buckets;
            for (std::vector<AuthQuotaBucket>::const_iterator it = quota_buckets.begin();
                 it != quota_buckets.end();
                 ++it) {
                if (it->bucket == kTrialVipQuotaBucket) {
                    result.remaining = it->remaining;
                    break;
                }
            }
            return result;
        }

        nlohmann::json consume_request = nlohmann::json{
            {"apiKey", request.token},
            {"capability", request.capability},
            {"idempotencyKey", request.request_id},
            {"usageAmount", request.units > 0 ? request.units : 1},
        };
        const OnlineCallResult online_result =
            PostOnlineAuthJson(ResolveAuthzBaseUrl(request.authz_base_url), kAuthzConsumeConfirmPath, consume_request);
        result.code = online_result.code;
        result.message = online_result.message;
        const bool accepted = JsonBoolOr(online_result.body, "accepted", online_result.allowed);
        const bool duplicate = JsonBoolOr(online_result.body, "duplicate", false);
        if (online_result.code == 0 && (accepted || duplicate)) {
            result.consumed = true;
            result.bucket = JsonStringOr(online_result.body, "bucket", bucket_name);
            result.limit = JsonIntOr(online_result.body, "limit", 0);
            result.remaining = JsonIntOr(online_result.body, "remainingQuota", JsonIntOr(online_result.body, "remaining", 0));

            AuthContext updated_context;
            if (cached_context.is_valid) {
                updated_context = cached_context;
                updated_context.expires_at = JsonInt64Or(online_result.body, "expires_at", updated_context.expires_at);
                const nlohmann::json quota_buckets_json = JsonArrayOr(online_result.body, "quota_buckets");
                if (!quota_buckets_json.empty()) {
                    updated_context.quota_buckets = ParseQuotaBucketsJson(quota_buckets_json);
                }
            } else {
                updated_context = BuildOnlineAuthContext(
                    parsed, online_result.body, DevicesMetadataEntitlementTier(BuildDevicesMetadataJson()));
            }
            result.auth_context = FinalizeAuthContext(updated_context);
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                online_auth_contexts_[parsed.token_digest] = result.auth_context;
            }
            return result;
        }
        if (online_result.code == 0) {
            result.code = ToCode(SdkStatusCode::UsageLimitExceeded);
        }
        if (!online_result.allowed &&
            (result.code == ToCode(SdkStatusCode::Ok) || result.code == ToCode(SdkStatusCode::TokenInvalid))) {
            result.code = ToCode(SdkStatusCode::UsageLimitExceeded);
        }
        if (result.message.empty()) {
            result.message = "usage limit exceeded";
        }
        return result;
    }

    std::string machine_code;
    std::string machine_code_error;
    if (!GetLocalMachineCode(&machine_code, &machine_code_error)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = machine_code_error.empty() ? "machine code unavailable" : machine_code_error;
        return result;
    }

    OfflineEntitlementRecord record;
    HiddenPluginAuthState hidden_state;
    const SdkAccountType device_entitlement_tier =
        DevicesMetadataEntitlementTier(BuildDevicesMetadataJson());
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        record = LoadOfflineEntitlement(parsed, NULL);
        hidden_state = LoadHiddenPluginAuthState(parsed);
        ApplyOfflineRecordHostActivation(record, &hidden_state);
        const bool formally_unlocked =
            hidden_state.unlocked || DeviceTierCanAuthorize(device_entitlement_tier, parsed.auth_context.licensed_account_type);
        const int limit = DefaultQuotaLimitForBucket(EffectiveOfflineQuotaAccountType(parsed, formally_unlocked), bucket_name);
        result.bucket = bucket_name;
        result.limit = limit;
        if (!formally_unlocked) {
            OfflineQuotaUsage& usage = record.quota_usage[bucket_name];
            const bool duplicate = !request.request_id.empty() &&
                                   usage.request_ids.find(request.request_id) != usage.request_ids.end();
            const int requested_units = request.units > 0 ? request.units : 1;
            const int next_used = duplicate ? usage.used : usage.used + requested_units;
            if (next_used > limit) {
                result.code = ToCode(SdkStatusCode::UsageLimitExceeded);
                result.message = "offline quota exhausted";
                result.remaining = limit > usage.used ? limit - usage.used : 0;
                result.auth_context = FinalizeAuthContext(
                    BuildOfflineAuthContext(parsed, machine_code, record, hidden_state, device_entitlement_tier));
                return result;
            }
            if (!duplicate) {
                usage.used = next_used;
                if (!request.request_id.empty()) {
                    usage.request_ids.insert(request.request_id);
                }
            }
            if (!duplicate && !SaveOfflineEntitlement(parsed, record)) {
                result.code = ToCode(SdkStatusCode::InternalError);
                result.message = "failed to persist offline quota";
                return result;
            }
        }
    }

    result.code = 0;
    result.message = "ok";
    result.consumed = true;
    result.auth_context = FinalizeAuthContext(
        BuildOfflineAuthContext(parsed, machine_code, record, hidden_state, device_entitlement_tier));
    const std::vector<AuthQuotaBucket>& quota_buckets = result.auth_context.quota_buckets;
    for (std::vector<AuthQuotaBucket>::const_iterator it = quota_buckets.begin(); it != quota_buckets.end(); ++it) {
        if (it->bucket == bucket_name) {
            result.limit = it->limit;
            result.remaining = it->remaining;
            break;
        }
    }
    return result;
}

AuthCodeGenerateResult PrivateSdkAuthService::GenerateAuthCode(const std::string& machine_code,
                                                               const std::string& expires_date,
                                                               int auth_type) const {
    AuthCodeGenerateResult result;
    if (machine_code.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "machine_code required";
        return result;
    }
    if (!IsValidExpiresDate(expires_date)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "expires_date must be YYYYMMDD";
        return result;
    }
    if (!IsPluginCapableGrant(auth_type)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "auth_type must grant plugin access";
        return result;
    }

    std::string init_error;
    if (!EnsureAuthorizedSubsystem(&init_error)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = init_error.empty() ? "auth subsystem unavailable" : init_error;
        return result;
    }

    editor::auth::AuthManager& auth_manager = editor::auth::AuthManager::GetInstance();
    result.auth_code = auth_manager.GenerateAuthCode(editor::auth::LICENSE_KEY, machine_code, expires_date, auth_type);
    if (result.auth_code.empty()) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = auth_manager.GetLastError().empty() ? "generate auth code failed" : auth_manager.GetLastError();
        return result;
    }
    return result;
}

bool PrivateSdkAuthService::GenerateApiKey(const ApiKeySpec& spec, std::string* api_key, std::string* err) const {
    return key_codec_.GenerateApiKey(spec, api_key, err);
}

ParsedApiKey PrivateSdkAuthService::ParseApiKey(const std::string& api_key, std::int64_t now_ts) const {
    ParsedApiKey parsed;
    ParseApiKey(api_key, now_ts, &parsed);
    return parsed;
}

void PrivateSdkAuthService::ParseApiKey(const std::string& api_key,
                                        std::int64_t now_ts,
                                        ParsedApiKey* parsed) const {
    if (parsed == NULL) {
        return;
    }
    key_codec_.ParseAndValidateApiKey(api_key, now_ts, parsed);
    if (parsed->code == 0) {
        const AuthContext finalized = FinalizeAuthContext(parsed->auth_context);
        AssignFinalizedAuthContextFields(&parsed->auth_context, finalized);
    }
}

AuthContext PrivateSdkAuthService::FinalizeAuthContext(const AuthContext& auth_context) const {
    AuthContext finalized;
    finalized.is_valid = auth_context.is_valid;
    finalized.account_type = auth_context.account_type;
    finalized.account_type_code = auth_context.account_type_code;
    finalized.licensed_account_type = auth_context.licensed_account_type;
    finalized.licensed_account_type_code = auth_context.licensed_account_type_code;
    finalized.auth_scene = auth_context.auth_scene;
    finalized.license_mode = auth_context.license_mode;
    finalized.host_auth_mode = auth_context.host_auth_mode;
    finalized.entitlement_state = auth_context.entitlement_state;
    finalized.commercial_authorized = auth_context.commercial_authorized;
    finalized.machine_code = auth_context.machine_code;
    finalized.expires_at = auth_context.expires_at;
    try {
        finalized.device_scope = auth_context.device_scope;
    } catch (const std::exception& e) {
        SDK_PRIVATE_LOG_WARN("auth.finalize_context device_scope_copy_failed reason={}", e.what());
        finalized.device_scope.clear();
    }
    try {
        finalized.quota_buckets = auth_context.quota_buckets;
    } catch (const std::exception& e) {
        SDK_PRIVATE_LOG_WARN("auth.finalize_context quota_buckets_copy_failed reason={}", e.what());
        finalized.quota_buckets.clear();
    }
    if (finalized.licensed_account_type == SdkAccountType::Unknown) {
        finalized.licensed_account_type =
            finalized.account_type == SdkAccountType::Trial ? SdkAccountType::Unknown : finalized.account_type;
    }
    finalized.licensed_account_type_code = AccountTypeCode(finalized.licensed_account_type);
    finalized.commercial_authorized = auth_context.commercial_authorized;
    finalized.capabilities = BuildCapabilities(auth_context.account_type);
    finalized.account_type_code = AccountTypeCode(auth_context.account_type);
    finalized.is_valid = true;
    if (finalized.entitlement_state.empty()) {
        if (IsOnlineMode(finalized.license_mode)) {
            finalized.entitlement_state = kOnlineEntitlementValidated;
        } else if (RequiresHostActivation(finalized)) {
            finalized.entitlement_state = kOfflineEntitlementTrial;
        } else {
            finalized.entitlement_state = kOfflineEntitlementLimited;
        }
    }
    return finalized;
}

std::vector<std::string> PrivateSdkAuthService::BuildCapabilities(SdkAccountType account_type) const {
    return BuildEntitledCapabilities(account_type);
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
