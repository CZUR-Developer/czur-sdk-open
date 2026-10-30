#include "private_provider_factory.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#endif

#include "module/ofd/ofd_processor.h"
#include "module/pdf/mupdf_processor.h"
#include "module/twain/twain_types.h"
#if defined(_WIN32)
#include "module/twain/private_windows_twain_provider.h"
#endif
#include "nlohmann/json.hpp"
#include <opencv2/opencv.hpp>
#include "imgengine.h"
#include "ocr_common.h"
#include "ocr_manager.h"
#include "sdk_ocr_language.h"
#include "private_auth_provider.h"
#include "private_device_provider.h"
#include "private_graphic_provider.h"
#include "private_image_enhance_provider.h"
#include "private_recognition_provider.h"
#include "private_ocr_task_lifecycle.h"
#include "private_sane_provider.h"
#include "private_storage_cleanup.h"
#include "support/private_sdk_logger.h"
#include "auth/private_sdk_support.h"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

typedef void (*CzurSdkPrivateDeviceActionEventCallback)(const char* event_json, void* user_data);
typedef void (*CzurSdkPrivateDeviceEventCallback)(const char* event_json, void* user_data);
typedef void (*CzurSdkPrivateTwainSourceEventCallback)(const char* event_json, void* user_data);
typedef void (*CzurSdkPrivateTwainScanEventCallback)(const char* event_json, void* user_data);

struct CaptureWaitState {
    std::mutex mutex;
    std::condition_variable condition;
    SdkCaptureResult result;
    bool completed = false;
};

namespace mt = ::editor::module::twain;

char PathSeparator() {
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

bool IsPathSeparator(char ch) {
    return ch == '/' || ch == '\\';
}

std::string JoinPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") {
        return name;
    }
    if (IsPathSeparator(dir[dir.size() - 1])) {
        return dir + name;
    }
    return dir + PathSeparator() + name;
}

bool DirectoryExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool IsWindowsDriveRoot(const std::string& path) {
#if defined(_WIN32)
    if (path.size() < 3 ||
        !std::isalpha(static_cast<unsigned char>(path[0])) ||
        path[1] != ':') {
        return false;
    }
    for (std::string::size_type i = 2; i < path.size(); ++i) {
        if (!IsPathSeparator(path[i])) {
            return false;
        }
    }
    return true;
#else
    (void)path;
    return false;
#endif
}

bool IsWindowsDriveOnlyPath(const std::string& path) {
#if defined(_WIN32)
    return path.size() == 2 &&
           std::isalpha(static_cast<unsigned char>(path[0])) &&
           path[1] == ':';
#else
    (void)path;
    return false;
#endif
}

std::string NormalizeWindowsDriveRootPath(const std::string& path) {
#if defined(_WIN32)
    return path.substr(0, 2) + "\\";
#else
    return path;
#endif
}

const char* DuplicateString(const std::string& value) {
    char* output = static_cast<char*>(std::malloc(value.size() + 1));
    if (output == NULL) {
        return NULL;
    }
    std::memcpy(output, value.c_str(), value.size() + 1);
    return output;
}

const char* JsonResponse(const nlohmann::json& response) {
    return DuplicateString(response.dump());
}

nlohmann::json BuildProviderResponse(int code, const std::string& message) {
    return nlohmann::json{
        {"code", code},
        {"message", message},
    };
}

const char* ProviderErrorResponse(int code, const std::string& message) {
    return JsonResponse(BuildProviderResponse(code, message));
}

bool ParseProviderRequestJson(const char* request_json, nlohmann::json* request, const char** message) {
    if (request == NULL || message == NULL) {
        return false;
    }
    if (request_json == NULL || request_json[0] == '\0') {
        *message = "request json required";
        return false;
    }
    *request = nlohmann::json::parse(request_json, NULL, false);
    if (request->is_discarded() || !request->is_object()) {
        *message = "request json invalid";
        return false;
    }
    return true;
}

nlohmann::json VideoResolutionToJson(const SdkVideoResolution& resolution) {
    return nlohmann::json{
        {"width", resolution.width},
        {"height", resolution.height},
        {"real_width", resolution.real_width},
        {"real_height", resolution.real_height},
        {"fps", resolution.fps},
        {"pixel_format", resolution.pixel_format},
        {"is_default", resolution.is_default},
    };
}

nlohmann::json OutputTargetSizeOptionToJson(const SdkOutputTargetSizeOption& option) {
    nlohmann::json value = {
        {"target_size", option.target_size},
        {"width", option.width},
        {"height", option.height},
    };
    if (option.is_device_default) {
        value["is_device_default"] = true;
    }
    return value;
}

nlohmann::json CaptureOutputCapabilitiesToJson(const SdkCaptureOutputCapabilities& capabilities) {
    nlohmann::json target_sizes = nlohmann::json::array();
    for (std::vector<SdkOutputTargetSizeOption>::const_iterator it = capabilities.target_sizes.begin();
         it != capabilities.target_sizes.end();
         ++it) {
        target_sizes.push_back(OutputTargetSizeOptionToJson(*it));
    }
    return nlohmann::json{
        {"target_size_supported", capabilities.target_size_supported},
        {"target_sizes", target_sizes},
    };
}

nlohmann::json DeviceDescriptorToJson(const SdkDeviceDescriptor& device) {
    nlohmann::json resolutions = nlohmann::json::array();
    for (std::vector<SdkVideoResolution>::const_iterator it = device.resolutions.begin();
         it != device.resolutions.end();
         ++it) {
        resolutions.push_back(VideoResolutionToJson(*it));
    }

    return nlohmann::json{
        {"device_id", device.device_id},
        {"model", device.model},
        {"display_name", device.display_name},
        {"vid", device.vid},
        {"pid", device.pid},
        {"status", device.status},
        {"authorized", device.authorized},
        {"supports_video", device.supports_video},
        {"image_transfer_protocol", device.image_transfer_protocol},
        {"resolutions", resolutions},
        {"capture_output", CaptureOutputCapabilitiesToJson(device.capture_output)},
    };
}

nlohmann::json VideoStartResultToJson(const SdkVideoStartResult& result) {
    return nlohmann::json{{"code", result.code},
                          {"message", result.message},
                          {"accepted", result.accepted},
                          {"pixel_format", result.pixel_format},
                          {"width", result.width},
                          {"height", result.height},
                          {"fps", result.fps}};
}

PrivateDeviceProvider& SharedPrivateDeviceProvider() {
    static PrivateDeviceProvider provider;
    return provider;
}

#if defined(_WIN32)
std::shared_ptr<mt::ITwainProvider> SharedPrivateTwainProvider() {
    // 真实 TWAIN provider 生命周期放在 private/module 层共享，sdk_open 只通过 DLL C API 间接调用。
    static std::shared_ptr<mt::ITwainProvider> provider = mt::CreateWindowsTwainProvider();
    return provider;
}
#endif

nlohmann::json CaptureResultToJson(const SdkCaptureResult& result);

nlohmann::json DeviceActionEventToJson(const SdkDeviceActionEvent& event) {
    nlohmann::json response = {
        {"device_id", event.device_id},
        {"type", event.type == SdkDeviceActionType::HardGrab ? "hardgrab" : "turn_detect"},
        {"auto_capture", event.auto_capture},
        {"timestamp_ms", event.timestamp_ms},
    };
    if (event.type == SdkDeviceActionType::HardGrab) {
        response["capture"] = CaptureResultToJson(event.capture);
    }
    return response;
}

nlohmann::json DeviceEventToJson(const SdkDeviceEvent& event) {
    return nlohmann::json{
        {"device_id", event.device_id},
        {"type", event.type},
        {"reason", event.reason},
        {"was_opened", event.was_opened},
        {"was_streaming", event.was_streaming},
        {"timestamp_ms", event.timestamp_ms},
    };
}

nlohmann::json PointToJson(const SdkPoint2f& point) {
    return nlohmann::json{{"x", point.x}, {"y", point.y}};
}

nlohmann::json RectToJson(const SdkRect4P& rect) {
    return nlohmann::json{{"left_top", PointToJson(rect.left_top)},
                          {"right_top", PointToJson(rect.right_top)},
                          {"right_down", PointToJson(rect.right_down)},
                          {"left_down", PointToJson(rect.left_down)}};
}

nlohmann::json CaptureResultToJson(const SdkCaptureResult& result) {
    nlohmann::json rects = nlohmann::json::array();
    for (std::vector<SdkRect4P>::const_iterator it = result.detected_rects.begin();
         it != result.detected_rects.end();
         ++it) {
        rects.push_back(RectToJson(*it));
    }
    nlohmann::json response = nlohmann::json{{"code", result.code},
                                             {"message", result.message},
                                             {"captured", result.captured},
                                             {"output_path", result.output_path},
                                             {"original_path", result.original_path},
                                             {"laser_path", result.laser_path},
                                             {"content_type", result.content_type},
                                             {"payload", result.payload},
                                             {"width", result.width},
                                             {"height", result.height},
                                             {"dpi", result.dpi},
                                             {"size", result.size},
                                             {"detected_rects", rects},
                                             {"detected_rects_source_width", result.detected_rects_source_width},
                                             {"detected_rects_source_height", result.detected_rects_source_height},
                                             {"scan_device_type", result.scan_device_type}};
    return response;
}

float JsonFloatField(const nlohmann::json& object, const char* key, float fallback = 0.0f) {
    if (!object.is_object()) {
        return fallback;
    }
    nlohmann::json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<float>() : fallback;
}

int JsonIntField(const nlohmann::json& object, const char* key, int fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    nlohmann::json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

uint64_t JsonUInt64Field(const nlohmann::json& object, const char* key, uint64_t fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    nlohmann::json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number_unsigned() ? it->get<uint64_t>() : fallback;
}

bool JsonBoolField(const nlohmann::json& object, const char* key, bool fallback = false) {
    if (!object.is_object()) {
        return fallback;
    }
    nlohmann::json::const_iterator it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

std::string JsonStringField(const nlohmann::json& object, const char* key, const std::string& fallback = "") {
    if (!object.is_object()) {
        return fallback;
    }
    nlohmann::json::const_iterator it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

std::vector<std::string> JsonStringArrayField(const nlohmann::json& object, const char* key) {
    std::vector<std::string> values;
    if (!object.is_object()) {
        return values;
    }
    nlohmann::json::const_iterator array_it = object.find(key);
    if (array_it == object.end() || !array_it->is_array()) {
        return values;
    }
    for (nlohmann::json::const_iterator it = array_it->begin(); it != array_it->end(); ++it) {
        if (it->is_string()) {
            values.push_back(it->get<std::string>());
        }
    }
    return values;
}

nlohmann::json TwainAssetToJson(const mt::TwainAsset& asset) {
    return nlohmann::json{{"asset_id", asset.asset_id},
                          {"kind", asset.kind},
                          {"path", asset.path},
                          {"url", asset.url},
                          {"download_url", asset.download_url},
                          {"content_type", asset.content_type},
                          {"width", asset.width},
                          {"height", asset.height},
                          {"size", asset.size}};
}

nlohmann::json TwainSourceToJson(const mt::TwainSource& source) {
    return nlohmann::json{{"source_id", source.source_id},
                          {"source_name", source.source_name},
                          {"manufacturer", source.manufacturer},
                          {"product_family", source.product_family},
                          {"product_name", source.product_name},
                          {"protocol_major", source.protocol_major},
                          {"protocol_minor", source.protocol_minor},
                          {"status", source.status},
                          {"openable", source.openable}};
}

nlohmann::json TwainSourcesToJson(const std::vector<mt::TwainSource>& sources) {
    nlohmann::json values = nlohmann::json::array();
    for (std::vector<mt::TwainSource>::const_iterator it = sources.begin(); it != sources.end(); ++it) {
        values.push_back(TwainSourceToJson(*it));
    }
    return values;
}

nlohmann::json TwainCapabilityConstraintToJson(const mt::TwainCapabilityConstraint& constraint) {
    return nlohmann::json{{"type", constraint.type},
                          {"min", constraint.min},
                          {"max", constraint.max},
                          {"quant", constraint.quant},
                          {"values_json", constraint.values_json}};
}

nlohmann::json TwainCapabilityToJson(const mt::TwainCapability& capability) {
    return nlohmann::json{{"cap", capability.cap},
                          {"cap_id", capability.cap_id},
                          {"name", capability.name},
                          {"title", capability.title},
                          {"type", capability.type},
                          {"value_json", capability.value_json},
                          {"constraint", TwainCapabilityConstraintToJson(capability.constraint)},
                          {"readonly", capability.readonly},
                          {"settable", capability.settable},
                          {"advanced", capability.advanced}};
}

mt::TwainCapabilitySetItem TwainCapabilitySetItemFromJson(const nlohmann::json& value) {
    mt::TwainCapabilitySetItem item;
    item.cap = JsonStringField(value, "cap");
    item.name = JsonStringField(value, "name");
    if (value.is_object()) {
        nlohmann::json::const_iterator value_json_it = value.find("value_json");
        if (value_json_it != value.end() && value_json_it->is_string()) {
            item.value_json = value_json_it->get<std::string>();
        } else {
            nlohmann::json::const_iterator value_it = value.find("value");
            if (value_it != value.end()) {
                item.value_json = value_it->dump();
            }
        }
    }
    return item;
}

std::vector<mt::TwainCapabilitySetItem> TwainCapabilitySetItemsFromJson(const nlohmann::json& value) {
    std::vector<mt::TwainCapabilitySetItem> items;
    if (!value.is_array()) {
        return items;
    }
    for (nlohmann::json::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        mt::TwainCapabilitySetItem item = TwainCapabilitySetItemFromJson(*it);
        if (!item.cap.empty() || !item.name.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

nlohmann::json TwainCapabilitySetItemToJson(const mt::TwainCapabilitySetItem& item) {
    return nlohmann::json{{"cap", item.cap},
                          {"name", item.name},
                          {"value_json", item.value_json}};
}

nlohmann::json TwainCapabilitySetResultItemToJson(const mt::TwainCapabilitySetResultItem& item) {
    return nlohmann::json{{"cap", item.cap},
                          {"name", item.name},
                          {"status", item.status},
                          {"message", item.message},
                          {"value_json", item.value_json}};
}

nlohmann::json TwainProfileToJson(const mt::TwainProfile& profile) {
    nlohmann::json capabilities = nlohmann::json::array();
    for (std::vector<mt::TwainCapabilitySetItem>::const_iterator it = profile.capabilities.begin();
         it != profile.capabilities.end();
         ++it) {
        capabilities.push_back(TwainCapabilitySetItemToJson(*it));
    }
    return nlohmann::json{{"profile_id", profile.profile_id},
                          {"source_key", profile.source_key},
                          {"name", profile.name},
                          {"capabilities", capabilities},
                          {"created_at", profile.created_at},
                          {"updated_at", profile.updated_at}};
}

nlohmann::json TwainScanTaskToJson(const mt::TwainScanTask& task) {
    nlohmann::json assets = nlohmann::json::array();
    for (std::vector<mt::TwainAsset>::const_iterator it = task.assets.begin(); it != task.assets.end(); ++it) {
        assets.push_back(TwainAssetToJson(*it));
    }
    return nlohmann::json{{"task_id", task.task_id},
                          {"connection_id", task.connection_id},
                          {"session_id", task.session_id},
                          {"status", task.status},
                          {"phase", task.phase},
                          {"progress", task.progress},
                          {"page_count", task.page_count},
                          {"current_page", task.current_page},
                          {"output_type", task.output_type},
                          {"output_format", task.output_format},
                          {"output_dir", task.output_dir},
                          {"export_type", task.export_type},
                          {"output_path", task.output_path},
                          {"output_paths", task.output_paths},
                          {"last_page_path", task.last_page_path},
                          {"assets", assets},
                          {"message", task.message},
                          {"error", task.error},
                          {"started_at", task.started_at},
                          {"updated_at", task.updated_at},
                          {"cancel_requested", task.cancel_requested},
                          {"ui_required", task.ui_required},
                          {"transfer_mechanism", task.transfer_mechanism}};
}

nlohmann::json TwainSourceEventToJson(const mt::TwainSourceEvent& event) {
    return nlohmann::json{{"code", event.code},
                          {"message", event.message},
                          {"connection_id", event.connection_id},
                          {"event_name", event.event_name},
                          {"generation", event.generation},
                          {"sources", TwainSourcesToJson(event.sources)},
                          {"added_sources", TwainSourcesToJson(event.added_sources)},
                          {"removed_sources", TwainSourcesToJson(event.removed_sources)}};
}

nlohmann::json TwainScanTaskEventToJson(const mt::TwainScanTaskEvent& event) {
    return nlohmann::json{{"code", event.code},
                          {"message", event.message},
                          {"connection_id", event.connection_id},
                          {"event_name", event.event_name},
                          {"task", TwainScanTaskToJson(event.task)}};
}

mt::TwainProfileRequest TwainProfileRequestFromJson(const nlohmann::json& request) {
    mt::TwainProfileRequest value;
    value.session_id = JsonStringField(request, "session_id");
    value.source_id = JsonStringField(request, "source_id");
    value.source_key = JsonStringField(request, "source_key");
    value.profile_id = JsonStringField(request, "profile_id");
    value.name = JsonStringField(request, "name");
    value.capabilities = TwainCapabilitySetItemsFromJson(request.value("capabilities", nlohmann::json::array()));
    return value;
}

mt::TwainScanRequest TwainScanRequestFromJson(const nlohmann::json& request) {
    mt::TwainScanRequest value;
    value.connection_id = JsonStringField(request, "connection_id");
    value.session_id = JsonStringField(request, "session_id");
    value.show_ui = JsonBoolField(request, "show_ui");
    value.transfer_mechanism = JsonStringField(request, "transfer_mechanism", value.transfer_mechanism);
    value.output_type = JsonStringField(request, "output_type", value.output_type);
    value.output_format = JsonStringField(request, "output_format");
    value.output_path = JsonStringField(request, "output_path");
    value.output_dir = JsonStringField(request, "output_dir");
    value.export_type = JsonStringField(request, "export_type", value.export_type);
    value.capabilities = TwainCapabilitySetItemsFromJson(request.value("capabilities", nlohmann::json::array()));
    return value;
}

nlohmann::json TwainUnavailableResponse() {
    return BuildProviderResponse(ToCode(SdkStatusCode::TwainNotAvailable),
                                 "TWAIN provider is only available in Windows private provider runtime");
}

std::shared_ptr<mt::ITwainProvider> GetPrivateTwainProviderForCall() {
#if defined(_WIN32)
    return SharedPrivateTwainProvider();
#else
    return std::shared_ptr<mt::ITwainProvider>();
#endif
}

SdkPoint2f PointFromJson(const nlohmann::json& value) {
    SdkPoint2f point;
    point.x = JsonFloatField(value, "x");
    point.y = JsonFloatField(value, "y");
    return point;
}

SdkRect4P RectFromJson(const nlohmann::json& value) {
    SdkRect4P rect;
    rect.left_top = PointFromJson(value.value("left_top", nlohmann::json::object()));
    rect.right_top = PointFromJson(value.value("right_top", nlohmann::json::object()));
    rect.right_down = PointFromJson(value.value("right_down", nlohmann::json::object()));
    rect.left_down = PointFromJson(value.value("left_down", nlohmann::json::object()));
    return rect;
}

SdkCropBorderOptions CropBorderFromJson(const nlohmann::json& value) {
    SdkCropBorderOptions options;
    options.enabled = JsonBoolField(value, "enabled");
    options.width = JsonIntField(value, "width");
    options.height = JsonIntField(value, "height");
    return options;
}

SdkCropCenterOptions CropCenterFromJson(const nlohmann::json& value) {
    SdkCropCenterOptions options;
    options.enabled = JsonBoolField(value, "enabled");
    options.margin = JsonIntField(value, "margin");
    return options;
}

SdkSinglePageOptions SinglePageFromJson(const nlohmann::json& value) {
    SdkSinglePageOptions options;
    options.realtime_detect_rects = JsonBoolField(value, "realtime_detect_rects");
    options.crop_border = CropBorderFromJson(value.value("crop_border", nlohmann::json::object()));
    options.id_card_round_corner = JsonBoolField(value, "id_card_round_corner");
    options.auto_rotate = JsonBoolField(value, "auto_rotate");
    options.smart_black_edge_optimize = JsonBoolField(value, "smart_black_edge_optimize", true);
    options.multi_target_paging = JsonBoolField(value, "multi_target_paging");
    return options;
}

SdkCurvedBookOptions CurvedBookFromJson(const nlohmann::json& value) {
    SdkCurvedBookOptions options;
    options.remove_finger = JsonBoolField(value, "remove_finger", true);
    options.finger_type = JsonStringField(value, "finger_type", "with_sleeve");
    options.smart_paging = JsonBoolField(value, "smart_paging", true);
    options.crop_border = CropBorderFromJson(value.value("crop_border", nlohmann::json::object()));
    options.crop_center = CropCenterFromJson(value.value("crop_center", nlohmann::json::object()));
    options.auto_complete = JsonBoolField(value, "auto_complete");
    return options;
}

nlohmann::json CropBorderToJson(const SdkCropBorderOptions& options) {
    return nlohmann::json{{"enabled", options.enabled}, {"width", options.width}, {"height", options.height}};
}

nlohmann::json CropCenterToJson(const SdkCropCenterOptions& options) {
    return nlohmann::json{{"enabled", options.enabled}, {"margin", options.margin}};
}

nlohmann::json SinglePageToJson(const SdkSinglePageOptions& options) {
    return nlohmann::json{{"realtime_detect_rects", options.realtime_detect_rects},
                          {"crop_border", CropBorderToJson(options.crop_border)},
                          {"id_card_round_corner", options.id_card_round_corner},
                          {"auto_rotate", options.auto_rotate},
                          {"smart_black_edge_optimize", options.smart_black_edge_optimize},
                          {"multi_target_paging", options.multi_target_paging}};
}

nlohmann::json CurvedBookToJson(const SdkCurvedBookOptions& options) {
    return nlohmann::json{{"remove_finger", options.remove_finger},
                          {"finger_type", options.finger_type},
                          {"smart_paging", options.smart_paging},
                          {"crop_border", CropBorderToJson(options.crop_border)},
                          {"crop_center", CropCenterToJson(options.crop_center)},
                          {"auto_complete", options.auto_complete}};
}

nlohmann::json ImageProcessOutputToJson(const SdkImageProcessOutput& output) {
    return nlohmann::json{{"asset_id", output.asset_id},
                          {"output_id", output.output_id},
                          {"role", output.role},
                          {"index", output.index},
                          {"path", output.path},
                          {"url", output.url},
                          {"download_url", output.download_url},
                          {"content_type", output.content_type},
                          {"width", output.width},
                          {"height", output.height},
                          {"size", output.size}};
}

nlohmann::json PageOutputToJson(const SdkPageOutput& output) {
    return nlohmann::json{{"output_id", output.output_id},
                          {"role", output.role},
                          {"index", output.index},
                          {"path", output.path},
                          {"content_type", output.content_type},
                          {"width", output.width},
                          {"height", output.height},
                          {"dpi", output.dpi},
                          {"size", output.size}};
}

nlohmann::json BarcodeToJson(const SdkBarcodeResult& barcode) {
    nlohmann::json points = nlohmann::json::array();
    for (std::vector<SdkPoint2f>::const_iterator it = barcode.points.begin();
         it != barcode.points.end();
         ++it) {
        points.push_back(PointToJson(*it));
    }
    return nlohmann::json{{"format_name", barcode.format_name},
                          {"format", barcode.format},
                          {"text", barcode.text},
                          {"points", points}};
}

SdkImageProcessRequest ImageProcessRequestFromJson(const nlohmann::json& request) {
    SdkImageProcessRequest value;
    value.input_upload_id = JsonStringField(request, "input_upload_id");
    value.input_path = JsonStringField(request, "input_path");
    value.output_path = JsonStringField(request, "output_path");
    value.output_dir = JsonStringField(request, "output_dir");
    value.page_processing = JsonStringField(request, "page_processing", value.page_processing);
    value.color_mode = JsonStringField(request, "color_mode", value.color_mode);
    value.output_format = JsonStringField(request, "output_format");
    value.dpi = JsonIntField(request, "dpi");
    value.single_page = SinglePageFromJson(request.value("single_page", nlohmann::json::object()));
    value.curved_book = CurvedBookFromJson(request.value("curved_book", nlohmann::json::object()));
    value.selected_area_rect = RectFromJson(request.value("selected_area_rect", nlohmann::json::object()));
    value.selected_area_source_width = JsonIntField(request, "selected_area_source_width");
    value.selected_area_source_height = JsonIntField(request, "selected_area_source_height");
    value.scan_device_type = JsonIntField(request, "scan_device_type");
    return value;
}

SdkPageProcessRequest PageProcessRequestFromJson(const nlohmann::json& request) {
    SdkPageProcessRequest value;
    value.device_id = JsonStringField(request, "device_id");
    value.input_path = JsonStringField(request, "input_path");
    value.laser_path = JsonStringField(request, "laser_path");
    value.output_dir = JsonStringField(request, "output_dir");
    value.output_path = JsonStringField(request, "output_path");
    value.page_processing = JsonStringField(request, "page_processing");
    value.width = JsonIntField(request, "width");
    value.height = JsonIntField(request, "height");
    value.dpi = JsonIntField(request, "dpi");
    value.single_page_realtime_detect_rects = JsonBoolField(request, "single_page_realtime_detect_rects");
    value.single_page = SinglePageFromJson(request.value("single_page", nlohmann::json::object()));
    value.curved_book = CurvedBookFromJson(request.value("curved_book", nlohmann::json::object()));
    nlohmann::json::const_iterator rects_it = request.find("detected_rects");
    if (rects_it != request.end() && rects_it->is_array()) {
        for (nlohmann::json::const_iterator it = rects_it->begin(); it != rects_it->end(); ++it) {
            value.detected_rects.push_back(RectFromJson(*it));
        }
    }
    value.detected_rects_source_width = JsonIntField(request, "detected_rects_source_width");
    value.detected_rects_source_height = JsonIntField(request, "detected_rects_source_height");
    value.selected_area_rect = RectFromJson(request.value("selected_area_rect", nlohmann::json::object()));
    value.selected_area_source_width = JsonIntField(request, "selected_area_source_width");
    value.selected_area_source_height = JsonIntField(request, "selected_area_source_height");
    value.scan_device_type = JsonIntField(request, "scan_device_type");
    return value;
}

SdkColorModeRequest ColorModeRequestFromJson(const nlohmann::json& request) {
    SdkColorModeRequest value;
    value.input_path = JsonStringField(request, "input_path");
    value.output_path = JsonStringField(request, "output_path");
    value.color_mode = JsonStringField(request, "color_mode");
    value.dpi = JsonIntField(request, "dpi");
    return value;
}

SdkFormatConvertRequest FormatConvertRequestFromJson(const nlohmann::json& request) {
    SdkFormatConvertRequest value;
    value.input_path = JsonStringField(request, "input_path");
    value.output_path = JsonStringField(request, "output_path");
    value.output_format = JsonStringField(request, "output_format");
    value.quality = JsonIntField(request, "quality");
    value.scale = JsonFloatField(request, "scale", value.scale);
    return value;
}

SdkFileConvertRequest FileConvertRequestFromJson(const nlohmann::json& request) {
    SdkFileConvertRequest value;
    value.input_upload_id = JsonStringField(request, "input_upload_id");
    value.input_upload_ids = JsonStringArrayField(request, "input_upload_ids");
    value.input_path = JsonStringField(request, "input_path");
    value.input_paths = JsonStringArrayField(request, "input_paths");
    value.output_path = JsonStringField(request, "output_path");
    value.output_dir = JsonStringField(request, "output_dir");
    value.output_format = JsonStringField(request, "output_format");
    value.source_type = JsonStringField(request, "source_type", value.source_type);
    value.source_format = JsonStringField(request, "source_format");
    value.target_type = JsonStringField(request, "target_type");
    value.export_type = JsonStringField(request, "export_type", value.export_type);
    value.pages = JsonStringField(request, "pages", value.pages);
    value.quality = JsonIntField(request, "quality", value.quality);
    value.render_dpi = JsonIntField(request, "render_dpi", value.render_dpi);
    value.tiff_color = JsonStringField(request, "tiff_color", value.tiff_color);
    value.tiff_compression = JsonStringField(request, "tiff_compression", value.tiff_compression);
    return value;
}

SdkBarcodeDetectRequest BarcodeDetectRequestFromJson(const nlohmann::json& request) {
    SdkBarcodeDetectRequest value;
    value.input_upload_id = JsonStringField(request, "input_upload_id");
    value.input_path = JsonStringField(request, "input_path");
    value.formats = JsonStringArrayField(request, "formats");
    return value;
}

nlohmann::json ImageProcessRequestToJson(const SdkImageProcessRequest& request) {
    return nlohmann::json{{"input_upload_id", request.input_upload_id},
                          {"input_path", request.input_path},
                          {"output_path", request.output_path},
                          {"output_dir", request.output_dir},
                          {"page_processing", request.page_processing},
                          {"color_mode", request.color_mode},
                          {"output_format", request.output_format},
                          {"dpi", request.dpi},
                          {"single_page", SinglePageToJson(request.single_page)},
                          {"curved_book", CurvedBookToJson(request.curved_book)},
                          {"selected_area_rect", RectToJson(request.selected_area_rect)},
                          {"selected_area_source_width", request.selected_area_source_width},
                          {"selected_area_source_height", request.selected_area_source_height},
                          {"scan_device_type", request.scan_device_type}};
}

nlohmann::json PageProcessRequestToJson(const SdkPageProcessRequest& request) {
    nlohmann::json rects = nlohmann::json::array();
    for (std::vector<SdkRect4P>::const_iterator it = request.detected_rects.begin();
         it != request.detected_rects.end();
         ++it) {
        rects.push_back(RectToJson(*it));
    }
    return nlohmann::json{{"device_id", request.device_id},
                          {"input_path", request.input_path},
                          {"laser_path", request.laser_path},
                          {"output_dir", request.output_dir},
                          {"output_path", request.output_path},
                          {"page_processing", request.page_processing},
                          {"width", request.width},
                          {"height", request.height},
                          {"dpi", request.dpi},
                          {"single_page_realtime_detect_rects", request.single_page_realtime_detect_rects},
                          {"single_page", SinglePageToJson(request.single_page)},
                          {"curved_book", CurvedBookToJson(request.curved_book)},
                          {"detected_rects", rects},
                          {"detected_rects_source_width", request.detected_rects_source_width},
                          {"detected_rects_source_height", request.detected_rects_source_height},
                          {"selected_area_rect", RectToJson(request.selected_area_rect)},
                          {"selected_area_source_width", request.selected_area_source_width},
                          {"selected_area_source_height", request.selected_area_source_height},
                          {"scan_device_type", request.scan_device_type}};
}

nlohmann::json ColorModeRequestToJson(const SdkColorModeRequest& request) {
    return nlohmann::json{{"input_path", request.input_path},
                          {"output_path", request.output_path},
                          {"color_mode", request.color_mode},
                          {"dpi", request.dpi}};
}

nlohmann::json FormatConvertRequestToJson(const SdkFormatConvertRequest& request) {
    return nlohmann::json{{"input_path", request.input_path},
                          {"output_path", request.output_path},
                          {"output_format", request.output_format},
                          {"quality", request.quality},
                          {"scale", request.scale}};
}

nlohmann::json FileConvertRequestToJson(const SdkFileConvertRequest& request) {
    return nlohmann::json{{"input_upload_id", request.input_upload_id},
                          {"input_upload_ids", request.input_upload_ids},
                          {"input_path", request.input_path},
                          {"input_paths", request.input_paths},
                          {"output_path", request.output_path},
                          {"output_dir", request.output_dir},
                          {"output_format", request.output_format},
                          {"source_type", request.source_type},
                          {"source_format", request.source_format},
                          {"target_type", request.target_type},
                          {"export_type", request.export_type},
                          {"pages", request.pages},
                          {"quality", request.quality},
                          {"render_dpi", request.render_dpi},
                          {"tiff_color", request.tiff_color},
                          {"tiff_compression", request.tiff_compression}};
}

nlohmann::json BarcodeDetectRequestToJson(const SdkBarcodeDetectRequest& request) {
    return nlohmann::json{{"input_upload_id", request.input_upload_id},
                          {"input_path", request.input_path},
                          {"formats", request.formats}};
}

nlohmann::json OcrTaskToJson(const SdkOcrTaskSnapshot& task) {
    return nlohmann::json{{"task_id", task.task_id},
                          {"status", task.status},
                          {"progress", task.progress},
                          {"output_path", task.output_path},
                          {"output_paths", task.output_paths},
                          {"format", task.format},
                          {"export_type", task.export_type},
                          {"message", task.message},
                          {"error", task.error}};
}

nlohmann::json OcrTextBlockToJson(const SdkOcrTextBlock& block) {
    return nlohmann::json{{"text", block.text},
                          {"x", block.x},
                          {"y", block.y},
                          {"width", block.width},
                          {"height", block.height},
                          {"confidence", block.confidence},
                          {"font_size", block.font_size}};
}

SdkOcrRecognizeRequest OcrRecognizeRequestFromJson(const nlohmann::json& request) {
    SdkOcrRecognizeRequest value;
    value.input_upload_ids = JsonStringArrayField(request, "input_upload_ids");
    value.input_files = JsonStringArrayField(request, "input_files");
    value.output_path = JsonStringField(request, "output_path");
    value.output_dir = JsonStringField(request, "output_dir");
    value.format = JsonStringField(request, "format", value.format);
    value.export_type = JsonStringField(request, "export_type", value.export_type);
    value.ext_params_json = JsonStringField(request, "ext_params_json");
    return value;
}

nlohmann::json ImageEnhanceCapabilityToJson(const SdkImageEnhanceCapability& capability) {
    return nlohmann::json{{"type", capability.type},
                          {"title", capability.title},
                          {"description", capability.description},
                          {"i18n_key", capability.i18n_key},
                          {"title_zh_cn", capability.title_zh_cn},
                          {"description_zh_cn", capability.description_zh_cn},
                          {"category", capability.category},
                          {"runtime", capability.runtime},
                          {"available", capability.available},
                          {"unavailable_reason", capability.unavailable_reason},
                          {"unavailable_reason_zh_cn", capability.unavailable_reason_zh_cn},
                          {"requires_capability", capability.requires_capability},
                          {"quota_unit", capability.quota_unit},
                          {"source_types", capability.source_types},
                          {"min_pages", capability.min_pages},
                          {"max_pages", capability.max_pages},
                          {"page_effect", capability.page_effect},
                          {"metadata", capability.metadata},
                          {"order_hint", capability.order_hint},
                          {"version", capability.version},
                          {"defaults_json", capability.defaults_json},
                          {"schema_json", capability.schema_json}};
}

nlohmann::json ImageEnhancePageToJson(const SdkImageEnhancePage& page) {
    return nlohmann::json{{"source_index", page.source_index},
                          {"output_index", page.output_index},
                          {"path", page.path},
                          {"dropped", page.dropped},
                          {"metadata_json", page.metadata_json}};
}

SdkImageEnhancePage ImageEnhancePageFromJson(const nlohmann::json& value) {
    SdkImageEnhancePage page;
    page.source_index = JsonIntField(value, "source_index");
    page.output_index = JsonIntField(value, "output_index");
    page.path = JsonStringField(value, "path");
    page.dropped = JsonBoolField(value, "dropped");
    page.metadata_json = JsonStringField(value, "metadata_json", page.metadata_json);
    return page;
}

SdkImageEnhanceStep ImageEnhanceStepFromJson(const nlohmann::json& value) {
    SdkImageEnhanceStep step;
    step.id = JsonStringField(value, "id");
    step.type = JsonStringField(value, "type");
    step.provider = JsonStringField(value, "provider", step.provider);
    step.enabled = JsonBoolField(value, "enabled", step.enabled);
    step.on_error = JsonStringField(value, "on_error", step.on_error);
    step.params_json = JsonStringField(value, "params_json", step.params_json);
    return step;
}

SdkImageEnhanceStepRequest ImageEnhanceStepRequestFromJson(const nlohmann::json& request) {
    SdkImageEnhanceStepRequest value;
    value.task_id = JsonStringField(request, "task_id");
    value.step = ImageEnhanceStepFromJson(request.value("step", nlohmann::json::object()));
    nlohmann::json::const_iterator pages_it = request.find("pages");
    if (pages_it != request.end() && pages_it->is_array()) {
        for (nlohmann::json::const_iterator it = pages_it->begin(); it != pages_it->end(); ++it) {
            value.pages.push_back(ImageEnhancePageFromJson(*it));
        }
    }
    value.output_dir = JsonStringField(request, "output_dir");
    value.online_api_key = JsonStringField(request, "online_api_key");
    value.online_base_url = JsonStringField(request, "online_base_url");
    return value;
}

std::string ParentPath(const std::string& path) {
    if (IsWindowsDriveOnlyPath(path) || IsWindowsDriveRoot(path)) {
        return path;
    }
    const std::string::size_type slash_pos = path.find_last_of("/\\");
    if (slash_pos == std::string::npos) {
        return ".";
    }
    if (slash_pos == 2 && path.size() >= 3 &&
        std::isalpha(static_cast<unsigned char>(path[0])) &&
        path[1] == ':') {
        return path.substr(0, 3);
    }
    if (slash_pos == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash_pos);
}

bool EnsureDirectoryRecursive(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (IsWindowsDriveOnlyPath(path)) {
        return DirectoryExists(path + "\\");
    }
    if (IsWindowsDriveRoot(path)) {
        return DirectoryExists(NormalizeWindowsDriveRootPath(path));
    }
    if (DirectoryExists(path)) {
        return true;
    }
    std::string current;
    std::size_t index = 0;
    if (IsPathSeparator(path[0])) {
        current = path.substr(0, 1);
        index = 1;
#if defined(_WIN32)
    } else if (path.size() >= 3 && path[1] == ':' && IsPathSeparator(path[2])) {
        current = path.substr(0, 3);
        index = 3;
#endif
    }
    while (index <= path.size()) {
        const std::size_t next = path.find_first_of("/\\", index);
        const std::string part = path.substr(index, next == std::string::npos ? std::string::npos : next - index);
        if (!part.empty()) {
            current = current.empty() || IsPathSeparator(current[current.size() - 1])
                          ? current + part
                          : current + PathSeparator() + part;
#if defined(_WIN32)
            if (!DirectoryExists(current) && ::_mkdir(current.c_str()) != 0 && !DirectoryExists(current)) {
#else
            if (!DirectoryExists(current) && ::mkdir(current.c_str(), 0755) != 0 && !DirectoryExists(current)) {
#endif
                return false;
            }
        }
        if (next == std::string::npos) {
            break;
        }
        index = next + 1;
    }
    return DirectoryExists(path);
}

std::string BaseNameWithoutExtension(const std::string& path) {
    const std::string::size_type slash_pos = path.find_last_of("/\\");
    const std::string leaf = slash_pos == std::string::npos ? path : path.substr(slash_pos + 1);
    const std::string::size_type dot_pos = leaf.find_last_of('.');
    if (dot_pos == std::string::npos || dot_pos == 0) {
        return leaf.empty() ? "output" : leaf;
    }
    return leaf.substr(0, dot_pos);
}

std::string ExtensionFromPath(const std::string& path) {
    const std::string::size_type slash_pos = path.find_last_of("/\\");
    const std::string leaf = slash_pos == std::string::npos ? path : path.substr(slash_pos + 1);
    const std::string::size_type dot_pos = leaf.find_last_of('.');
    if (dot_pos == std::string::npos || dot_pos + 1 >= leaf.size()) {
        return "";
    }
    std::string extension = leaf.substr(dot_pos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension == "jpeg") {
        return "jpg";
    }
    if (extension == "tif") {
        return "tiff";
    }
    return extension;
}

std::string UniqueOutputPath(const std::string& output_dir,
                             const std::string& input_path,
                             const std::string& format,
                             const std::vector<std::string>& used_paths) {
    const std::string base = BaseNameWithoutExtension(input_path);
    const std::string extension = format.empty() ? "docx" : format;
    std::string candidate = JoinPath(output_dir, base + "." + extension);
    int suffix = 2;
    while (FileExists(candidate) || std::find(used_paths.begin(), used_paths.end(), candidate) != used_paths.end()) {
        candidate = JoinPath(output_dir, base + "_" + std::to_string(suffix) + "." + extension);
        ++suffix;
    }
    return candidate;
}

editor::common::OcrFormat ParseOcrFormat(const std::string& format) {
    std::string value = format;
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    if (value == "txt") {
        return editor::common::OcrFormat::TXT;
    }
    if (value == "pdf") {
        return editor::common::OcrFormat::PDF;
    }
    if (value == "docx") {
        return editor::common::OcrFormat::DOCX;
    }
    if (value == "xlsx") {
        return editor::common::OcrFormat::XLSX;
    }
    if (value == "ofd") {
        return editor::common::OcrFormat::OFD;
    }
    if (value == "json") {
        return editor::common::OcrFormat::JSON;
    }
    return editor::common::OcrFormat::DOCX;
}

std::string OcrTaskStatusToString(editor::ocr::TaskStatus status) {
    switch (status) {
        case editor::ocr::TaskStatus::Pending:
            return "queued";
        case editor::ocr::TaskStatus::Processing:
            return "running";
        case editor::ocr::TaskStatus::Completed:
            return "completed";
        case editor::ocr::TaskStatus::Failed:
            return "failed";
    }
    return "unknown";
}

SdkOcrTextBlock BuildTextBlock(const CZ_OCR_Ret& ocr_result) {
    SdkOcrTextBlock block;
    block.text = std::string(ocr_result.texts);
    float min_x = static_cast<float>(ocr_result.rbox.points[0].x);
    float max_x = min_x;
    float min_y = static_cast<float>(ocr_result.rbox.points[0].y);
    float max_y = min_y;
    for (int i = 1; i < 4; ++i) {
        const float x = static_cast<float>(ocr_result.rbox.points[i].x);
        const float y = static_cast<float>(ocr_result.rbox.points[i].y);
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }
    block.x = min_x;
    block.y = min_y;
    block.width = max_x - min_x;
    block.height = max_y - min_y;
    if (block.width <= 0.0f || block.height <= 0.0f) {
        block.x = static_cast<float>(ocr_result.bbox.x0);
        block.y = static_cast<float>(ocr_result.bbox.y0);
        block.width = static_cast<float>(ocr_result.bbox.x1 - ocr_result.bbox.x0);
        block.height = static_cast<float>(ocr_result.bbox.y1 - ocr_result.bbox.y0);
    }
    block.confidence = ocr_result.score;
    block.font_size = ocr_result.fontSize > 0 ? static_cast<float>(ocr_result.fontSize) : block.height * 0.75f;
    return block;
}

class PrivateOcrProvider : public ISdkOcrProvider {
public:
    std::string ProviderName() const override { return "czur-ocr-provider"; }
    SdkOcrRecognizeResult Recognize(const SdkOcrRecognizeRequest& request) override {
        SdkOcrRecognizeResult result;
        if (!IsSupportedSdkOcrLanguage(request.language)) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "unsupported OCR language";
            return result;
        }
        nlohmann::json ext_params = nlohmann::json::object();
        if (!request.ext_params_json.empty()) {
            try {
                ext_params = nlohmann::json::parse(request.ext_params_json);
            } catch (const nlohmann::json::exception&) {
                result.code = ToCode(SdkStatusCode::InvalidParams);
                result.message = "OCR ext_params_json must be a JSON object";
                return result;
            }
            if (!ext_params.is_object()) {
                result.code = ToCode(SdkStatusCode::InvalidParams);
                result.message = "OCR ext_params_json must be a JSON object";
                return result;
            }
        }
        // The public field is authoritative, including when it defaults to auto.
        // Keep the language with the existing per-task export parameters.
        ext_params["language"] = SdkOcrEngineLanguage(request.language);
        const std::string ext_params_json = ext_params.dump();
        editor::ocr::OcrManager* manager = EnsureManager(&result.code, &result.message);
        if (manager == NULL) {
            return result;
        }
        const std::string sdk_task_id = "ocr-" + std::to_string(next_task_id_++);
        SdkOcrTaskSnapshot snapshot;
        snapshot.task_id = sdk_task_id;
        snapshot.status = "queued";
        snapshot.output_path = request.output_path;
        snapshot.export_type = request.export_type.empty() ? "multi-page" : request.export_type;
        snapshot.format = request.format;
        snapshot.message = "queued";
        if (snapshot.export_type == "multi-page" && !request.output_path.empty()) {
            snapshot.output_paths.push_back(request.output_path);
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            tasks_[sdk_task_id] = snapshot;
            // SDK 快照标记 cancelled 不代表底层线程退出；实际完成回调前始终视为仍可能写盘。
            task_lifecycle_.MarkSubmitted(sdk_task_id);
        }

        if (snapshot.export_type == "single-page") {
            const std::string output_dir = request.output_dir.empty() ? request.output_path : request.output_dir;
            if (!EnsureDirectoryRecursive(output_dir)) {
                result.code = ToCode(SdkStatusCode::ProviderCallFailed);
                result.message = "failed to create ocr output directory";
                std::lock_guard<std::mutex> lock(mu_);
                tasks_.erase(sdk_task_id);
                task_lifecycle_.MarkCompleted(sdk_task_id);
                return result;
            }
            std::vector<editor::ocr::OcrManager::OcrTaskInfo> task_list;
            std::vector<std::string> output_paths;
            for (std::vector<std::string>::const_iterator it = request.input_files.begin(); it != request.input_files.end(); ++it) {
                const std::string output_path = UniqueOutputPath(output_dir, *it, request.format, output_paths);
                output_paths.push_back(output_path);
                editor::ocr::OcrManager::OcrTaskInfo task_info;
                task_info.inputFiles.push_back(*it);
                task_info.outputPath = output_path;
                task_info.format = ParseOcrFormat(request.format);
                task_info.extParams = ext_params_json;
                task_info.engineType = editor::ocr::OcrEngineType::CzOcr;
                task_list.push_back(task_info);
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                SdkOcrTaskSnapshot& task = tasks_[sdk_task_id];
                task.output_path = output_dir;
                task.output_paths = output_paths;
            }
            const int batch_id = manager->submitBatchTaskWithDetailedProgressAndParams(
                task_list,
                [this, sdk_task_id](const editor::ocr::OcrManager::BatchOcrProgressInfo& progress_info) {
                    std::lock_guard<std::mutex> lock(mu_);
                    SdkOcrTaskSnapshot& task = tasks_[sdk_task_id];
                    if (task.status == "cancelled") {
                        return;
                    }
                    task.status = OcrTaskStatusToString(progress_info.batchStatus);
                    task.progress = progress_info.overallProgress;
                    task.message = progress_info.stage;
                },
                [this, sdk_task_id](int, const std::vector<editor::ocr::OcrTaskResult>& results) {
                    std::lock_guard<std::mutex> lock(mu_);
                    SdkOcrTaskSnapshot& task = tasks_[sdk_task_id];
                    task_lifecycle_.MarkCompleted(sdk_task_id);
                    if (task.status == "cancelled") {
                        return;
                    }
                    int failed_count = 0;
                    std::string first_error;
                    for (std::vector<editor::ocr::OcrTaskResult>::const_iterator it = results.begin(); it != results.end(); ++it) {
                        if (it->errorCode != editor::common::OcrErrOk) {
                            ++failed_count;
                            if (first_error.empty()) {
                                first_error = it->errorMessage.empty()
                                                  ? editor::common::getOcrErrorMessage(it->errorCode)
                                                  : it->errorMessage;
                            }
                        }
                    }
                    if (failed_count == 0) {
                        task.status = "completed";
                        task.progress = 100;
                        task.message = "completed";
                        task.error.clear();
                    } else {
                        task.status = failed_count == static_cast<int>(results.size()) ? "failed" : "completed";
                        task.progress = 100;
                        task.error = first_error;
                        task.message = first_error.empty() ? "partial completed" : first_error;
                    }
                });
            if (batch_id < 0) {
                result.code = ToCode(SdkStatusCode::ProviderCallFailed);
                result.message = "submit ocr batch task failed";
                std::lock_guard<std::mutex> lock(mu_);
                tasks_.erase(sdk_task_id);
                task_lifecycle_.MarkCompleted(sdk_task_id);
                return result;
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                internal_batch_by_sdk_task_[sdk_task_id] = batch_id;
                result.task = tasks_[sdk_task_id];
            }
            result.task_id = sdk_task_id;
            return result;
        }

        editor::ocr::OcrManager::OcrTaskInfo task_info;
        task_info.inputFiles = request.input_files;
        task_info.outputPath = request.output_path;
        task_info.format = ParseOcrFormat(request.format);
        task_info.extParams = ext_params_json;
        task_info.engineType = editor::ocr::OcrEngineType::CzOcr;
        const int batch_id = manager->submitBatchTaskWithDetailedProgressAndParams(
            std::vector<editor::ocr::OcrManager::OcrTaskInfo>(1, task_info),
            [this, sdk_task_id](const editor::ocr::OcrManager::BatchOcrProgressInfo& progress_info) {
                std::lock_guard<std::mutex> lock(mu_);
                SdkOcrTaskSnapshot& task = tasks_[sdk_task_id];
                if (task.status == "cancelled") {
                    return;
                }
                task.status = OcrTaskStatusToString(progress_info.batchStatus);
                task.progress = progress_info.overallProgress;
                task.message = progress_info.stage;
            },
            [this, sdk_task_id](int, const std::vector<editor::ocr::OcrTaskResult>& results) {
                std::lock_guard<std::mutex> lock(mu_);
                SdkOcrTaskSnapshot& task = tasks_[sdk_task_id];
                task_lifecycle_.MarkCompleted(sdk_task_id);
                if (task.status == "cancelled") {
                    return;
                }
                const editor::ocr::OcrTaskResult* task_result = results.empty() ? NULL : &results.front();
                if (task_result == NULL) {
                    task.status = "failed";
                    task.error = "ocr task returned no result";
                    task.message = task.error;
                    return;
                }
                if (task_result->errorCode == editor::common::OcrErrOk) {
                    task.status = "completed";
                    task.progress = 100;
                    task.message = "completed";
                    task.error.clear();
                } else {
                    task.status = "failed";
                    task.error = task_result->errorMessage.empty()
                                     ? editor::common::getOcrErrorMessage(task_result->errorCode)
                                     : task_result->errorMessage;
                    task.message = task.error;
                }
                if (!task_result->outputPath.empty()) {
                    task.output_path = task_result->outputPath;
                    task.output_paths.clear();
                    task.output_paths.push_back(task_result->outputPath);
                }
            });
        if (batch_id < 0) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "submit ocr task failed";
            std::lock_guard<std::mutex> lock(mu_);
            tasks_.erase(sdk_task_id);
            task_lifecycle_.MarkCompleted(sdk_task_id);
            return result;
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            internal_batch_by_sdk_task_[sdk_task_id] = batch_id;
            result.task = tasks_[sdk_task_id];
        }
        result.task_id = sdk_task_id;
        return result;
    }
    SdkOcrGetResult GetTask(const SdkOcrGetRequest& request) override {
        SdkOcrGetResult result;
        std::lock_guard<std::mutex> lock(mu_);
        const std::map<std::string, SdkOcrTaskSnapshot>::const_iterator it = tasks_.find(request.task_id);
        if (it == tasks_.end()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "ocr task not found";
            return result;
        }
        result.task = it->second;
        return result;
    }
    SdkOcrCancelResult Cancel(const SdkOcrCancelRequest& request) override {
        SdkOcrCancelResult result;
        editor::ocr::OcrManager* manager = EnsureManager(&result.code, &result.message);
        if (manager == NULL) {
            return result;
        }
        int internal_task_id = -1;
        int internal_batch_id = -1;
        {
            std::lock_guard<std::mutex> lock(mu_);
            std::map<std::string, SdkOcrTaskSnapshot>::iterator task_it = tasks_.find(request.task_id);
            const std::map<std::string, int>::const_iterator id_it = internal_task_by_sdk_task_.find(request.task_id);
            const std::map<std::string, int>::const_iterator batch_it = internal_batch_by_sdk_task_.find(request.task_id);
            if (task_it == tasks_.end() || (id_it == internal_task_by_sdk_task_.end() && batch_it == internal_batch_by_sdk_task_.end())) {
                result.code = ToCode(SdkStatusCode::InvalidParams);
                result.message = "ocr task not found";
                return result;
            }
            if (id_it != internal_task_by_sdk_task_.end()) {
                internal_task_id = id_it->second;
            }
            if (batch_it != internal_batch_by_sdk_task_.end()) {
                internal_batch_id = batch_it->second;
            }
        }
        result.cancelled = internal_batch_id >= 0 ? manager->cancelBatchTask(internal_batch_id) : manager->cancelTask(internal_task_id);
        {
            std::lock_guard<std::mutex> lock(mu_);
            SdkOcrTaskSnapshot& task = tasks_[request.task_id];
            if (result.cancelled) {
                task.status = "cancelled";
                task.message = "cancelled";
            }
            result.task = task;
        }
        return result;
    }
    SdkOcrExtractTextResult ExtractText(const SdkOcrExtractTextRequest& request) override {
        SdkOcrExtractTextResult result;
        if (!IsSupportedSdkOcrLanguage(request.language)) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "unsupported OCR language";
            return result;
        }
        editor::ocr::OcrManager* manager = EnsureManager(&result.code, &result.message);
        if (manager == NULL) {
            return result;
        }
        cv::Mat image = cv::imread(request.input_path, cv::IMREAD_COLOR);
        if (image.empty()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "invalid image";
            return result;
        }
        if (!image.isContinuous()) {
            image = image.clone();
        }
        std::vector<unsigned char> image_data(image.data, image.data + image.total() * image.elemSize());
        std::vector<std::pair<std::vector<CZ_OCR_Ret>, std::string> > groups;
        const editor::common::OcrError err = manager->processImageData(image_data,
                                                                       image.cols,
                                                                       image.rows,
                                                                       image.channels(),
                                                                       groups,
                                                                       editor::ocr::OcrEngineUseCase::InteractiveTextExtract,
                                                                       SdkOcrEngineLanguage(request.language));
        if (err != editor::common::OcrErrOk) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = editor::common::getOcrErrorMessage(err);
            return result;
        }
        result.recognized = true;
        result.input_path = request.input_path;
        result.width = image.cols;
        result.height = image.rows;
        for (std::vector<std::pair<std::vector<CZ_OCR_Ret>, std::string> >::const_iterator group_it = groups.begin();
             group_it != groups.end();
             ++group_it) {
            for (std::vector<CZ_OCR_Ret>::const_iterator ocr_it = group_it->first.begin();
                 ocr_it != group_it->first.end();
                 ++ocr_it) {
                result.blocks.push_back(BuildTextBlock(*ocr_it));
            }
        }
        return result;
    }

    std::size_t ActiveTaskCount() const {
        // 以底层完成回调为边界，避免取消请求刚受理但 OCR 线程尚未退出时误删工作目录。
        std::lock_guard<std::mutex> lock(mu_);
        return task_lifecycle_.ActiveTaskCount();
    }

    std::size_t ClearFinishedTasks() {
        // 仅清终态任务，同时移除 SDK task 与底层 task/batch 的映射，旧任务随后统一返回 not found。
        std::lock_guard<std::mutex> lock(mu_);
        std::size_t count = 0;
        for (std::map<std::string, SdkOcrTaskSnapshot>::iterator it = tasks_.begin(); it != tasks_.end();) {
            const bool terminal = it->second.status == "completed" || it->second.status == "failed" ||
                                  it->second.status == "cancelled";
            if (!terminal || task_lifecycle_.IsActive(it->first)) {
                ++it;
                continue;
            }
            internal_task_by_sdk_task_.erase(it->first);
            internal_batch_by_sdk_task_.erase(it->first);
            it = tasks_.erase(it);
            ++count;
        }
        return count;
    }

private:
    editor::ocr::OcrManager* EnsureManager(int* code, std::string* message) {
        try {
            const std::string log_dir = GetSdkRuntimeLogDir();
            EnsureDirectoryRecursive(log_dir);
            editor::ocr::OcrManager& manager =
                editor::ocr::OcrManager::getInstance(JoinPath(log_dir, "czur-sdk-ocr.log"), 3);
            return &manager;
        } catch (const std::exception& e) {
            if (code != NULL) {
                *code = ToCode(SdkStatusCode::ProviderNotReady);
            }
            if (message != NULL) {
                *message = e.what();
            }
            return NULL;
        }
    }

    std::atomic<int> next_task_id_{1};
    mutable std::mutex mu_;
    std::map<std::string, SdkOcrTaskSnapshot> tasks_;
    std::map<std::string, int> internal_task_by_sdk_task_;
    std::map<std::string, int> internal_batch_by_sdk_task_;
    // 仅由底层完成回调移除，表示 OCR 线程仍可能访问 SDK Open 临时目录。
    PrivateOcrTaskLifecycle task_lifecycle_;
};

class PrivateOfdProvider : public ISdkOfdProvider {
public:
    std::string ProviderName() const override { return "czur-file-convert-provider"; }
    SdkFileConvertResult Convert(const SdkFileConvertRequest& request) override {
        SdkFileConvertResult result;
        const std::vector<std::string> input_paths = CollectInputPaths(request);
        const std::string format = NormalizeFormat(request.output_format);
        const std::string source_format = SourceFormatFromRequest(request, input_paths);
        if (input_paths.empty()) {
            return Error(SdkStatusCode::InvalidParams, "file.convert input is empty");
        }
        if (!IsSupportedOutputFormat(format)) {
            return Error(SdkStatusCode::UnsupportedMethod, "unsupported file.convert output format: " + request.output_format);
        }
        for (std::vector<std::string>::const_iterator it = input_paths.begin(); it != input_paths.end(); ++it) {
            if (!FileExists(*it)) {
                return Error(SdkStatusCode::InvalidParams, "file.convert input file does not exist: " + *it);
            }
        }

        PageSet page_set;
        SdkFileConvertResult page_error = BuildPageSet(input_paths, source_format, request, &page_set);
        if (!IsOkStatusCode(page_error.code)) {
            return page_error;
        }
        if (page_set.pages.empty()) {
            CleanupPageSet(page_set);
            return Error(SdkStatusCode::InvalidParams, "file.convert selected pages are empty");
        }

        result.source_format = source_format;
        result.source_page_count = page_set.source_page_count;
        result.selected_page_count = static_cast<int>(page_set.pages.size());
        result.accepted = result.selected_page_count;
        const std::string export_type = request.export_type.empty() ? "multi-page" : request.export_type;
        if (export_type == "single-page") {
            const std::string output_dir = request.output_dir.empty()
                                               ? (request.output_path.empty() ? "." : ParentPath(request.output_path))
                                               : request.output_dir;
            if (!EnsureDirectoryRecursive(output_dir)) {
                CleanupPageSet(page_set);
                return Error(SdkStatusCode::ProviderCallFailed,
                             "failed to create file.convert output directory: output_dir=" + output_dir);
            }
            std::vector<std::string> used_paths;
            for (std::vector<PageImage>::const_iterator it = page_set.pages.begin(); it != page_set.pages.end(); ++it) {
                const std::string output_path = (page_set.pages.size() == 1 && !request.output_path.empty())
                                                    ? request.output_path
                                                    : UniqueOutputPathForPage(output_dir, *it, format, used_paths);
                if (!EnsureDirectoryRecursive(ParentPath(output_path))) {
                    CleanupPageSet(page_set);
                    return Error(SdkStatusCode::ProviderCallFailed,
                                 "failed to create file.convert output directory: output_path=" + output_path +
                                     ", parent=" + ParentPath(output_path));
                }
                used_paths.push_back(output_path);
                const SdkFileConvertResult one_result = ConvertToOneOutput({it->path}, output_path, format, request);
                if (!IsOkStatusCode(one_result.code)) {
                    CleanupPageSet(page_set);
                    return one_result;
                }
                result.output_paths.push_back(output_path);
            }
            result.output_path = result.output_paths.empty() ? "" : result.output_paths.front();
            result.converted = static_cast<int>(result.output_paths.size());
            CleanupPageSet(page_set);
            return result;
        }

        if (format == "jpg" || format == "png") {
            CleanupPageSet(page_set);
            return Error(SdkStatusCode::InvalidParams, "jpg and png targets only support single-page export");
        }

        if (request.output_path.empty()) {
            CleanupPageSet(page_set);
            return Error(SdkStatusCode::InvalidParams, "file.convert output_path is required for multi-page export");
        }
        if (!EnsureDirectoryRecursive(ParentPath(request.output_path))) {
            CleanupPageSet(page_set);
            return Error(SdkStatusCode::ProviderCallFailed,
                         "failed to create file.convert output directory: output_path=" + request.output_path +
                             ", parent=" + ParentPath(request.output_path));
        }
        std::vector<std::string> page_paths;
        for (std::vector<PageImage>::const_iterator it = page_set.pages.begin(); it != page_set.pages.end(); ++it) {
            page_paths.push_back(it->path);
        }
        SdkFileConvertResult convert_result = ConvertToOneOutput(page_paths, request.output_path, format, request);
        CleanupPageSet(page_set);
        if (IsOkStatusCode(convert_result.code)) {
            result.code = convert_result.code;
            result.message = convert_result.message;
            result.converted = 1;
            result.output_path = request.output_path;
            result.output_paths.clear();
            result.output_paths.push_back(request.output_path);
            return result;
        }
        return convert_result;
    }

private:
    struct PageImage {
        std::string path;
        std::string temp_path;
        std::string source_path;
        int source_index = 0;
        int source_page = 1;
    };

    struct PageSet {
        std::vector<PageImage> pages;
        std::string source_format;
        int source_page_count = 0;
    };

    struct PreparedImage {
        std::string path;
        std::string temp_path;
        ImgProc::ImageInfo info;
    };

    static SdkFileConvertResult Error(SdkStatusCode code, const std::string& message) {
        SdkFileConvertResult result;
        result.code = ToCode(code);
        result.message = message;
        return result;
    }

    static std::string NormalizeFormat(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (value == "jpeg") {
            return "jpg";
        }
        if (value == "tif") {
            return "tiff";
        }
        return value;
    }

    static std::vector<std::string> CollectInputPaths(const SdkFileConvertRequest& request) {
        std::vector<std::string> input_paths = request.input_paths;
        if (!request.input_path.empty() &&
            std::find(input_paths.begin(), input_paths.end(), request.input_path) == input_paths.end()) {
            input_paths.insert(input_paths.begin(), request.input_path);
        }
        return input_paths;
    }

    static bool IsSupportedOutputFormat(const std::string& format) {
        return format == "jpg" || format == "png" || format == "tiff" || format == "pdf" || format == "ofd";
    }

    static bool IsDocumentSourceFormat(const std::string& format) {
        return format == "pdf" || format == "ofd" || format == "tiff";
    }

    static std::string SourceFormatFromRequest(const SdkFileConvertRequest& request,
                                               const std::vector<std::string>& input_paths) {
        std::string value = NormalizeFormat(request.source_format.empty() ? request.source_type : request.source_format);
        if (value == "images" || value == "base64") {
            value = "image";
        }
        if (value.empty() || value == "image") {
            if (!input_paths.empty()) {
                const std::string extension = ExtensionFromPath(input_paths.front());
                if (extension == "pdf" || extension == "ofd") {
                    return extension;
                }
            }
            return "image";
        }
        return value;
    }

    static std::vector<int> ParsePageSelection(const std::string& pages,
                                               int page_count,
                                               std::string* message) {
        std::vector<int> selected;
        std::string value = pages;
        value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }), value.end());
        if (value.empty() || value == "all") {
            for (int i = 1; i <= page_count; ++i) {
                selected.push_back(i);
            }
            return selected;
        }

        std::size_t start = 0;
        while (start <= value.size()) {
            const std::size_t comma = value.find(',', start);
            const std::string token = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (token.empty()) {
                if (message != NULL) {
                    *message = "invalid pages expression";
                }
                return std::vector<int>();
            }
            const std::size_t dash = token.find('-');
            int first = 0;
            int last = 0;
            if (dash == std::string::npos) {
                first = std::atoi(token.c_str());
                last = first;
            } else {
                first = std::atoi(token.substr(0, dash).c_str());
                last = std::atoi(token.substr(dash + 1).c_str());
            }
            if (first < 1 || last < first || last > page_count) {
                if (message != NULL) {
                    *message = "pages out of range";
                }
                return std::vector<int>();
            }
            for (int page = first; page <= last; ++page) {
                selected.push_back(page);
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        return selected;
    }

    static std::string TempPageImagePath(const SdkFileConvertRequest& request,
                                         const std::string& source_path,
                                         int page_number) {
        const std::string dir = !request.output_dir.empty()
                                    ? request.output_dir
                                    : (!request.output_path.empty() ? ParentPath(request.output_path) : ".");
        std::ostringstream name;
        name << BaseNameWithoutExtension(source_path) << ".sdk-page-" << page_number << ".png";
        return JoinPath(dir, name.str());
    }

    static bool SaveRenderDataAsImage(const RenderData& render_data,
                                      const std::string& output_path,
                                      int quality,
                                      std::string* message) {
        if (render_data.width <= 0 || render_data.height <= 0 || render_data.imageData.empty()) {
            if (message != NULL) {
                *message = "rendered page image is empty";
            }
            return false;
        }
        int mat_type = CV_8UC3;
        if (render_data.channel == 4) {
            mat_type = CV_8UC4;
        } else if (render_data.channel == 1) {
            mat_type = CV_8UC1;
        }
        cv::Mat image(render_data.height,
                      render_data.width,
                      mat_type,
                      const_cast<uint8_t*>(render_data.imageData.data()),
                      static_cast<std::size_t>(render_data.stride));
        cv::Mat writable = image.clone();
        std::vector<int> params;
        const std::string extension = ExtensionFromPath(output_path);
        if (extension == "jpg") {
            if (writable.channels() == 4) {
                cv::cvtColor(writable, writable, cv::COLOR_BGRA2BGR);
            }
            params.push_back(cv::IMWRITE_JPEG_QUALITY);
            params.push_back(std::max(1, std::min(100, quality)));
        }
        if (!cv::imwrite(output_path, writable, params)) {
            if (message != NULL) {
                *message = "failed to write rendered page image: " + output_path;
            }
            return false;
        }
        return FileExists(output_path);
    }

    static SdkFileConvertResult BuildImagePageSet(const std::vector<std::string>& input_paths,
                                                  PageSet* page_set) {
        page_set->source_format = "image";
        page_set->source_page_count = static_cast<int>(input_paths.size());
        for (std::size_t i = 0; i < input_paths.size(); ++i) {
            PageImage page;
            page.path = input_paths[i];
            page.source_path = input_paths[i];
            page.source_index = static_cast<int>(i);
            page.source_page = static_cast<int>(i + 1);
            page_set->pages.push_back(page);
        }
        return SdkFileConvertResult();
    }

    static SdkFileConvertResult BuildPdfPageSet(const std::string& input_path,
                                                const SdkFileConvertRequest& request,
                                                PageSet* page_set) {
        try {
            editor::pdf::MuPdfProcessor processor;
            if (!processor.Open(input_path)) {
                return Error(SdkStatusCode::ProviderCallFailed, "failed to open pdf source");
            }
            const int page_count = processor.GetPageCount();
            page_set->source_format = "pdf";
            page_set->source_page_count = page_count;
            std::string message;
            const std::vector<int> pages = ParsePageSelection(request.pages, page_count, &message);
            if (pages.empty()) {
                return Error(SdkStatusCode::InvalidParams, message.empty() ? "selected pages are empty" : message);
            }
            const float scale = std::max(1, request.render_dpi) / 72.0f;
            for (std::vector<int>::const_iterator it = pages.begin(); it != pages.end(); ++it) {
                const PageData page_data = processor.GetPageData(*it - 1, scale);
                const std::string temp_path = TempPageImagePath(request, input_path, *it);
                if (!SaveRenderDataAsImage(page_data.renderedPage, temp_path, request.quality, &message)) {
                    CleanupPageSet(*page_set);
                    return Error(SdkStatusCode::ProviderCallFailed, message);
                }
                PageImage page;
                page.path = temp_path;
                page.temp_path = temp_path;
                page.source_path = input_path;
                page.source_page = *it;
                page_set->pages.push_back(page);
            }
            return SdkFileConvertResult();
        } catch (const std::exception& e) {
            CleanupPageSet(*page_set);
            return Error(SdkStatusCode::ProviderCallFailed, std::string("pdf page extraction failed: ") + e.what());
        }
    }

    static SdkFileConvertResult BuildOfdPageSet(const std::string& input_path,
                                                const SdkFileConvertRequest& request,
                                                PageSet* page_set) {
        try {
            editor::ofd::OfdProcessor processor;
            if (!processor.Open(input_path)) {
                return Error(SdkStatusCode::ProviderCallFailed, "failed to open ofd source");
            }
            const int page_count = processor.GetPageCount();
            page_set->source_format = "ofd";
            page_set->source_page_count = page_count;
            std::string message;
            const std::vector<int> pages = ParsePageSelection(request.pages, page_count, &message);
            if (pages.empty()) {
                return Error(SdkStatusCode::InvalidParams, message.empty() ? "selected pages are empty" : message);
            }
            processor.setRenderDPI(static_cast<float>(std::max(1, request.render_dpi)));
            for (std::vector<int>::const_iterator it = pages.begin(); it != pages.end(); ++it) {
                const PageData page_data = processor.GetPageData(*it - 1, 1.0f);
                const std::string temp_path = TempPageImagePath(request, input_path, *it);
                if (!SaveRenderDataAsImage(page_data.renderedPage, temp_path, request.quality, &message)) {
                    CleanupPageSet(*page_set);
                    return Error(SdkStatusCode::ProviderCallFailed, message);
                }
                PageImage page;
                page.path = temp_path;
                page.temp_path = temp_path;
                page.source_path = input_path;
                page.source_page = *it;
                page_set->pages.push_back(page);
            }
            return SdkFileConvertResult();
        } catch (const std::exception& e) {
            CleanupPageSet(*page_set);
            return Error(SdkStatusCode::ProviderCallFailed, std::string("ofd page extraction failed: ") + e.what());
        }
    }

    static SdkFileConvertResult BuildTiffPageSet(const std::string& input_path,
                                                 const SdkFileConvertRequest& request,
                                                 PageSet* page_set) {
        std::vector<cv::Mat> pages_mat;
        if (!cv::imreadmulti(input_path, pages_mat, cv::IMREAD_COLOR) || pages_mat.empty()) {
            return Error(SdkStatusCode::ProviderCallFailed, "failed to open tiff source");
        }
        const int page_count = static_cast<int>(pages_mat.size());
        page_set->source_format = "tiff";
        page_set->source_page_count = page_count;
        std::string message;
        const std::vector<int> pages = ParsePageSelection(request.pages, page_count, &message);
        if (pages.empty()) {
            return Error(SdkStatusCode::InvalidParams, message.empty() ? "selected pages are empty" : message);
        }
        for (std::vector<int>::const_iterator it = pages.begin(); it != pages.end(); ++it) {
            const std::string temp_path = TempPageImagePath(request, input_path, *it);
            std::vector<int> params;
            params.push_back(cv::IMWRITE_PNG_COMPRESSION);
            params.push_back(3);
            if (!cv::imwrite(temp_path, pages_mat[*it - 1], params) || !FileExists(temp_path)) {
                CleanupPageSet(*page_set);
                return Error(SdkStatusCode::ProviderCallFailed, "failed to extract tiff page");
            }
            PageImage page;
            page.path = temp_path;
            page.temp_path = temp_path;
            page.source_path = input_path;
            page.source_page = *it;
            page_set->pages.push_back(page);
        }
        return SdkFileConvertResult();
    }

    static SdkFileConvertResult BuildPageSet(const std::vector<std::string>& input_paths,
                                             const std::string& source_format,
                                             const SdkFileConvertRequest& request,
                                             PageSet* page_set) {
        page_set->pages.clear();
        page_set->source_format = source_format;
        page_set->source_page_count = 0;
        if (!IsDocumentSourceFormat(source_format) || source_format == "image") {
            return BuildImagePageSet(input_paths, page_set);
        }
        if (input_paths.size() != 1) {
            return Error(SdkStatusCode::InvalidParams, "document source accepts one input file");
        }
        if (!EnsureDirectoryRecursive(!request.output_dir.empty()
                                          ? request.output_dir
                                          : (!request.output_path.empty() ? ParentPath(request.output_path) : "."))) {
            return Error(SdkStatusCode::ProviderCallFailed, "failed to create file.convert temp directory");
        }
        if (source_format == "pdf") {
            return BuildPdfPageSet(input_paths.front(), request, page_set);
        }
        if (source_format == "ofd") {
            return BuildOfdPageSet(input_paths.front(), request, page_set);
        }
        if (source_format == "tiff") {
            return BuildTiffPageSet(input_paths.front(), request, page_set);
        }
        return Error(SdkStatusCode::UnsupportedMethod, "unsupported file.convert source format: " + source_format);
    }

    static void CleanupPageSet(const PageSet& page_set) {
        for (std::vector<PageImage>::const_iterator it = page_set.pages.begin(); it != page_set.pages.end(); ++it) {
            if (!it->temp_path.empty()) {
                std::remove(it->temp_path.c_str());
            }
        }
    }

    static std::string UniqueOutputPathForPage(const std::string& output_dir,
                                               const PageImage& page,
                                               const std::string& format,
                                               const std::vector<std::string>& used_paths) {
        std::ostringstream base;
        base << BaseNameWithoutExtension(page.source_path);
        if (!page.temp_path.empty()) {
            base << "_p" << page.source_page;
        }
        const std::string extension = format.empty() ? "bin" : format;
        std::string candidate = JoinPath(output_dir, base.str() + "." + extension);
        int suffix = 2;
        while (FileExists(candidate) || std::find(used_paths.begin(), used_paths.end(), candidate) != used_paths.end()) {
            candidate = JoinPath(output_dir, base.str() + "_" + std::to_string(suffix) + "." + extension);
            ++suffix;
        }
        return candidate;
    }

    static ImgProc::ImageEngine::ImageQuality ParseQuality(int quality) {
        if (quality >= 70) {
            return ImgProc::ImageEngine::HIGH_QUALITY;
        }
        if (quality >= 40) {
            return ImgProc::ImageEngine::MEDIUM_QUALITY;
        }
        return ImgProc::ImageEngine::LOW_QUALITY;
    }

    static ImgProc::ImageEngine::TiffColorFormat ParseTiffColor(const std::string& value) {
        const std::string normalized = NormalizeFormat(value);
        if (normalized == "gray" || normalized == "grey" || normalized == "grayscale") {
            return ImgProc::ImageEngine::TIFF_GRAYSCALE;
        }
        return ImgProc::ImageEngine::TIFF_COLOR;
    }

    static ImgProc::ImageEngine::TiffCompressionType ParseTiffCompression(const std::string& value) {
        const std::string normalized = NormalizeFormat(value);
        if (normalized == "none") {
            return ImgProc::ImageEngine::TIFF_COMP_NONE;
        }
        if (normalized == "jpeg" || normalized == "jpg") {
            return ImgProc::ImageEngine::TIFF_COMP_JPEG;
        }
        if (normalized == "group4" || normalized == "g4") {
            return ImgProc::ImageEngine::TIFF_COMP_GROUP4;
        }
        return ImgProc::ImageEngine::TIFF_COMP_LZW;
    }

    static std::string TempJpgPath(const std::string& output_path, std::size_t index) {
        std::ostringstream path;
        path << output_path << ".sdk-tmp-" << index << ".jpg";
        return path.str();
    }

    static bool BuildPreparedImages(const std::vector<std::string>& input_paths,
                                    const std::string& output_path,
                                    int quality,
                                    std::vector<PreparedImage>* prepared,
                                    std::string* message) {
        if (prepared == NULL) {
            return false;
        }
        ImgProc::ImageEngine engine;
        const ImgProc::ImageEngine::ImageQuality image_quality = ParseQuality(quality);
        prepared->clear();
        prepared->reserve(input_paths.size());
        for (std::size_t i = 0; i < input_paths.size(); ++i) {
            PreparedImage image;
            image.info = engine.getImageInfo(input_paths[i]);
            if (image.info.error_code != ImgProc::SUCCESS || image.info.width == 0 || image.info.height == 0) {
                if (message != NULL) {
                    *message = "failed to read image info: " + input_paths[i];
                }
                CleanupPreparedImages(*prepared);
                return false;
            }
            image.temp_path = TempJpgPath(output_path, i);
            const int dpi = image.info.dpi > 0 ? image.info.dpi : 72;
            const ImgProc::ImgProcErrorCode code = engine.convertToJpg(input_paths[i], image.temp_path, image_quality, dpi);
            if (code != ImgProc::SUCCESS || !FileExists(image.temp_path)) {
                if (message != NULL) {
                    *message = "failed to prepare image for document conversion: " + input_paths[i];
                }
                CleanupPreparedImages(*prepared);
                return false;
            }
            image.path = image.temp_path;
            prepared->push_back(image);
        }
        return true;
    }

    static void CleanupPreparedImages(const std::vector<PreparedImage>& prepared) {
        for (std::vector<PreparedImage>::const_iterator it = prepared.begin(); it != prepared.end(); ++it) {
            if (!it->temp_path.empty()) {
                std::remove(it->temp_path.c_str());
            }
        }
    }

    static void PageSizeFromImage(const ImgProc::ImageInfo& info, float* width, float* height) {
        const int dpi = info.dpi > 0 ? info.dpi : 72;
        *width = static_cast<float>(info.width) * 72.0f / static_cast<float>(dpi);
        *height = static_cast<float>(info.height) * 72.0f / static_cast<float>(dpi);
    }

    static SdkFileConvertResult ConvertToOneOutput(const std::vector<std::string>& input_paths,
                                                   const std::string& output_path,
                                                   const std::string& format,
                                                   const SdkFileConvertRequest& request) {
        if (format == "jpg" || format == "png") {
            if (input_paths.size() != 1) {
                return Error(SdkStatusCode::InvalidParams, "jpg and png targets require one source page per output");
            }
            return ConvertToImageFile(input_paths.front(), output_path, format, request);
        }
        if (format == "tiff") {
            return ConvertToTiff(input_paths, output_path, request);
        }
        if (format == "pdf") {
            return ConvertToPdf(input_paths, output_path, request);
        }
        if (format == "ofd") {
            return ConvertToOfd(input_paths, output_path, request);
        }
        return Error(SdkStatusCode::UnsupportedMethod, "unsupported file.convert output format: " + format);
    }

    static SdkFileConvertResult ConvertToImageFile(const std::string& input_path,
                                                   const std::string& output_path,
                                                   const std::string& format,
                                                   const SdkFileConvertRequest& request) {
        ImgProc::ImageEngine engine;
        ImgProc::ImgProcErrorCode code = ImgProc::UNKNOWN_ERROR;
        if (format == "png") {
            code = engine.convertToPng(input_path, output_path, ParseQuality(request.quality), 72, 24);
        } else {
            code = engine.convertToJpg(input_path, output_path, ParseQuality(request.quality), 72);
        }
        if (code != ImgProc::SUCCESS || !FileExists(output_path)) {
            return Error(SdkStatusCode::ProviderCallFailed, "image conversion failed");
        }
        SdkFileConvertResult result;
        result.accepted = 1;
        result.converted = 1;
        result.output_path = output_path;
        result.output_paths.push_back(output_path);
        return result;
    }

    static SdkFileConvertResult ConvertToTiff(const std::vector<std::string>& input_paths,
                                              const std::string& output_path,
                                              const SdkFileConvertRequest& request) {
        ImgProc::ImageEngine engine;
        const ImgProc::ImgProcErrorCode code = engine.convertToTiff(input_paths,
                                                                     output_path,
                                                                     ParseTiffColor(request.tiff_color),
                                                                     ParseTiffCompression(request.tiff_compression),
                                                                     72);
        if (code != ImgProc::SUCCESS || !FileExists(output_path)) {
            return Error(SdkStatusCode::ProviderCallFailed,
                         "tiff conversion failed: code=" + std::to_string(static_cast<int>(code)) +
                             ", output_path=" + output_path);
        }
        SdkFileConvertResult result;
        result.accepted = static_cast<int>(input_paths.size());
        result.converted = 1;
        result.output_path = output_path;
        result.output_paths.push_back(output_path);
        return result;
    }

    static SdkFileConvertResult ConvertToPdf(const std::vector<std::string>& input_paths,
                                             const std::string& output_path,
                                             const SdkFileConvertRequest& request) {
        std::vector<PreparedImage> prepared;
        std::string message;
        if (!BuildPreparedImages(input_paths, output_path, request.quality, &prepared, &message)) {
            return Error(SdkStatusCode::ProviderCallFailed, message);
        }

        try {
            editor::pdf::MuPdfProcessor processor;
            if (!processor.Open("")) {
                CleanupPreparedImages(prepared);
                return Error(SdkStatusCode::ProviderCallFailed, "failed to create pdf document");
            }
            for (std::size_t i = 0; i < prepared.size(); ++i) {
                float width = 0.0f;
                float height = 0.0f;
                PageSizeFromImage(prepared[i].info, &width, &height);
                nlohmann::json page_params;
                page_params["width"] = width;
                page_params["height"] = height;
                processor.InsertPage(-1, page_params.dump());
                const int page_index = processor.GetPageCount() - 1;

                nlohmann::json image_params;
                image_params["rect"] = {{"x0", 0}, {"y0", 0}, {"x1", width}, {"y1", height}};
                image_params["keep_proportion"] = true;
                image_params["overlay"] = true;
                const editor::OperationResult add_result = processor.AddImage(prepared[i].path, image_params.dump(), page_index);
                if (!add_result.success) {
                    CleanupPreparedImages(prepared);
                    return Error(SdkStatusCode::ProviderCallFailed, "failed to add image to pdf");
                }
            }
            const bool saved = processor.Save(output_path);
            CleanupPreparedImages(prepared);
            if (!saved || !FileExists(output_path)) {
                return Error(SdkStatusCode::ProviderCallFailed, "failed to save pdf document");
            }
        } catch (const std::exception& e) {
            CleanupPreparedImages(prepared);
            return Error(SdkStatusCode::ProviderCallFailed, std::string("pdf conversion failed: ") + e.what());
        } catch (...) {
            CleanupPreparedImages(prepared);
            return Error(SdkStatusCode::ProviderCallFailed, "pdf conversion failed");
        }

        SdkFileConvertResult result;
        result.accepted = static_cast<int>(input_paths.size());
        result.converted = 1;
        result.output_path = output_path;
        result.output_paths.push_back(output_path);
        return result;
    }

    static SdkFileConvertResult ConvertToOfd(const std::vector<std::string>& input_paths,
                                             const std::string& output_path,
                                             const SdkFileConvertRequest& request) {
        std::vector<PreparedImage> prepared;
        std::string message;
        if (!BuildPreparedImages(input_paths, output_path, request.quality, &prepared, &message)) {
            return Error(SdkStatusCode::ProviderCallFailed, message);
        }

        try {
            editor::ofd::OfdProcessor processor;
            if (!processor.Open(output_path)) {
                CleanupPreparedImages(prepared);
                return Error(SdkStatusCode::ProviderCallFailed, "failed to create ofd document");
            }
            for (std::size_t i = 0; i < prepared.size(); ++i) {
                float width = 0.0f;
                float height = 0.0f;
                PageSizeFromImage(prepared[i].info, &width, &height);
                nlohmann::json page_params;
                page_params["width"] = width;
                page_params["height"] = height;
                processor.InsertPage(-1, page_params.dump());
                const int page_index = processor.GetPageCount() - 1;

                nlohmann::json image_params;
                image_params["x"] = 0;
                image_params["y"] = 0;
                image_params["width"] = width;
                image_params["height"] = height;
                image_params["keepAspectRatio"] = false;
                const editor::OperationResult add_result = processor.AddImage(prepared[i].path, image_params.dump(), page_index);
                if (!add_result.success) {
                    CleanupPreparedImages(prepared);
                    return Error(SdkStatusCode::ProviderCallFailed, "failed to add image to ofd");
                }
            }
            const bool saved = processor.Save(output_path);
            CleanupPreparedImages(prepared);
            if (!saved || !FileExists(output_path)) {
                return Error(SdkStatusCode::ProviderCallFailed, "failed to save ofd document");
            }
        } catch (const std::exception& e) {
            CleanupPreparedImages(prepared);
            return Error(SdkStatusCode::ProviderCallFailed, std::string("ofd conversion failed: ") + e.what());
        } catch (...) {
            CleanupPreparedImages(prepared);
            return Error(SdkStatusCode::ProviderCallFailed, "ofd conversion failed");
        }

        SdkFileConvertResult result;
        result.accepted = static_cast<int>(input_paths.size());
        result.converted = 1;
        result.output_path = output_path;
        result.output_paths.push_back(output_path);
        return result;
    }
};

#if defined(__linux__) || defined(__APPLE__)
class PosixPrivateStorageProvider : public ISdkStorageProvider {
public:
    PosixPrivateStorageProvider(const std::shared_ptr<PrivateDeviceProvider>& device_provider,
                                const std::shared_ptr<PrivateOcrProvider>& ocr_provider,
                                const std::shared_ptr<PrivateSaneProvider>& sane_provider)
        : device_provider_(device_provider), ocr_provider_(ocr_provider), sane_provider_(sane_provider) {}

    std::string ProviderName() const override {
#if defined(__APPLE__)
        return "private-macos-storage-provider";
#else
        return "private-linux-storage-provider";
#endif
    }

    SdkStorageCleanupResult CleanupTemp(const SdkStorageCleanupRequest& request) override {
        SdkStorageCleanupResult result;
        std::unique_lock<std::mutex> capture_storage_lock;
        if (device_provider_) {
            capture_storage_lock = device_provider_->TryLockCaptureStorageLifecycle();
            if (!capture_storage_lock.owns_lock()) {
                result.code = ToCode(SdkStatusCode::StorageBusy);
                result.message = "temporary storage is busy";
                result.active.capture = 1;
                return result;
            }
            result.active.capture = device_provider_->ActiveCaptureCount();
        }
        if (ocr_provider_) {
            result.active.ocr = ocr_provider_->ActiveTaskCount();
        }
        // SANE 只在 Linux 上可用；macOS 的 private SANE provider 是不可用占位实现。
#if defined(__linux__)
        if (sane_provider_) {
            result.active.sane = sane_provider_->ActiveTaskCount();
        }
#endif
        if (result.active.Total() != 0) {
            result.code = ToCode(SdkStatusCode::StorageBusy);
            result.message = "temporary storage is busy";
            return result;
        }

        result = RemoveSdkOpenTempDirectories(request.work_dir);
        if (!IsOkStatusCode(result.code)) {
            return result;
        }
        if (ocr_provider_) {
            result.cleared_ocr_task_count = ocr_provider_->ClearFinishedTasks();
            result.cleared_task_count += result.cleared_ocr_task_count;
        }
#if defined(__linux__)
        if (sane_provider_) {
            // Open 层维护同一批 SANE 任务镜像，对外计数由 Open 层去重；这里仅清 private 快照。
            sane_provider_->ClearFinishedTasks();
        }
#endif
        return result;
    }

private:
    std::shared_ptr<PrivateDeviceProvider> device_provider_;
    std::shared_ptr<PrivateOcrProvider> ocr_provider_;
    std::shared_ptr<PrivateSaneProvider> sane_provider_;
};
#endif

} // namespace

void ShutdownOcrRuntime() {
    editor::ocr::OcrManager::getInstance().shutdown();
}

ProviderBundle CreateProviderBundle(bool enable_sdk_open_storage_provider) {
    SDK_PRIVATE_LOG_INFO("create provider bundle begin");
    ProviderBundle bundle;
#if defined(SDK_OPEN_HARMONY_REAL_PROVIDERS)
    // Harmony has no desktop scanner backends.  Keep the bundle limited to
    // providers whose OHOS libraries are packaged by sdk_open_harmony.
    // USB/UVC devices are backed by the native OHOS DeviceManager; SANE/TWAIN
    // remain unavailable and are explicitly cleared by the host.
    bundle.auth_provider = std::make_shared<PrivateAuthProvider>();
    bundle.device_provider = std::make_shared<PrivateDeviceProvider>();
    bundle.image_enhance_provider = std::make_shared<PrivateImageEnhanceProvider>();
    bundle.ocr_provider = std::make_shared<PrivateOcrProvider>();
    bundle.ofd_provider = std::make_shared<PrivateOfdProvider>();
    bundle.recognition_provider = std::make_shared<PrivateRecognitionProvider>();
    (void)enable_sdk_open_storage_provider;
#else
    bundle.auth_provider = std::make_shared<PrivateAuthProvider>();
    const std::shared_ptr<PrivateDeviceProvider> device_provider =
        std::make_shared<PrivateDeviceProvider>();
    bundle.device_provider = device_provider;
#if defined(__APPLE__)
    // SDK Open 在 macOS 上启动后立即比较共享 helper 版本。这里只初始化设备
    // 管理器和 hotplug 监听，不会打开或占用任何摄像头。
    if (!device_provider->InitializeForRuntimeStartup()) {
        SDK_PRIVATE_LOG_WARN("device.manager startup preflight failed; device APIs remain unavailable until helper/config is repaired");
    }
#endif
    bundle.graphic_provider = std::make_shared<PrivateGraphicProvider>();
    bundle.image_enhance_provider = std::make_shared<PrivateImageEnhanceProvider>();
    bundle.ocr_provider = std::make_shared<PrivateOcrProvider>();
    bundle.ofd_provider = std::make_shared<PrivateOfdProvider>();
    bundle.recognition_provider = std::make_shared<PrivateRecognitionProvider>();
    bundle.sane_provider = std::make_shared<PrivateSaneProvider>();
#if defined(__linux__) || defined(__APPLE__)
    if (enable_sdk_open_storage_provider) {
        // POSIX SDK Open 复用同一份 capture/OCR/SANE provider，在实际删除前复核真实执行状态。
        const std::shared_ptr<PrivateDeviceProvider> device_provider = std::static_pointer_cast<PrivateDeviceProvider>(bundle.device_provider);
        const std::shared_ptr<PrivateOcrProvider> ocr_provider = std::static_pointer_cast<PrivateOcrProvider>(bundle.ocr_provider);
        const std::shared_ptr<PrivateSaneProvider> sane_provider = std::static_pointer_cast<PrivateSaneProvider>(bundle.sane_provider);
        bundle.storage_provider = std::make_shared<PosixPrivateStorageProvider>(device_provider, ocr_provider, sane_provider);
    }
#else
    (void)enable_sdk_open_storage_provider;
#endif
#endif
    SDK_PRIVATE_LOG_INFO("create provider bundle success auth={} device={} graphic={} image_enhance={} ocr={} ofd={} recognition={} sane={} storage={}",
                         static_cast<bool>(bundle.auth_provider),
                         static_cast<bool>(bundle.device_provider),
                         static_cast<bool>(bundle.graphic_provider),
                         static_cast<bool>(bundle.image_enhance_provider),
                         static_cast<bool>(bundle.ocr_provider),
                         static_cast<bool>(bundle.ofd_provider),
                         static_cast<bool>(bundle.recognition_provider),
                         static_cast<bool>(bundle.sane_provider),
                         static_cast<bool>(bundle.storage_provider));
    return bundle;
}

#if !defined(SDK_OPEN_HARMONY_REAL_PROVIDERS)
extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API void czur_sdk_private_device_action_event_set_callback(
    CzurSdkPrivateDeviceActionEventCallback callback,
    void* user_data) {
    if (callback == NULL) {
        SharedPrivateDeviceProvider().SetDeviceActionEventSink(SdkDeviceActionEventCallback());
        SDK_PRIVATE_LOG_INFO("device.action_event callback cleared");
        return;
    }

    SharedPrivateDeviceProvider().SetDeviceActionEventSink(
        [callback, user_data](const SdkDeviceActionEvent& event) {
            try {
                const std::string event_json = DeviceActionEventToJson(event).dump();
                callback(event_json.c_str(), user_data);
            } catch (const std::exception& e) {
                SDK_PRIVATE_LOG_WARN("device.action_event callback failed: {}", e.what());
            } catch (...) {
                SDK_PRIVATE_LOG_WARN("device.action_event callback failed: unknown error");
            }
        });
    SDK_PRIVATE_LOG_INFO("device.action_event callback registered");
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API void czur_sdk_private_device_event_set_callback(
    CzurSdkPrivateDeviceEventCallback callback,
    void* user_data) {
    if (callback == NULL) {
        SharedPrivateDeviceProvider().SetDeviceEventSink(SdkDeviceEventCallback());
        SDK_PRIVATE_LOG_INFO("device.lifecycle callback cleared");
        return;
    }

    SharedPrivateDeviceProvider().SetDeviceEventSink(
        [callback, user_data](const SdkDeviceEvent& event) {
            try {
                const std::string event_json = DeviceEventToJson(event).dump();
                callback(event_json.c_str(), user_data);
            } catch (const std::exception& e) {
                SDK_PRIVATE_LOG_WARN("device.lifecycle callback failed: {}", e.what());
            } catch (...) {
                SDK_PRIVATE_LOG_WARN("device.lifecycle callback failed: unknown error");
            }
        });
    SDK_PRIVATE_LOG_INFO("device.lifecycle callback registered");
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API void czur_sdk_private_twain_source_event_set_callback(
    CzurSdkPrivateTwainSourceEventCallback callback,
    void* user_data) {
    std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
    if (!provider) {
        return;
    }
    if (callback == NULL) {
        provider->SetSourceEventSink(mt::TwainSourceEventCallback());
        SDK_PRIVATE_LOG_INFO("twain.source_event callback cleared");
        return;
    }

    // TWAIN 事件由 module/twain 内部线程产生；private DLL 只负责转成稳定 JSON 回调给 sdk_open。
    provider->SetSourceEventSink(
        [callback, user_data](const mt::TwainSourceEvent& event) {
            try {
                const std::string event_json = TwainSourceEventToJson(event).dump();
                callback(event_json.c_str(), user_data);
            } catch (const std::exception& e) {
                SDK_PRIVATE_LOG_WARN("twain.source_event callback failed: {}", e.what());
            } catch (...) {
                SDK_PRIVATE_LOG_WARN("twain.source_event callback failed: unknown error");
            }
        });
    SDK_PRIVATE_LOG_INFO("twain.source_event callback registered");
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API void czur_sdk_private_twain_scan_event_set_callback(
    CzurSdkPrivateTwainScanEventCallback callback,
    void* user_data) {
    std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
    if (!provider) {
        return;
    }
    if (callback == NULL) {
        provider->SetScanTaskEventSink(mt::TwainScanTaskEventCallback());
        SDK_PRIVATE_LOG_INFO("twain.scan_event callback cleared");
        return;
    }

    // 扫描进度和完成事件下沉在 module/twain；sdk_open 只消费这个 JSON 快照并转发 WebSocket。
    provider->SetScanTaskEventSink(
        [callback, user_data](const mt::TwainScanTaskEvent& event) {
            try {
                const std::string event_json = TwainScanTaskEventToJson(event).dump();
                callback(event_json.c_str(), user_data);
            } catch (const std::exception& e) {
                SDK_PRIVATE_LOG_WARN("twain.scan_event callback failed: {}", e.what());
            } catch (...) {
                SDK_PRIVATE_LOG_WARN("twain.scan_event callback failed: unknown error");
            }
        });
    SDK_PRIVATE_LOG_INFO("twain.scan_event callback registered");
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_status_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        const mt::TwainStatusResult result = provider->GetStatus();
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["available"] = result.available;
        response["platform"] = result.platform;
        response["supported_platforms"] = result.supported_platforms;
        response["dsm_loaded"] = result.dsm_loaded;
        response["dsm_path"] = result.dsm_path;
        response["twain_version"] = result.twain_version;
        response["reason"] = result.reason;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_list_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        mt::TwainListRequest list_request;
        list_request.refresh = JsonBoolField(request, "refresh", list_request.refresh);
        const mt::TwainListResult result = provider->ListSources(list_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["generation"] = result.generation;
        response["sources"] = TwainSourcesToJson(result.sources);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_watch_start_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        mt::TwainWatchRequest watch_request;
        watch_request.connection_id = JsonStringField(request, "connection_id");
        watch_request.enabled = JsonBoolField(request, "enabled", watch_request.enabled);
        const mt::TwainWatchResult result = provider->WatchStart(watch_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["watching"] = result.watching;
        response["generation"] = result.generation;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_watch_stop_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        mt::TwainWatchRequest watch_request;
        watch_request.connection_id = JsonStringField(request, "connection_id");
        watch_request.enabled = JsonBoolField(request, "enabled", watch_request.enabled);
        const mt::TwainWatchResult result = provider->WatchStop(watch_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["watching"] = result.watching;
        response["generation"] = result.generation;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_open_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        mt::TwainOpenRequest open_request;
        open_request.source_id = JsonStringField(request, "source_id");
        const mt::TwainOpenResult result = provider->OpenSource(open_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["opened"] = result.opened;
        response["session_id"] = result.session_id;
        response["source"] = TwainSourceToJson(result.source);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_close_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        mt::TwainCloseRequest close_request;
        close_request.session_id = JsonStringField(request, "session_id");
        const mt::TwainCloseResult result = provider->CloseSource(close_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["closed"] = result.closed;
        response["was_opened"] = result.was_opened;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_get_capabilities_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        mt::TwainGetCapabilitiesRequest cap_request;
        cap_request.session_id = JsonStringField(request, "session_id");
        const mt::TwainGetCapabilitiesResult result = provider->GetCapabilities(cap_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["capabilities"] = nlohmann::json::array();
        for (std::vector<mt::TwainCapability>::const_iterator it = result.capabilities.begin();
             it != result.capabilities.end();
             ++it) {
            response["capabilities"].push_back(TwainCapabilityToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_set_capabilities_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        mt::TwainSetCapabilitiesRequest set_request;
        set_request.session_id = JsonStringField(request, "session_id");
        set_request.capabilities = TwainCapabilitySetItemsFromJson(request.value("capabilities", nlohmann::json::array()));
        const mt::TwainSetCapabilitiesResult result = provider->SetCapabilities(set_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["applied"] = result.applied;
        response["requires_reload"] = result.requires_reload;
        response["results"] = nlohmann::json::array();
        for (std::vector<mt::TwainCapabilitySetResultItem>::const_iterator it = result.results.begin();
             it != result.results.end();
             ++it) {
            response["results"].push_back(TwainCapabilitySetResultItemToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_profile_list_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        const mt::TwainProfileListResult result = provider->ListProfiles(TwainProfileRequestFromJson(request));
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["profiles"] = nlohmann::json::array();
        for (std::vector<mt::TwainProfile>::const_iterator it = result.profiles.begin(); it != result.profiles.end(); ++it) {
            response["profiles"].push_back(TwainProfileToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

static const char* TwainProfileMutationJson(const char* request_json, const std::string& method) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        const mt::TwainProfileRequest profile_request = TwainProfileRequestFromJson(request);
        mt::TwainProfileResult result;
        if (method == "save") {
            result = provider->SaveProfile(profile_request);
        } else if (method == "apply") {
            result = provider->ApplyProfile(profile_request);
        } else {
            result = provider->DeleteProfile(profile_request);
        }
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["applied"] = result.applied;
        response["saved"] = result.saved;
        response["deleted"] = result.deleted;
        response["profile"] = TwainProfileToJson(result.profile);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_profile_save_json(const char* request_json) {
    return TwainProfileMutationJson(request_json, "save");
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_profile_apply_json(const char* request_json) {
    return TwainProfileMutationJson(request_json, "apply");
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_profile_delete_json(const char* request_json) {
    return TwainProfileMutationJson(request_json, "delete");
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_scan_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        const mt::TwainScanResult result = provider->Scan(TwainScanRequestFromJson(request));
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["accepted"] = result.accepted;
        response["task_id"] = result.task_id;
        response["task"] = TwainScanTaskToJson(result.task);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_scan_get_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        mt::TwainScanGetRequest get_request;
        get_request.task_id = JsonStringField(request, "task_id");
        const mt::TwainScanResult result = provider->GetScan(get_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["accepted"] = result.accepted;
        response["task_id"] = result.task_id;
        response["task"] = TwainScanTaskToJson(result.task);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_twain_scan_cancel_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::shared_ptr<mt::ITwainProvider> provider = GetPrivateTwainProviderForCall();
        if (!provider) {
            return JsonResponse(TwainUnavailableResponse());
        }

        mt::TwainScanCancelRequest cancel_request;
        cancel_request.task_id = JsonStringField(request, "task_id");
        const mt::TwainScanResult result = provider->CancelScan(cancel_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["accepted"] = result.accepted;
        response["task_id"] = result.task_id;
        response["task"] = TwainScanTaskToJson(result.task);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_device_list_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        const std::vector<SdkDeviceDescriptor> devices = SharedPrivateDeviceProvider().ListDevices();
        nlohmann::json response = BuildProviderResponse(ToCode(SdkStatusCode::Ok), "ok");
        response["devices"] = nlohmann::json::array();
        for (std::vector<SdkDeviceDescriptor>::const_iterator it = devices.begin();
             it != devices.end();
             ++it) {
            response["devices"].push_back(DeviceDescriptorToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_device_get_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }
        if (!request.contains("device_id") || !request["device_id"].is_string()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "device_id required");
        }

        SdkDeviceOpenRequest get_request;
        get_request.device_id = request["device_id"].get<std::string>();
        const SdkDeviceOpenResult result = SharedPrivateDeviceProvider().GetDevice(get_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["opened"] = result.opened;
        response["device"] = DeviceDescriptorToJson(result.device);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_device_open_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkDeviceOpenRequest open_request;
        open_request.device_id = JsonStringField(request, "device_id");
        open_request.width = JsonIntField(request, "width");
        open_request.height = JsonIntField(request, "height");
        open_request.fps = JsonIntField(request, "fps");
        open_request.pixel_format = JsonStringField(request, "pixel_format", open_request.pixel_format);
        if (open_request.device_id.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "device_id required");
        }

        const SdkDeviceOpenResult result = SharedPrivateDeviceProvider().OpenDevice(open_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["opened"] = result.opened;
        response["device"] = DeviceDescriptorToJson(result.device);
        if (!result.device_sn.empty()) {
            response["device_sn"] = result.device_sn;
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_device_close_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkDeviceCloseRequest close_request;
        close_request.device_id = JsonStringField(request, "device_id");
        if (close_request.device_id.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "device_id required");
        }

        const SdkDeviceCloseResult result = SharedPrivateDeviceProvider().CloseDevice(close_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["closed"] = result.closed;
        response["was_opened"] = result.was_opened;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_device_capture_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkCaptureRequest capture_request;
        capture_request.device_id = JsonStringField(request, "device_id");
        capture_request.output_dir = JsonStringField(request, "output_dir");
        capture_request.include_base64 = JsonBoolField(request, "include_base64");
        capture_request.timeout_ms = JsonIntField(request, "timeout_ms", capture_request.timeout_ms);
        if (capture_request.device_id.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "device_id required");
        }

        std::shared_ptr<CaptureWaitState> wait_state(new CaptureWaitState());
        SharedPrivateDeviceProvider().CaptureStill(
            capture_request,
            [wait_state](const SdkCaptureResult& result) {
                {
                    std::lock_guard<std::mutex> lock(wait_state->mutex);
                    if (wait_state->completed) {
                        SDK_PRIVATE_LOG_INFO("device.capture c_api_late_callback_ignored code={}", result.code);
                        return;
                    }
                    wait_state->result = result;
                    wait_state->completed = true;
                }
                wait_state->condition.notify_one();
            });
        SdkCaptureResult capture_result;
        {
            std::unique_lock<std::mutex> lock(wait_state->mutex);
            const int timeout_ms = capture_request.timeout_ms > 0 ? capture_request.timeout_ms : kDefaultCaptureTimeoutMs;
            if (!wait_state->completed &&
                !wait_state->condition.wait_for(lock, std::chrono::milliseconds(timeout_ms + 1000), [wait_state]() {
                    return wait_state->completed;
                })) {
                wait_state->result.code = ToCode(SdkStatusCode::CaptureTimeout);
                wait_state->result.message = "capture timeout";
                wait_state->completed = true;
                SDK_PRIVATE_LOG_WARN("device.capture c_api_wait_timeout timeout_ms={}", timeout_ms);
            }
            capture_result = wait_state->result;
        }
        return JsonResponse(CaptureResultToJson(capture_result));
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" {

typedef void (*CzurSdkPrivateVideoFrameCallback)(const char* frame_json,
                                                 const unsigned char* payload,
                                                 size_t payload_size,
                                                 void* user_data);

}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_video_start_json(
    const char* request_json,
    CzurSdkPrivateVideoFrameCallback callback,
    void* user_data) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkVideoStartRequest start_request;
        start_request.device_id = JsonStringField(request, "device_id");
        start_request.stream_id = JsonStringField(request, "stream_id");
        start_request.width = JsonIntField(request, "width");
        start_request.height = JsonIntField(request, "height");
        start_request.fps = JsonIntField(request, "fps");
        start_request.pixel_format = JsonStringField(request, "pixel_format", start_request.pixel_format);
        start_request.page_processing = JsonStringField(request, "page_processing");
        start_request.single_page_realtime_detect_rects =
            JsonBoolField(request, "single_page_realtime_detect_rects");
        start_request.single_page_multi_target_paging =
            JsonBoolField(request, "single_page_multi_target_paging");
        if (start_request.device_id.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "device_id required");
        }
        if (start_request.stream_id.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "stream_id required");
        }

        const SdkVideoStartResult result = SharedPrivateDeviceProvider().StartVideo(
            start_request,
            [callback, user_data](const SdkVideoFrame& frame) {
                if (callback == NULL || frame.payload.empty()) {
                    return;
                }
                nlohmann::json rects = nlohmann::json::array();
                for (std::vector<SdkRect4P>::const_iterator it = frame.detected_rects.begin();
                     it != frame.detected_rects.end();
                     ++it) {
                    rects.push_back(RectToJson(*it));
                }
                const nlohmann::json frame_json =
                    nlohmann::json{{"device_id", frame.device_id},
                                   {"stream_id", frame.stream_id},
                                   {"frame_seq", frame.frame_seq},
                                   {"timestamp_ms", frame.timestamp_ms},
                                   {"width", frame.width},
                                   {"height", frame.height},
                                   {"pixel_format", frame.pixel_format},
                                   {"detected_rects", rects},
                                   {"detected_rects_source_width", frame.detected_rects_source_width},
                                   {"detected_rects_source_height", frame.detected_rects_source_height}};
                const std::string frame_text = frame_json.dump();
                callback(frame_text.c_str(),
                         frame.payload.empty() ? NULL : &frame.payload[0],
                         frame.payload.size(),
                         user_data);
            });
        return JsonResponse(VideoStartResultToJson(result));
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_video_stop_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkVideoStopRequest stop_request;
        stop_request.device_id = JsonStringField(request, "device_id");
        if (stop_request.device_id.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "device_id required");
        }

        const SdkVideoStopResult result = SharedPrivateDeviceProvider().StopVideo(stop_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["stopped"] = result.stopped;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_video_format_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkVideoFormatRequest format_request;
        format_request.device_id = JsonStringField(request, "device_id");
        format_request.width = JsonIntField(request, "width");
        format_request.height = JsonIntField(request, "height");
        format_request.fps = JsonIntField(request, "fps");
        format_request.pixel_format = JsonStringField(request, "pixel_format", format_request.pixel_format);
        if (format_request.device_id.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "device_id required");
        }

        const SdkVideoFormatResult result = SharedPrivateDeviceProvider().SetVideoFormat(format_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["applied"] = result.applied;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_video_profile_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkVideoProfileRequest profile_request;
        profile_request.device_id = JsonStringField(request, "device_id");
        profile_request.page_processing = JsonStringField(request, "page_processing");
        profile_request.single_page_realtime_detect_rects =
            JsonBoolField(request, "single_page_realtime_detect_rects");
        profile_request.single_page_multi_target_paging =
            JsonBoolField(request, "single_page_multi_target_paging");
        if (profile_request.device_id.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "device_id required");
        }

        const SdkVideoProfileResult result = SharedPrivateDeviceProvider().SetVideoProfile(profile_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["applied"] = result.applied;
        response["page_processing"] = result.page_processing;
        response["single_page_realtime_detect_rects"] = result.single_page_realtime_detect_rects;
        response["single_page_multi_target_paging"] = result.single_page_multi_target_paging;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_capture_turn_detect_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkTurnDetectRequest turn_request;
        turn_request.device_id = JsonStringField(request, "device_id");
        turn_request.enabled = JsonBoolField(request, "enabled");
        turn_request.auto_capture = JsonBoolField(request, "auto_capture");
        turn_request.scan_device_type = JsonIntField(request, "scan_device_type", turn_request.scan_device_type);
        turn_request.cooldown_ms = JsonIntField(request, "cooldown_ms", turn_request.cooldown_ms);
        if (turn_request.device_id.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "device_id required");
        }
        if (turn_request.cooldown_ms < 0) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "cooldown_ms must be non-negative");
        }

        const SdkTurnDetectResult result = SharedPrivateDeviceProvider().SetTurnDetect(turn_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["applied"] = result.applied;
        response["enabled"] = result.enabled;
        response["auto_capture"] = result.auto_capture;
        response["scan_device_type"] = result.scan_device_type;
        response["cooldown_ms"] = result.cooldown_ms;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_graphic_process_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        PrivateGraphicProvider provider;
        const SdkImageProcessResult result = provider.Process(ImageProcessRequestFromJson(request));
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["processed"] = result.processed;
        response["output_path"] = result.output_path;
        response["outputs"] = nlohmann::json::array();
        for (std::vector<SdkImageProcessOutput>::const_iterator it = result.outputs.begin();
             it != result.outputs.end();
             ++it) {
            response["outputs"].push_back(ImageProcessOutputToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_graphic_process_page_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        PrivateGraphicProvider provider;
        const SdkPageProcessResult result = provider.ProcessPage(PageProcessRequestFromJson(request));
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["processed"] = result.processed;
        response["unsupported"] = result.unsupported;
        response["output_path"] = result.output_path;
        response["outputs"] = nlohmann::json::array();
        for (std::vector<SdkPageOutput>::const_iterator it = result.outputs.begin();
             it != result.outputs.end();
             ++it) {
            response["outputs"].push_back(PageOutputToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_graphic_apply_color_mode_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        PrivateGraphicProvider provider;
        const SdkColorModeResult result = provider.ApplyColorMode(ColorModeRequestFromJson(request));
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["processed"] = result.processed;
        response["unsupported"] = result.unsupported;
        response["output_path"] = result.output_path;
        response["width"] = result.width;
        response["height"] = result.height;
        response["size"] = result.size;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_graphic_convert_image_format_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        PrivateGraphicProvider provider;
        const SdkFormatConvertResult result = provider.ConvertImageFormat(FormatConvertRequestFromJson(request));
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["converted"] = result.converted;
        response["passthrough"] = result.passthrough;
        response["output_path"] = result.output_path;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_file_convert_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        PrivateOfdProvider provider;
        const SdkFileConvertResult result = provider.Convert(FileConvertRequestFromJson(request));
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["accepted"] = result.accepted;
        response["converted"] = result.converted;
        response["output_path"] = result.output_path;
        response["output_paths"] = result.output_paths;
        response["source_format"] = result.source_format;
        response["source_page_count"] = result.source_page_count;
        response["selected_page_count"] = result.selected_page_count;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_recognition_barcode_detect_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        PrivateRecognitionProvider provider;
        const SdkBarcodeDetectResult result = provider.DetectBarcode(BarcodeDetectRequestFromJson(request));
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["detected"] = result.detected;
        response["input_path"] = result.input_path;
        response["width"] = result.width;
        response["height"] = result.height;
        response["barcodes"] = nlohmann::json::array();
        for (std::vector<SdkBarcodeResult>::const_iterator it = result.barcodes.begin();
             it != result.barcodes.end();
             ++it) {
            response["barcodes"].push_back(BarcodeToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

PrivateOcrProvider& SharedPrivateOcrProvider() {
    static PrivateOcrProvider provider;
    return provider;
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_storage_cleanup_temp_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }
        const std::string work_dir = JsonStringField(request, "work_dir");
        if (work_dir.empty()) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), "work_dir required");
        }

        // DLL 内部串行化清理，避免多个 SDK Open 连接同时删除同一组目录。
        static std::mutex cleanup_mu;
        std::lock_guard<std::mutex> cleanup_lock(cleanup_mu);
        SdkStorageActiveTasks active;
        // 与普通拍照注册和实际写盘串行，防止 timeout 回调后仍在处理的帧跨过删除边界。
        PrivateDeviceProvider& device_provider = SharedPrivateDeviceProvider();
        std::unique_lock<std::mutex> capture_storage_lock = device_provider.TryLockCaptureStorageLifecycle();
        if (!capture_storage_lock.owns_lock()) {
            nlohmann::json response = BuildProviderResponse(ToCode(SdkStatusCode::StorageBusy),
                                                            "temporary storage is busy");
            response["active"] = nlohmann::json{{"capture", 1}, {"ocr", 0}, {"sane", 0}, {"twain", 0}};
            return JsonResponse(response);
        }
        active.capture = device_provider.ActiveCaptureCount();
        // private 持有真实 capture/OCR/TWAIN 实例，必须在实际删除前进行第二次 busy 检查。
        active.ocr = SharedPrivateOcrProvider().ActiveTaskCount();
#if defined(_WIN32)
        const std::shared_ptr<mt::ITwainProvider> twain = SharedPrivateTwainProvider();
        active.twain = mt::ActiveWindowsTwainTaskCount(twain);
#endif
        if (active.Total() != 0) {
            // private 发现活跃任务时不等待、不取消，也不触碰 capture/tasks。
            nlohmann::json response = BuildProviderResponse(ToCode(SdkStatusCode::StorageBusy),
                                                            "temporary storage is busy");
            response["active"] = nlohmann::json{{"capture", active.capture},
                                                 {"ocr", active.ocr},
                                                 {"sane", 0},
                                                 {"twain", active.twain}};
            return JsonResponse(response);
        }

        SdkStorageCleanupResult result = RemoveSdkOpenTempDirectories(work_dir);
        if (IsOkStatusCode(result.code)) {
            // 先成功删除磁盘目录，再清理终态内存记录；删除失败时保留查询状态便于排障。
            result.cleared_ocr_task_count = SharedPrivateOcrProvider().ClearFinishedTasks();
            result.cleared_task_count += result.cleared_ocr_task_count;
#if defined(_WIN32)
            if (twain) {
                result.cleared_twain_task_count = mt::ClearFinishedWindowsTwainTasks(twain);
                result.cleared_task_count += result.cleared_twain_task_count;
            }
#endif
        }
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["capture_removed"] = result.capture_removed;
        response["tasks_removed"] = result.tasks_removed;
        response["cleared_task_count"] = result.cleared_task_count;
        response["cleared_ocr_task_count"] = result.cleared_ocr_task_count;
        response["cleared_twain_task_count"] = result.cleared_twain_task_count;
        response["active"] = nlohmann::json{{"capture", result.active.capture},
                                             {"ocr", result.active.ocr},
                                             {"sane", result.active.sane},
                                             {"twain", result.active.twain}};
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_ocr_recognize_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        std::string language;
        std::string language_error;
        if (!ReadSdkOcrLanguage(request, &language, &language_error)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), language_error);
        }
        SdkOcrRecognizeRequest recognize_request = OcrRecognizeRequestFromJson(request);
        recognize_request.language = language;
        const SdkOcrRecognizeResult result = SharedPrivateOcrProvider().Recognize(recognize_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["task_id"] = result.task_id;
        response["task"] = OcrTaskToJson(result.task);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_ocr_get_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkOcrGetRequest get_request;
        get_request.task_id = JsonStringField(request, "task_id");
        const SdkOcrGetResult result = SharedPrivateOcrProvider().GetTask(get_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["task"] = OcrTaskToJson(result.task);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_ocr_cancel_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkOcrCancelRequest cancel_request;
        cancel_request.task_id = JsonStringField(request, "task_id");
        const SdkOcrCancelResult result = SharedPrivateOcrProvider().Cancel(cancel_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["cancelled"] = result.cancelled;
        response["task"] = OcrTaskToJson(result.task);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_ocr_extract_text_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        SdkOcrExtractTextRequest extract_request;
        std::string language_error;
        if (!ReadSdkOcrLanguage(request, &extract_request.language, &language_error)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidParams), language_error);
        }
        extract_request.input_upload_id = JsonStringField(request, "input_upload_id");
        extract_request.input_path = JsonStringField(request, "input_path");
        const SdkOcrExtractTextResult result = SharedPrivateOcrProvider().ExtractText(extract_request);
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["recognized"] = result.recognized;
        response["input_path"] = result.input_path;
        response["width"] = result.width;
        response["height"] = result.height;
        response["blocks"] = nlohmann::json::array();
        for (std::vector<SdkOcrTextBlock>::const_iterator it = result.blocks.begin();
             it != result.blocks.end();
             ++it) {
            response["blocks"].push_back(OcrTextBlockToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_image_enhance_capabilities_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        PrivateImageEnhanceProvider provider;
        const SdkImageEnhanceCapabilityResult result = provider.ListCapabilities();
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["provider"] = result.provider;
        response["kind"] = result.kind;
        response["available"] = result.available;
        response["capabilities"] = nlohmann::json::array();
        for (std::vector<SdkImageEnhanceCapability>::const_iterator it = result.capabilities.begin();
             it != result.capabilities.end();
             ++it) {
            response["capabilities"].push_back(ImageEnhanceCapabilityToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_image_enhance_run_step_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseProviderRequestJson(request_json, &request, &parse_message)) {
            return ProviderErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        PrivateImageEnhanceProvider provider;
        const SdkImageEnhanceStepResult result = provider.RunStep(ImageEnhanceStepRequestFromJson(request));
        nlohmann::json response = BuildProviderResponse(result.code, result.message);
        response["processed"] = result.processed;
        response["metadata_json"] = result.metadata_json;
        response["warnings"] = result.warnings;
        response["pages"] = nlohmann::json::array();
        for (std::vector<SdkImageEnhancePage>::const_iterator it = result.pages.begin();
             it != result.pages.end();
             ++it) {
            response["pages"].push_back(ImageEnhancePageToJson(*it));
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ProviderErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API void czur_sdk_private_providers_free_string(const char* value) {
    std::free(const_cast<char*>(value));
}
#endif

} // namespace private_impl
} // namespace sdk
} // namespace editor
