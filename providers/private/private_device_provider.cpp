// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "private_device_provider.h"

#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

#include "device_command.h"
#include "device_command_sn.h"
#include "capture_pending_identity.h"
#if defined(_WIN32)
#include "device_command_windows.h"
#endif
#include "czcv/czcv_all_header.hpp"
#include "graphic/czcv_result_ownership.h"
#include "graphic/graphic_algorithm_runtime.h"
#include "image_write_utils.h"
#include "opencv2/opencv.hpp"
#include "private_sdk_support.h"
#include "private_device_frame_codec.h"
#include "support/private_sdk_executable_path.h"
#include "support/private_sdk_logger.h"
#include "sdk_status_code.h"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

const int kStillImageActionSuppressAfterCaptureMs = 3000;

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

bool FileExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string JoinPath(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    if (IsPathSeparator(left[left.size() - 1])) {
        return left + right;
    }
    return left + PathSeparator() + right;
}

std::string ParentDir(const std::string& path) {
    const std::string::size_type pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return "";
    }
    if (pos == 0) {
        return path.substr(0, 1);
    }
#if defined(_WIN32)
    if (pos == 2 && path.size() >= 3 && path[1] == ':' && IsPathSeparator(path[2])) {
        return path.substr(0, 3);
    }
#endif
    return path.substr(0, pos);
}

void RehomeCzcvDetectedRects(czcv::CutParams& params) {
    graphic::RehomeCzcvStdVector(params.detectedRects);
}

std::string FormatDisplayName(const editor::devices::DeviceInfo& info,
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

int64_t NowMs() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

bool HasBoolFeature(const std::shared_ptr<editor::devices::DeviceConfigData>& config,
                    const std::string& feature) {
    if (!config) {
        return false;
    }
    const std::map<std::string, bool>::const_iterator it = config->boolFeatures.find(feature);
    return it != config->boolFeatures.end() && it->second;
}

std::string TrimAsciiWhitespace(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) -> bool {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
    }), value.end());
    return value;
}

bool TryParsePositiveIntToken(const std::string& token, int* value) {
    if (value != NULL) {
        *value = 0;
    }
    if (token.empty()) {
        return false;
    }
    char* end = NULL;
    const long parsed = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || (end != NULL && *end != '\0') || parsed <= 0 || parsed > 100000) {
        return false;
    }
    if (value != NULL) {
        *value = static_cast<int>(parsed);
    }
    return true;
}

std::vector<std::string> ResolutionRangeTokens(const std::shared_ptr<editor::devices::DeviceConfigData>& config) {
    std::vector<std::string> tokens;
    if (!config) {
        return tokens;
    }

    const std::map<std::string, std::vector<std::string> >::const_iterator array_it =
        config->arrayFeatures.find("ResolutionRange");
    if (array_it != config->arrayFeatures.end() && !array_it->second.empty()) {
        tokens = array_it->second;
    } else {
        const std::map<std::string, std::string>::const_iterator string_it =
            config->stringFeatures.find("ResolutionRange");
        if (string_it != config->stringFeatures.end()) {
            std::istringstream ss(string_it->second);
            std::string token;
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }
        }
    }

    for (std::vector<std::string>::iterator it = tokens.begin(); it != tokens.end(); ++it) {
        *it = TrimAsciiWhitespace(*it);
    }
    return tokens;
}

bool MakeOutputTargetSizeOption(int target_size,
                                bool is_device_default,
                                SdkOutputTargetSizeOption* option) {
    int width = 0;
    int height = 0;
    if (!ResolveSdkOutputTargetSize(target_size, &width, &height)) {
        return false;
    }
    if (option != NULL) {
        option->target_size = target_size;
        option->width = width;
        option->height = height;
        option->is_device_default = is_device_default;
    }
    return true;
}

void AddOutputTargetSizeOption(std::vector<SdkOutputTargetSizeOption>* options,
                               std::set<int>* seen,
                               int target_size,
                               bool is_device_default) {
    if (options == NULL || seen == NULL) {
        return;
    }
    const std::set<int>::const_iterator seen_it = seen->find(target_size);
    if (seen_it != seen->end()) {
        if (is_device_default) {
            for (std::vector<SdkOutputTargetSizeOption>::iterator it = options->begin(); it != options->end(); ++it) {
                if (it->target_size == target_size) {
                    it->is_device_default = true;
                    break;
                }
            }
        }
        return;
    }

    SdkOutputTargetSizeOption option;
    if (!MakeOutputTargetSizeOption(target_size, is_device_default, &option)) {
        return;
    }
    options->push_back(option);
    seen->insert(target_size);
}

SdkCaptureOutputCapabilities BuildCaptureOutputCapabilities(
    const std::shared_ptr<editor::devices::DeviceConfigData>& config,
    bool image_transfer_protocol) {
    SdkCaptureOutputCapabilities capabilities;
    if (!image_transfer_protocol || !config) {
        return capabilities;
    }

    const int default_pixel = config->pixels;
    bool default_found = false;
    std::set<int> seen;
    const std::vector<std::string> tokens = ResolutionRangeTokens(config);
    for (std::vector<std::string>::const_iterator it = tokens.begin(); it != tokens.end(); ++it) {
        int target_size = 0;
        if (!TryParsePositiveIntToken(*it, &target_size)) {
            continue;
        }
        const bool is_default = target_size == default_pixel;
        if (is_default) {
            default_found = true;
        }
        AddOutputTargetSizeOption(&capabilities.target_sizes, &seen, target_size, is_default);
    }

    if (!default_found && default_pixel > 0 && seen.find(default_pixel) == seen.end()) {
        SdkOutputTargetSizeOption option;
        if (MakeOutputTargetSizeOption(default_pixel, true, &option)) {
            std::vector<SdkOutputTargetSizeOption>::iterator insert_pos = capabilities.target_sizes.end();
            for (std::vector<SdkOutputTargetSizeOption>::iterator it = capabilities.target_sizes.begin();
                 it != capabilities.target_sizes.end();
                 ++it) {
                if (default_pixel < it->target_size) {
                    insert_pos = it;
                    break;
                }
            }
            capabilities.target_sizes.insert(insert_pos, option);
            seen.insert(default_pixel);
        }
    }

    capabilities.target_size_supported = !capabilities.target_sizes.empty();
    return capabilities;
}

bool EnsureDirectory(const std::string& path) {
#if defined(_WIN32)
    return path.empty() || ::_mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return path.empty() || ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

std::vector<uint8_t> ReadBinaryFile(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) {
        return std::vector<uint8_t>();
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

uint64_t FileSize(const std::string& path) {
    struct stat st;
    return path.empty() || ::stat(path.c_str(), &st) != 0 ? 0 : static_cast<uint64_t>(st.st_size);
}

bool WriteBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    if (path.empty() || bytes.empty()) {
        return false;
    }
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

std::string HardGrabDirectory() {
    return JoinPath(JoinPath(GetSdkRuntimeDir(), "capture"), "hardgrab");
}

std::string HardGrabPath(uint64_t token, int64_t timestamp_ms, const char* name) {
    std::ostringstream path;
    path << "hardgrab-" << timestamp_ms << "-" << token;
    const std::string capture_dir = JoinPath(HardGrabDirectory(), path.str());
    if (!EnsureDirectoryRecursive(capture_dir)) {
        return "";
    }
    return JoinPath(capture_dir, name);
}

void RefreshLaserConfigIfSupported(const std::shared_ptr<editor::devices::Device>& device,
                                   editor::devices::DeviceManager* manager) {
    if (!device || manager == NULL || !HasBoolFeature(device->getDeviceConfig(), "Laser")) {
        return;
    }
    const std::string config_path = GetSdkDeviceLaserConfigPath(device->getDeviceId());
    const std::string config_dir = ParentDir(config_path);
    if (!EnsureDirectoryRecursive(config_dir)) {
        SDK_PRIVATE_LOG_WARN("device.laser_config create_dir_failed device={} dir={}",
                             SafeDeviceId(device->getDeviceId()),
                             SafePathSummary(config_dir));
        return;
    }

    editor::devices::DeviceCommand command(device, manager);
    if (!command.readLaserConfig(config_path)) {
        SDK_PRIVATE_LOG_WARN("device.laser_config read_failed device={} path={}",
                             SafeDeviceId(device->getDeviceId()),
                             SafePathSummary(config_path));
    } else {
        SDK_PRIVATE_LOG_INFO("device.laser_config refreshed device={} path={}",
                             SafeDeviceId(device->getDeviceId()),
                             SafePathSummary(config_path));
    }
}

bool ParseJpegSize(const uint8_t* data, size_t size, int* width, int* height) {
    if (width != NULL) {
        *width = 0;
    }
    if (height != NULL) {
        *height = 0;
    }
    if (data == NULL || size < 4 || data[0] != 0xff || data[1] != 0xd8) {
        return false;
    }

    size_t pos = 2;
    while (pos + 9 < size) {
        if (data[pos] != 0xff) {
            ++pos;
            continue;
        }
        while (pos < size && data[pos] == 0xff) {
            ++pos;
        }
        if (pos >= size) {
            return false;
        }
        const uint8_t marker = data[pos++];
        if (marker == 0xd8 || marker == 0xd9 || marker == 0x01) {
            continue;
        }
        if (pos + 2 > size) {
            return false;
        }
        const size_t segment_size = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
        if (segment_size < 2 || pos + segment_size > size) {
            return false;
        }
        if ((marker >= 0xc0 && marker <= 0xc3) ||
            (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) ||
            (marker >= 0xcd && marker <= 0xcf)) {
            if (segment_size < 7) {
                return false;
            }
            if (height != NULL) {
                *height = (static_cast<int>(data[pos + 3]) << 8) | data[pos + 4];
            }
            if (width != NULL) {
                *width = (static_cast<int>(data[pos + 5]) << 8) | data[pos + 6];
            }
            return width == NULL || height == NULL || (*width > 0 && *height > 0);
        }
        pos += segment_size;
    }
    return false;
}

bool IsSupportedVideoPixelFormat(const std::string& pixel_format) {
    return pixel_format.empty() || pixel_format == "mjpeg" || pixel_format == "jpeg" || pixel_format == "bgr24";
}

std::string NormalizeVideoPixelFormat(const std::string& pixel_format) {
    if (pixel_format == "bgr24") {
        return "bgr24";
    }
    return "mjpeg";
}

bool EncodeBgrFrameToJpeg(const editor::devices::GenericFrame& frame, std::vector<uint8_t>* jpeg_payload) {
    if (jpeg_payload == NULL || frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    const size_t expected_size = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 3;
    if (frame.data.size() < expected_size) {
        return false;
    }
    cv::Mat source(frame.height, frame.width, CV_8UC3, const_cast<uint8_t*>(&frame.data[0]));
    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(85);
    return EncodeJpegBytesLocked(source, jpeg_payload, params);
}

std::vector<int> CaptureJpegParams() {
    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(95);
    return params;
}

bool ResolveCaptureExportResolution(const std::shared_ptr<editor::devices::Device>& device,
                                    int source_width,
                                    int source_height,
                                    int* target_width,
                                    int* target_height) {
    if (target_width != NULL) {
        *target_width = 0;
    }
    if (target_height != NULL) {
        *target_height = 0;
    }

    if (!device || source_width <= 0 || source_height <= 0) {
        return false;
    }

    const std::shared_ptr<editor::devices::DeviceConfigData> config = device->getDeviceConfig();
    if (!config || !config->exportScale) {
        return false;
    }

    const std::map<std::string, editor::devices::FWTypeConfig>::const_iterator it =
        config->fwType.find("default");
    if (it == config->fwType.end()) {
        return false;
    }

    const editor::devices::FWTypeConfig& fw_config = it->second;
    if (fw_config.exportPicResolutionWidth <= 0 || fw_config.exportPicResolutionHeight <= 0) {
        return false;
    }
    if (source_width == fw_config.exportPicResolutionWidth &&
        source_height == fw_config.exportPicResolutionHeight) {
        return false;
    }
    if (fw_config.takePicResolutionWidth > 0 &&
        fw_config.takePicResolutionHeight > 0 &&
        (source_width != fw_config.takePicResolutionWidth ||
         source_height != fw_config.takePicResolutionHeight)) {
        return false;
    }

    if (target_width != NULL) {
        *target_width = fw_config.exportPicResolutionWidth;
    }
    if (target_height != NULL) {
        *target_height = fw_config.exportPicResolutionHeight;
    }
    return true;
}

bool DecodeJpeg(const std::vector<uint8_t>& jpeg_bytes, cv::Mat* image) {
    if (image == NULL || jpeg_bytes.empty()) {
        return false;
    }
    *image = cv::imdecode(jpeg_bytes, cv::IMREAD_COLOR);
    return !image->empty();
}

bool WriteCaptureJpegOutput(const std::string& image_path,
                            const std::vector<uint8_t>& jpeg_bytes,
                            int source_width,
                            int source_height,
                            int target_width,
                            int target_height,
                            int dpi) {
    if (image_path.empty() || jpeg_bytes.empty() || source_width <= 0 || source_height <= 0) {
        return false;
    }

    const int normalized_dpi = editor::sdk::private_impl::NormalizeImageDpi(dpi);
    if (target_width <= 0 || target_height <= 0 ||
        (source_width == target_width && source_height == target_height)) {
        return editor::sdk::private_impl::WriteJpegBytesWithDpi(
            image_path, jpeg_bytes, source_width, source_height, normalized_dpi);
    }

    cv::Mat original_image;
    if (!DecodeJpeg(jpeg_bytes, &original_image)) {
        return false;
    }

    cv::Mat resized_image;
    cv::resize(original_image,
               resized_image,
               cv::Size(target_width, target_height),
               0,
               0,
               cv::INTER_LANCZOS4);
    if (resized_image.empty()) {
        return false;
    }

    return editor::sdk::private_impl::WriteImageWithDpiLocked(
        image_path, resized_image, normalized_dpi, CaptureJpegParams());
}

int ResolveCaptureActualDpi(const std::shared_ptr<editor::devices::Device>& device) {
    const int dpi = (!device || !device->getDeviceConfig()) ? 0 : device->getDeviceConfig()->actualDPI;
    return editor::sdk::private_impl::NormalizeImageDpi(dpi);
}

int ResolveScanDeviceType(const std::shared_ptr<editor::devices::Device>& device) {
    if (!device || !device->getDeviceConfig()) {
        return 0;
    }
    const std::string& serial_type = device->getDeviceConfig()->deviceSerialType;
    if (serial_type == "ETM" || serial_type.find("ET") == 0) {
        return 0;
    }
    if (serial_type == "ETS" || serial_type == "AURA") {
        return 1;
    }
    return 2;
}

SdkRect4P ToSdkRect(const std::vector<cv::Point2f>& points) {
    SdkRect4P rect;
    if (points.size() >= 4) {
        rect.left_top.x = points[0].x;
        rect.left_top.y = points[0].y;
        rect.right_top.x = points[1].x;
        rect.right_top.y = points[1].y;
        rect.right_down.x = points[2].x;
        rect.right_down.y = points[2].y;
        rect.left_down.x = points[3].x;
        rect.left_down.y = points[3].y;
    }
    return rect;
}

SdkVideoResolution ToSdkResolution(const editor::devices::Resolution& resolution) {
    SdkVideoResolution sdk_resolution;
    sdk_resolution.width = resolution.width;
    sdk_resolution.height = resolution.height;
    sdk_resolution.real_width = resolution.realWidth;
    sdk_resolution.real_height = resolution.realHeight;
    sdk_resolution.fps = resolution.fps;
    sdk_resolution.pixel_format = "mjpeg";
    sdk_resolution.is_default = resolution.isDefault;
    return sdk_resolution;
}

struct StillImagePayloadSelection {
    std::vector<uint8_t> original_from_path;
    std::vector<uint8_t> laser_from_path;
    const std::vector<uint8_t>* original = NULL;
    const std::vector<uint8_t>* laser = NULL;
};

// still-image 设备可能返回内存数据，也可能返回文件路径；选择逻辑统一二者，
// 随后由 Provider 负责将选中的原图写入 SDK runtime 临时目录或任务目录。
// 大小图成对返回时沿用 CZURScanner 口径：较大的图作为原图，较小的图作为激光图。
void SelectStillImagePayloads(const editor::devices::StillImagePair& image_pair,
                              StillImagePayloadSelection* selection) {
    if (selection == NULL) {
        return;
    }
    selection->original = &image_pair.first;
    if (!image_pair.originalPath.empty()) {
        selection->original_from_path = ReadBinaryFile(image_pair.originalPath);
        selection->original = &selection->original_from_path;
    }
    if (!image_pair.laserPath.empty()) {
        selection->laser_from_path = ReadBinaryFile(image_pair.laserPath);
        selection->laser = &selection->laser_from_path;
    }
    if (image_pair.isPair) {
        if (!image_pair.second.empty() && image_pair.first.size() >= image_pair.second.size()) {
            selection->original = &image_pair.first;
            selection->laser = &image_pair.second;
        } else if (!image_pair.second.empty()) {
            selection->original = &image_pair.second;
            selection->laser = &image_pair.first;
        } else if (!selection->original_from_path.empty() &&
                   !selection->laser_from_path.empty() &&
                   selection->original_from_path.size() < selection->laser_from_path.size()) {
            selection->original = &selection->laser_from_path;
            selection->laser = &selection->original_from_path;
        } else if (!selection->original_from_path.empty()) {
            selection->original = &selection->original_from_path;
            selection->laser = selection->laser_from_path.empty() ? NULL : &selection->laser_from_path;
        }
    }
}

} // namespace

struct PrivateDeviceProvider::PendingCapture {
    SdkCaptureRequest request;
    SdkCaptureCallback callback;
    bool image_transfer_protocol = false;
    uint64_t capture_token = 0;
    int timeout_ms = kDefaultCaptureTimeoutMs;
    int64_t armed_at_ms = 0;
};

std::string PrivateDeviceProvider::ProviderName() const {
    return "czur-device-provider";
}

void PrivateDeviceProvider::SetDeviceActionEventSink(SdkDeviceActionEventCallback sink) {
    std::lock_guard<std::mutex> lock(state_mu_);
    device_action_event_sink_ = std::move(sink);
}

void PrivateDeviceProvider::SetDeviceEventSink(SdkDeviceEventCallback sink) {
    std::lock_guard<std::mutex> lock(state_mu_);
    device_event_sink_ = std::move(sink);
}

bool PrivateDeviceProvider::IsDeviceRecentlyRemoved(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(state_mu_);
    return recently_removed_device_ids_.find(device_id) != recently_removed_device_ids_.end();
}

std::vector<SdkDeviceDescriptor> PrivateDeviceProvider::ListDevices() const {
    SDK_PRIVATE_LOG_INFO("device.list begin");
    if (!EnsureDeviceManagerReady()) {
        SDK_PRIVATE_LOG_ERROR("device.list failed provider_not_ready");
        return std::vector<SdkDeviceDescriptor>();
    }

    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
    if (manager == NULL) {
        SDK_PRIVATE_LOG_ERROR("device.list failed manager_null");
        return std::vector<SdkDeviceDescriptor>();
    }
    manager->updateDeviceList();
    std::vector<SdkDeviceDescriptor> devices = BuildDeviceDescriptors(manager->getDevices());
    SDK_PRIVATE_LOG_INFO("device.list end count={}", devices.size());
    return devices;
}

SdkDeviceOpenResult PrivateDeviceProvider::GetDevice(const SdkDeviceOpenRequest& request) {
    SDK_PRIVATE_LOG_INFO("device.get begin device={}", SafeDeviceId(request.device_id));
    SdkDeviceOpenResult result = BuildDeviceResult(request.device_id, true);
    SDK_PRIVATE_LOG_INFO("device.get end device={} code={} opened={} resolutions={}",
                         SafeDeviceId(request.device_id),
                         result.code,
                         result.opened,
                         result.device.resolutions.size());
    return result;
}

SdkDeviceOpenResult PrivateDeviceProvider::OpenDevice(const SdkDeviceOpenRequest& request) {
    SDK_PRIVATE_LOG_INFO("device.open begin device={} requested={}x{} fps={} format={}",
                         SafeDeviceId(request.device_id),
                         request.width,
                         request.height,
                         request.fps,
                         request.pixel_format);
    SdkDeviceOpenResult result;
    if (!IsSupportedVideoPixelFormat(request.pixel_format)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "unsupported pixel_format";
        return result;
    }
    if (!EnsureDeviceManagerReady()) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        SDK_PRIVATE_LOG_ERROR("device.open failed device={} code={} message={}",
                              SafeDeviceId(request.device_id),
                              result.code,
                              result.message);
        return result;
    }

    const std::shared_ptr<editor::devices::Device> device = LookupDevice(request.device_id);
    if (!device) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "device not found";
        SDK_PRIVATE_LOG_WARN("device.open failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        return result;
    }

    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
    result.opened = manager != NULL && manager->openDevice(device);
    result.device = BuildDeviceDescriptor(device);
    if (!result.opened) {
        result.code = ToCode(SdkStatusCode::DeviceOpenFailed);
        result.message = "failed to open device";
        SDK_PRIVATE_LOG_ERROR("device.open failed device={} code={} message={}",
                              SafeDeviceId(request.device_id),
                              result.code,
                              result.message);
        return result;
    }
    RefreshLaserConfigIfSupported(device, manager);

    const editor::devices::Resolution selected = PickResolution(device, request.width, request.height, request.fps);
    if (selected.width <= 0) {
        result.code = ToCode(SdkStatusCode::PreviewResolutionUnsupported);
        result.message = "preview resolution unsupported";
        SDK_PRIVATE_LOG_WARN("device.open failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        return result;
    }
    if (request.width > 0 && (selected.width != request.width || selected.height != request.height)) {
        result.code = ToCode(SdkStatusCode::PreviewResolutionUnsupported);
        result.message = "preview resolution unsupported";
        SDK_PRIVATE_LOG_WARN("device.open failed device={} code={} requested={}x{} selected={}x{}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             request.width,
                             request.height,
                             selected.width,
                             selected.height);
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        selected_resolutions_[request.device_id] = selected;
    }
    result.device.resolutions.push_back(ToSdkResolution(selected));
    result.device_sn = ReadDeviceCommandSn(result.device.image_transfer_protocol, [device, manager]() {
        editor::devices::DeviceCommand command(device, manager);
        return command.readDeviceSerial();
    });
    if (result.device.image_transfer_protocol && result.device_sn.empty()) {
        SDK_PRIVATE_LOG_WARN("device.open device_sn unavailable device={}", SafeDeviceId(request.device_id));
    }
    SDK_PRIVATE_LOG_INFO("device.open end device={} code={} selected={}x{} fps={} image_transfer={}",
                         SafeDeviceId(request.device_id),
                         result.code,
                         selected.width,
                         selected.height,
                         selected.fps,
                         result.device.image_transfer_protocol);
    return result;
}

SdkDeviceCloseResult PrivateDeviceProvider::CloseDevice(const SdkDeviceCloseRequest& request) {
    SDK_PRIVATE_LOG_INFO("device.close begin device={}", SafeDeviceId(request.device_id));
    SdkDeviceCloseResult result;
    if (!EnsureDeviceManagerReady()) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        SDK_PRIVATE_LOG_ERROR("device.close failed device={} code={} message={}",
                              SafeDeviceId(request.device_id),
                              result.code,
                              result.message);
        return result;
    }

    const std::shared_ptr<editor::devices::Device> device = LookupDevice(request.device_id);
    if (!device) {
        if (!IsDeviceRecentlyRemoved(request.device_id)) {
            result.code = ToCode(SdkStatusCode::DeviceNotFound);
            result.message = "device not found";
            SDK_PRIVATE_LOG_WARN("device.close failed device={} code={} message={}",
                                 SafeDeviceId(request.device_id),
                                 result.code,
                                 result.message);
            return result;
        }
        SdkCaptureResult capture_result;
        capture_result.code = ToCode(SdkStatusCode::StreamNotReady);
        capture_result.message = "device removed during capture";
        CompletePendingCapture(request.device_id, std::shared_ptr<PendingCapture>(), capture_result, "device_close");
        DisarmStreamState(request.device_id, true);
        {
            std::lock_guard<std::mutex> lock(state_mu_);
            selected_resolutions_.erase(request.device_id);
            still_image_action_suppressed_until_ms_.erase(request.device_id);
        }
        result.closed = true;
        result.message = "device already removed";
        SDK_PRIVATE_LOG_INFO("device.close end device={} code={} closed={} was_opened=false reason={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.closed,
                             "device_removed");
        return result;
    }

    SdkCaptureResult capture_result;
    capture_result.code = ToCode(SdkStatusCode::StreamNotReady);
    capture_result.message = "device closed during capture";
    CompletePendingCapture(request.device_id, std::shared_ptr<PendingCapture>(), capture_result, "device_close");
    DisarmStreamState(request.device_id, true);

    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
    result.was_opened = device->isOpened();
    if (device->isStreaming() && manager != NULL) {
        manager->stopStreaming(device);
    }
    if (manager != NULL && device->isOpened()) {
        manager->closeDevice(device);
    }
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        selected_resolutions_.erase(request.device_id);
        still_image_action_suppressed_until_ms_.erase(request.device_id);
    }
    result.closed = true;
    SDK_PRIVATE_LOG_INFO("device.close end device={} code={} closed={} was_opened={}",
                         SafeDeviceId(request.device_id),
                         result.code,
                         result.closed,
                         result.was_opened);
    return result;
}

void PrivateDeviceProvider::CaptureStill(const SdkCaptureRequest& request, SdkCaptureCallback callback) {
    SDK_PRIVATE_LOG_INFO("device.capture begin device={} output_dir={} timeout_ms={} include_base64={}",
                         SafeDeviceId(request.device_id),
                         SafePathSummary(request.output_dir),
                         request.timeout_ms,
                         request.include_base64);
    SdkCaptureResult result;
    if (!callback) {
        SDK_PRIVATE_LOG_WARN("device.capture ignored device={} callback_missing", SafeDeviceId(request.device_id));
        return;
    }
    if (!EnsureDeviceManagerReady()) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        SDK_PRIVATE_LOG_ERROR("device.capture failed device={} code={} message={}",
                              SafeDeviceId(request.device_id),
                              result.code,
                              result.message);
        callback(result);
        return;
    }

    const std::shared_ptr<editor::devices::Device> device = LookupDevice(request.device_id);
    if (!device) {
        result.code = ToCode(SdkStatusCode::DeviceNotFound);
        result.message = "device not found";
        SDK_PRIVATE_LOG_WARN("device.capture failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        callback(result);
        return;
    }
    if (!device->isOpened()) {
        result.code = ToCode(SdkStatusCode::DeviceNotOpen);
        result.message = "device not open";
        SDK_PRIVATE_LOG_WARN("device.capture failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        callback(result);
        return;
    }
    if (!device->isStreaming()) {
        result.code = ToCode(SdkStatusCode::StreamNotReady);
        result.message = "video stream not started";
        SDK_PRIVATE_LOG_WARN("device.capture failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        callback(result);
        return;
    }

    // 从目录准备到 pending 注册与 cleanup 串行；校验失败 callback 不在该锁内执行。
    std::unique_lock<std::mutex> storage_lifecycle_lock(capture_storage_lifecycle_mu_);
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        if (active_stream_sequences_.find(request.device_id) == active_stream_sequences_.end()) {
            result.code = ToCode(SdkStatusCode::StreamNotReady);
            result.message = "sdk video stream not started";
            SDK_PRIVATE_LOG_WARN("device.capture failed device={} code={} message={}",
                                 SafeDeviceId(request.device_id),
                                 result.code,
                                 result.message);
            callback(result);
            return;
        }
        if (pending_captures_.find(request.device_id) != pending_captures_.end()) {
            result.code = ToCode(SdkStatusCode::DeviceBusy);
            result.message = "device already has an active capture";
            SDK_PRIVATE_LOG_WARN("device.capture failed device={} code={} message={}",
                                 SafeDeviceId(request.device_id),
                                 result.code,
                                 result.message);
            callback(result);
            return;
        }
    }

    const std::string output_dir = request.output_dir.empty() ? "." : request.output_dir;
    if (!EnsureDirectoryRecursive(output_dir)) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "failed to create capture output directory";
        SDK_PRIVATE_LOG_ERROR("device.capture failed device={} code={} output_dir={} message={}",
                              SafeDeviceId(request.device_id),
                              result.code,
                              SafePathSummary(output_dir),
                              result.message);
        callback(result);
        return;
    }

    const bool image_transfer = HasBoolFeature(device->getDeviceConfig(), "ImageTransferProtocol");
    const int timeout_ms = request.timeout_ms > 0 ? request.timeout_ms : kDefaultCaptureTimeoutMs;
    std::shared_ptr<PendingCapture> pending(new PendingCapture());
    pending->request = request;
    pending->callback = callback;
    pending->image_transfer_protocol = image_transfer;
    pending->capture_token = next_capture_token_.fetch_add(1);
    pending->timeout_ms = timeout_ms;
    pending->armed_at_ms = NowMs();
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        pending_captures_[request.device_id] = pending;
    }
    SDK_PRIVATE_LOG_INFO("device.capture armed device={} capture_token={} source=armed image_transfer={} timeout_ms={} elapsed_ms=0",
                         SafeDeviceId(request.device_id),
                         pending->capture_token,
                         image_transfer,
                         timeout_ms);
    // pending 已对 cleanup 可见；设备触发和异步 callback 不应长期占用存储生命周期锁。
    storage_lifecycle_lock.unlock();

    const std::string capture_device_id = request.device_id;
    std::thread([this, capture_device_id, pending, timeout_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        SdkCaptureResult timeout_result;
        timeout_result.code = ToCode(SdkStatusCode::CaptureTimeout);
        timeout_result.message = "capture timeout";
        CompletePendingCapture(capture_device_id, pending, timeout_result, "timeout");
    }).detach();

    if (image_transfer) {
        editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
        if (manager == NULL) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = "provider not ready";
            CompletePendingCapture(request.device_id, pending, result, "provider_not_ready");
            return;
        }
        manager->setDeviceStillImageCallback(device, [this, capture_device_id](const editor::devices::StillImagePair& image_pair) {
            HandleStillImageReceived(capture_device_id, image_pair);
        });

        editor::devices::DeviceCommand device_command(device, manager);
        if (!device_command.takePhoto()) {
            result.code = ToCode(SdkStatusCode::CaptureFailed);
            result.message = "failed to notify device to take photo";
            SDK_PRIVATE_LOG_ERROR("device.capture trigger_failed device={} code={} message={}",
                                  SafeDeviceId(request.device_id),
                                  result.code,
                                  result.message);
            CompletePendingCapture(request.device_id, pending, result, "trigger_failure");
        }
        return;
    }

    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
    if (manager == NULL) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        CompletePendingCapture(request.device_id, pending, result, "provider_not_ready");
        return;
    }
    manager->requestRawCapture(device);

    result.code = ToCode(SdkStatusCode::Ok);
    result.message = "waiting for next video frame";
}

SdkVideoStartResult PrivateDeviceProvider::StartVideo(const SdkVideoStartRequest& request, SdkVideoFrameCallback callback) {
    SDK_PRIVATE_LOG_INFO("device.video_start begin device={} stream_present={} requested={}x{} fps={} format={}",
                         SafeDeviceId(request.device_id),
                         !request.stream_id.empty(),
                         request.width,
                         request.height,
                         request.fps,
                         request.pixel_format);
    SdkVideoStartResult result;
    if (!IsSupportedVideoPixelFormat(request.pixel_format)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "unsupported pixel_format";
        return result;
    }
    if (!EnsureDeviceManagerReady()) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        SDK_PRIVATE_LOG_ERROR("device.video_start failed device={} code={} message={}",
                              SafeDeviceId(request.device_id),
                              result.code,
                              result.message);
        return result;
    }

    const std::shared_ptr<editor::devices::Device> device = LookupDevice(request.device_id);
    if (!device) {
        result.code = ToCode(SdkStatusCode::DeviceNotFound);
        result.message = "device not found";
        SDK_PRIVATE_LOG_WARN("device.video_start failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        return result;
    }
    if (!device->isOpened()) {
        result.code = ToCode(SdkStatusCode::DeviceNotOpen);
        result.message = "device not open";
        SDK_PRIVATE_LOG_WARN("device.video_start failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        return result;
    }
    if (request.stream_id.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "stream_id required";
        SDK_PRIVATE_LOG_WARN("device.video_start failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        return result;
    }

    editor::devices::Resolution selected = editor::devices::Resolution();
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        const std::map<std::string, editor::devices::Resolution>::const_iterator it =
            selected_resolutions_.find(request.device_id);
        if (it != selected_resolutions_.end()) {
            selected = it->second;
        }
    }
    if (selected.width <= 0 || (request.width > 0 && (selected.width != request.width || selected.height != request.height))) {
        selected = PickResolution(device, request.width, request.height, request.fps);
    }
    if (selected.width <= 0 || (request.width > 0 && (selected.width != request.width || selected.height != request.height))) {
        result.code = ToCode(SdkStatusCode::PreviewResolutionUnsupported);
        result.message = "preview resolution unsupported";
        SDK_PRIVATE_LOG_WARN("device.video_start failed device={} code={} requested={}x{}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             request.width,
                             request.height);
        return result;
    }

    // 48M 拍照档位由逻辑导出尺寸决定，不能使用独立的视频预览尺寸。
    int export_width = selected.width;
    int export_height = selected.height;
    if (HasBoolFeature(device->getDeviceConfig(), "Use48MFirmware")) {
        // SDK 暂无用户扫描尺寸配置，沿用采编王首次启动时的设备默认扫描档位。
        export_width = 0;
        export_height = 0;
        const std::vector<editor::devices::ScanResolution> scan_resolutions = device->getScanResolutions();
        for (std::vector<editor::devices::ScanResolution>::const_iterator it = scan_resolutions.begin();
             it != scan_resolutions.end(); ++it) {
            if (it->isDefault && it->width > 0 && it->height > 0) {
                export_width = it->width;
                export_height = it->height;
                break;
            }
        }
        if (export_width <= 0 || export_height <= 0) {
            result.code = ToCode(SdkStatusCode::DeviceModelUnsupported);
            result.message = "default scan resolution unavailable for 48M device";
            SDK_PRIVATE_LOG_WARN("device.video_start failed device={} code={} message={}",
                                 SafeDeviceId(request.device_id), result.code, result.message);
            return result;
        }
    }

    std::shared_ptr<std::atomic<uint64_t> > frame_seq(new std::atomic<uint64_t>(1));
    const bool realtime_detect_rects =
        request.page_processing == "single_page" && request.single_page_realtime_detect_rects;
    const bool multi_target_paging = realtime_detect_rects && request.single_page_multi_target_paging;
    const int scan_device_type = ResolveScanDeviceType(device);
    const std::string output_pixel_format = NormalizeVideoPixelFormat(request.pixel_format);
    SDK_PRIVATE_LOG_INFO(
        "device.video_start selected device={} logical={}x{} native={}x{} fps={} format={} output={}",
        SafeDeviceId(request.device_id),
        selected.width,
        selected.height,
        selected.realWidth > 0 ? selected.realWidth : selected.width,
        selected.realHeight > 0 ? selected.realHeight : selected.height,
        selected.fps,
        static_cast<int>(selected.format),
        output_pixel_format);
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        if (active_stream_sequences_.find(request.device_id) != active_stream_sequences_.end()) {
            result.code = ToCode(SdkStatusCode::DeviceBusy);
            result.message = "device already streaming";
            SDK_PRIVATE_LOG_WARN("device.video_start failed device={} code={} message={}",
                                 SafeDeviceId(request.device_id),
                                 result.code,
                                 result.message);
            return result;
        }
        active_stream_sequences_[request.device_id] = frame_seq;
        realtime_detect_rects_[request.device_id] = realtime_detect_rects;
        multi_target_paging_[request.device_id] = multi_target_paging;
        detected_rect_caches_.erase(request.device_id);
        std::map<std::string, TurnDetectState>::iterator turn_it = turn_detect_states_.find(request.device_id);
        if (turn_it != turn_detect_states_.end()) {
            turn_it->second.initialized = false;
        }
    }

    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
#if defined(OS_OHOS)
    // The OHOS USB backend deliberately exposes complete native MJPEG frames.
    // Decode only inside the provider when an SDK feature explicitly needs BGR.
    const bool force_native_mjpeg = true;
#else
    const bool force_native_mjpeg = false;
#endif
    const editor::devices::DeviceStreamOptions stream_options =
        device_frame_codec::BuildStreamOptions(output_pixel_format, force_native_mjpeg);

    // 底层 startStreaming 会根据导出目标映射 48M 固件拍照档位（含裁切方案）。
    device->setExportResolution(export_width, export_height);
    SDK_PRIVATE_LOG_INFO("device.video_start export device={} dimensions={}x{}",
                         SafeDeviceId(request.device_id), export_width, export_height);

    const bool started = manager != NULL && manager->startStreaming(
        device,
        selected.realWidth > 0 ? selected.realWidth : selected.width,
        selected.realHeight > 0 ? selected.realHeight : selected.height,
        selected.fps > 0 ? selected.fps : 15,
        selected.format,
        [this, request, selected, callback, frame_seq, scan_device_type, output_pixel_format, device](editor::devices::GenericFrame* frame) {
            if (frame == NULL || (frame->data.empty() && frame->raw_mjpeg.empty() && frame->native_mjpeg.empty())) {
                return;
            }

            const bool has_native_mjpeg = !frame->native_mjpeg.empty();
            const bool realtime_detect_rects = RealtimeDetectRectsEnabled(request.device_id);
            bool turn_detect_enabled = false;
            if (output_pixel_format != "bgr24" && !realtime_detect_rects) {
                std::lock_guard<std::mutex> lock(state_mu_);
                const std::map<std::string, TurnDetectState>::const_iterator it =
                    turn_detect_states_.find(request.device_id);
                turn_detect_enabled = it != turn_detect_states_.end() && it->second.enabled;
            }
            const bool needs_bgr_processing = device_frame_codec::NeedsBgrFrame(
                output_pixel_format, realtime_detect_rects, turn_detect_enabled);
            size_t expected_size =
                static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height) * 3;
            if (frame->data.size() < expected_size &&
                has_native_mjpeg && needs_bgr_processing && device) {
#if defined(OS_OHOS)
                if (!device_frame_codec::DecodeNativeMjpegFrame(frame)) {
                    SDK_PRIVATE_LOG_WARN(
                        "device.video_frame on_demand_decode_failed device={} dimensions={}x{} mjpeg_size={}",
                        SafeDeviceId(request.device_id),
                        frame->width,
                        frame->height,
                        static_cast<uint64_t>(frame->native_mjpeg.size()));
                }
#else
#if defined(_WIN32)
                editor::devices::RawFrameBuffer mjpeg_frame;
                mjpeg_frame.data = frame->native_mjpeg.data();
                mjpeg_frame.size = static_cast<long>(frame->native_mjpeg.size());
                mjpeg_frame.width = frame->width;
                mjpeg_frame.height = frame->height;
#else
                uvc_frame_t mjpeg_frame;
                std::memset(&mjpeg_frame, 0, sizeof(mjpeg_frame));
                mjpeg_frame.data = const_cast<uint8_t*>(frame->native_mjpeg.data());
                mjpeg_frame.data_bytes = frame->native_mjpeg.size();
                mjpeg_frame.width = frame->width;
                mjpeg_frame.height = frame->height;
                mjpeg_frame.frame_format = UVC_FRAME_FORMAT_MJPEG;
#endif
                if (!device->decodeMJPEGFrame(&mjpeg_frame, frame->data)) {
                    SDK_PRIVATE_LOG_WARN(
                        "device.video_frame on_demand_decode_failed device={} dimensions={}x{} mjpeg_size={}",
                        SafeDeviceId(request.device_id),
                        frame->width,
                        frame->height,
                        static_cast<uint64_t>(frame->native_mjpeg.size()));
                }
#endif
            }
            expected_size =
                static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height) * 3;
            const bool has_valid_bgr =
                frame->width > 0 && frame->height > 0 && frame->data.size() >= expected_size;
            std::vector<SdkRect4P> detected_rects;
            if (realtime_detect_rects) {
                if (has_valid_bgr) {
                    detected_rects = DetectSinglePageRects(request.device_id,
                                                           frame,
                                                           scan_device_type,
                                                           MultiTargetPagingEnabled(request.device_id),
                                                           frame_seq);
                } else {
                    UpdateDetectedRectCache(request.device_id,
                                            frame_seq,
                                            detected_rects,
                                            frame->width,
                                            frame->height,
                                            scan_device_type);
                }
            }
            TryCompleteFrameCapture(request.device_id, frame);
            TryCompleteHardGrabFrameCapture(request.device_id, frame);
            if (!callback) {
                return;
            }

            if (!has_valid_bgr && (output_pixel_format == "bgr24" || !has_native_mjpeg)) {
                SDK_PRIVATE_LOG_WARN("device.video_frame missing_requested_payload device={} pixel_format={} dimensions={}x{} bgr_size={} mjpeg_size={}",
                                     SafeDeviceId(request.device_id),
                                     output_pixel_format,
                                     frame->width,
                                     frame->height,
                                     static_cast<uint64_t>(frame->data.size()),
                                     static_cast<uint64_t>(frame->native_mjpeg.size()));
                return;
            }
            if (has_valid_bgr) {
                DetectPageTurn(request.device_id, frame, scan_device_type);
            }

            SdkVideoFrame sdk_frame;
            sdk_frame.device_id = request.device_id;
            sdk_frame.stream_id = request.stream_id;
            sdk_frame.frame_seq = frame_seq->fetch_add(1);
            sdk_frame.timestamp_ms = NowMs();
            sdk_frame.width = frame->width;
            sdk_frame.height = frame->height;
            sdk_frame.pixel_format = output_pixel_format;
            if (output_pixel_format == "bgr24") {
                sdk_frame.payload.assign(frame->data.begin(), frame->data.begin() + expected_size);
            } else if (!frame->native_mjpeg.empty()) {
                sdk_frame.payload = frame->native_mjpeg;
            } else if (!EncodeBgrFrameToJpeg(*frame, &sdk_frame.payload)) {
                SDK_PRIVATE_LOG_WARN("device.video_frame jpeg_encode_failed device={} dimensions={}x{} data_size={}",
                                     SafeDeviceId(request.device_id),
                                     frame->width,
                                     frame->height,
                                     static_cast<uint64_t>(frame->data.size()));
                return;
            }
            sdk_frame.detected_rects = detected_rects;
            sdk_frame.detected_rects_source_width = frame->width;
            sdk_frame.detected_rects_source_height = frame->height;
            callback(sdk_frame);
        },
        stream_options);

    if (!started) {
        std::lock_guard<std::mutex> lock(state_mu_);
        active_stream_sequences_.erase(request.device_id);
        realtime_detect_rects_.erase(request.device_id);
        multi_target_paging_.erase(request.device_id);
        detected_rect_caches_.erase(request.device_id);
        ResetTurnDetectSession(request.device_id);
        result.code = ToCode(SdkStatusCode::StreamNotReady);
        result.message = "failed to start video";
        SDK_PRIVATE_LOG_ERROR("device.video_start failed device={} code={} logical={}x{} native={}x{} fps={}",
                              SafeDeviceId(request.device_id),
                              result.code,
                              selected.width,
                              selected.height,
                              selected.realWidth > 0 ? selected.realWidth : selected.width,
                              selected.realHeight > 0 ? selected.realHeight : selected.height,
                              selected.fps);
        return result;
    }

    const std::string still_image_device_id = request.device_id;
    manager->setDeviceStillImageCallback(device, [this, still_image_device_id](const editor::devices::StillImagePair& image_pair) {
        HandleStillImageReceived(still_image_device_id, image_pair);
    });

    const bool still_image_hard_detect = HasBoolFeature(device->getDeviceConfig(), "ImageTransferProtocol");
    const std::string hard_detect_device_id = request.device_id;
    const bool hard_detect_registered = manager != NULL && manager->setHardDetect(device, [this, hard_detect_device_id, still_image_hard_detect]() {
        if (still_image_hard_detect) {
            SDK_PRIVATE_LOG_INFO("device.action_event hardgrab_frame_capture_skipped device={} reason=still_image_capture",
                                 SafeDeviceId(hard_detect_device_id));
            return;
        }
        {
            std::lock_guard<std::mutex> lock(state_mu_);
            PendingHardGrabCapture& pending = pending_hardgrab_captures_[hard_detect_device_id];
            pending.timestamp_ms = NowMs();
        }
        SDK_PRIVATE_LOG_INFO("device.action_event hardgrab_frame_capture_armed device={}",
                             SafeDeviceId(hard_detect_device_id));
    });
    if (hard_detect_registered) {
        SDK_PRIVATE_LOG_INFO("device.action_event hard_detect_registered device={}", SafeDeviceId(request.device_id));
    } else if (still_image_hard_detect) {
        SDK_PRIVATE_LOG_INFO("device.action_event hard_detect_unavailable device={} still_image_fallback=true",
                             SafeDeviceId(request.device_id));
    } else {
        SDK_PRIVATE_LOG_WARN("device.action_event hard_detect_unavailable device={}", SafeDeviceId(request.device_id));
    }

    result.accepted = true;
    result.pixel_format = output_pixel_format;
    result.width = selected.realWidth > 0 ? selected.realWidth : selected.width;
    result.height = selected.realHeight > 0 ? selected.realHeight : selected.height;
    result.fps = selected.fps > 0 ? selected.fps : 15;
    SDK_PRIVATE_LOG_INFO("device.video_start end device={} code={} accepted={} selected={}x{} fps={}",
                         SafeDeviceId(request.device_id),
                         result.code,
                         result.accepted,
                         result.width,
                         result.height,
                         result.fps);
    return result;
}

SdkVideoStopResult PrivateDeviceProvider::StopVideo(const SdkVideoStopRequest& request) {
    SDK_PRIVATE_LOG_INFO("device.video_stop begin device={}", SafeDeviceId(request.device_id));
    SdkVideoStopResult result;
    const std::shared_ptr<editor::devices::Device> device = LookupDevice(request.device_id);
    if (!device) {
        if (IsDeviceRecentlyRemoved(request.device_id)) {
            SdkCaptureResult capture_result;
            capture_result.code = ToCode(SdkStatusCode::StreamNotReady);
            capture_result.message = "device removed during capture";
            CompletePendingCapture(request.device_id, std::shared_ptr<PendingCapture>(), capture_result, "device_close");
            DisarmStreamState(request.device_id, true);
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                selected_resolutions_.erase(request.device_id);
                still_image_action_suppressed_until_ms_.erase(request.device_id);
            }
            result.stopped = true;
            result.message = "device already removed";
            SDK_PRIVATE_LOG_INFO("device.video_stop end device={} code={} stopped={} reason=device_removed",
                                 SafeDeviceId(request.device_id),
                                 result.code,
                                 result.stopped);
            return result;
        }
        result.code = ToCode(SdkStatusCode::DeviceNotFound);
        result.message = "device not found";
        SDK_PRIVATE_LOG_WARN("device.video_stop failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        return result;
    }
    SdkCaptureResult capture_result;
    capture_result.code = ToCode(SdkStatusCode::StreamNotReady);
    capture_result.message = "video stream stopped during capture";
    CompletePendingCapture(request.device_id, std::shared_ptr<PendingCapture>(), capture_result, "video_stop");
    DisarmStreamState(request.device_id, false);
    if (device->isStreaming()) {
        editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
        if (manager != NULL) {
            manager->stopStreaming(device);
        }
    }
    result.stopped = true;
    SDK_PRIVATE_LOG_INFO("device.video_stop end device={} code={} stopped={}",
                         SafeDeviceId(request.device_id),
                         result.code,
                         result.stopped);
    return result;
}

bool PrivateDeviceProvider::CompletePendingCapture(const std::string& device_id,
                                                   const std::shared_ptr<PendingCapture>& expected_pending,
                                                   const SdkCaptureResult& result,
                                                   const char* completion_source) {
    SdkCaptureCallback callback;
    bool suppress_still_image_action = false;
    uint64_t capture_token = 0;
    int timeout_ms = 0;
    int64_t armed_at_ms = 0;
    const char* source = completion_source != NULL ? completion_source : "unknown";
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        const std::map<std::string, std::shared_ptr<PendingCapture> >::iterator it = pending_captures_.find(device_id);
        const std::shared_ptr<PendingCapture> current_pending =
            it == pending_captures_.end() ? std::shared_ptr<PendingCapture>() : it->second;
        const PendingCaptureMatch match = MatchPendingCapture(expected_pending, current_pending);
        if (match == PendingCaptureMatch::Missing) {
            if (expected_pending) {
                if (std::strcmp(source, "timeout") == 0) {
                    SDK_PRIVATE_LOG_INFO("device.capture timeout_ignored_stale device={} capture_token={} source={} reason=pending_missing",
                                         SafeDeviceId(device_id),
                                         expected_pending->capture_token,
                                         source);
                } else {
                    SDK_PRIVATE_LOG_INFO("device.capture completion_ignored device={} capture_token={} source={} reason=pending_missing",
                                         SafeDeviceId(device_id),
                                         expected_pending->capture_token,
                                         source);
                }
            }
            return false;
        }
        if (match == PendingCaptureMatch::Replaced) {
            if (std::strcmp(source, "timeout") == 0) {
                SDK_PRIVATE_LOG_INFO("device.capture timeout_ignored_stale device={} capture_token={} current_capture_token={} source={} reason=pending_replaced",
                                     SafeDeviceId(device_id),
                                     expected_pending->capture_token,
                                     current_pending->capture_token,
                                     source);
            } else {
                SDK_PRIVATE_LOG_INFO("device.capture completion_ignored device={} capture_token={} current_capture_token={} source={} reason=pending_replaced",
                                     SafeDeviceId(device_id),
                                     expected_pending->capture_token,
                                     current_pending->capture_token,
                                     source);
            }
            return false;
        }
        callback = current_pending->callback;
        suppress_still_image_action = current_pending->image_transfer_protocol && !result.captured;
        capture_token = current_pending->capture_token;
        timeout_ms = current_pending->timeout_ms;
        armed_at_ms = current_pending->armed_at_ms;
        pending_captures_.erase(it);
    }
    if (suppress_still_image_action) {
        SuppressStillImageActionEvent(device_id, NowMs() + kStillImageActionSuppressAfterCaptureMs);
    }
    if (callback) {
        const int64_t elapsed_ms = armed_at_ms > 0 ? std::max<int64_t>(0, NowMs() - armed_at_ms) : 0;
        SDK_PRIVATE_LOG_INFO("device.capture complete device={} capture_token={} source={} timeout_ms={} elapsed_ms={} code={} captured={} original={} laser={} size={} dimensions={}x{}",
                             SafeDeviceId(device_id),
                             capture_token,
                             source,
                             timeout_ms,
                             elapsed_ms,
                             result.code,
                             result.captured,
                             SafePathSummary(result.original_path),
                             SafePathSummary(result.laser_path),
                             result.size,
                             result.width,
                             result.height);
        callback(result);
    }
    return true;
}

bool PrivateDeviceProvider::TryCompleteFrameCapture(const std::string& device_id, editor::devices::GenericFrame* frame) {
    // Provider 将采集 JPEG 规范为设备默认导出尺寸后保存到任务 raw 目录；纸张、
    // 色彩和后置处理仍由 sdk_open 的设备级 FIFO 执行。
    std::unique_lock<std::mutex> storage_lifecycle_lock(capture_storage_lifecycle_mu_);
    std::shared_ptr<PendingCapture> pending;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        const std::map<std::string, std::shared_ptr<PendingCapture> >::const_iterator it = pending_captures_.find(device_id);
        if (it == pending_captures_.end() || it->second->image_transfer_protocol) {
            return false;
        }
        pending = it->second;
    }

    if (frame == NULL || frame->raw_mjpeg.empty()) {
        return false;
    }

    const std::vector<uint8_t>& jpeg_payload = frame->raw_mjpeg;
    SdkCaptureResult result;
    int jpeg_width = 0;
    int jpeg_height = 0;
    if (!ParseJpegSize(jpeg_payload.data(), jpeg_payload.size(), &jpeg_width, &jpeg_height)) {
        result.code = ToCode(SdkStatusCode::CaptureFailed);
        result.message = "video frame is not jpeg";
        storage_lifecycle_lock.unlock();
        CompletePendingCapture(device_id, pending, result, "raw_frame");
        return true;
    }

    const std::shared_ptr<editor::devices::Device> device = LookupDevice(device_id);
    int target_width = 0;
    int target_height = 0;
    const bool need_resize = ResolveCaptureExportResolution(
        device, jpeg_width, jpeg_height, &target_width, &target_height);
    const int final_width = need_resize ? target_width : jpeg_width;
    const int final_height = need_resize ? target_height : jpeg_height;
    const int actual_dpi = ResolveCaptureActualDpi(device);
    const std::string output_dir = pending->request.output_dir.empty() ? "." : pending->request.output_dir;
    const std::string output_path = JoinPath(output_dir, "original.jpg");
    if (!WriteCaptureJpegOutput(output_path,
                                jpeg_payload,
                                jpeg_width,
                                jpeg_height,
                                final_width,
                                final_height,
                                actual_dpi)) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = need_resize ? "failed to interpolate capture frame" : "failed to write capture frame";
        storage_lifecycle_lock.unlock();
        CompletePendingCapture(device_id, pending, result, "raw_frame");
        return true;
    }
    result.captured = true;
    result.content_type = "image/jpeg";
    result.output_path = output_path;
    result.original_path = output_path;
    result.width = final_width;
    result.height = final_height;
    result.dpi = actual_dpi;
    result.size = FileSize(output_path);
    const DetectedRectCache rect_cache = GetDetectedRectCache(device_id);
    result.detected_rects = rect_cache.rects;
    result.detected_rects_source_width = rect_cache.source_width;
    result.detected_rects_source_height = rect_cache.source_height;
    result.scan_device_type = rect_cache.scan_device_type;
    SDK_PRIVATE_LOG_INFO("device.capture frame_saved device={} dimensions={}x{} dpi={} bytes={} output={} queued_processing=true",
                         SafeDeviceId(device_id),
                         final_width,
                         final_height,
                         actual_dpi,
                         result.size,
                         SafePathSummary(output_path));
    storage_lifecycle_lock.unlock();
    CompletePendingCapture(device_id, pending, result, "raw_frame");
    return true;
}

// 构造硬拍自动采集结果。Provider 先保存原图，sdk_open 只通过路径接管后续任务。
SdkCaptureResult PrivateDeviceProvider::BuildHardGrabJpegCapture(const std::string& device_id,
                                                                 const std::vector<uint8_t>& jpeg_payload,
                                                                 const std::vector<uint8_t>* laser_payload) {
    SdkCaptureResult result;
    result.content_type = "image/jpeg";
    if (jpeg_payload.empty()) {
        result.code = ToCode(SdkStatusCode::CaptureFailed);
        result.message = "empty hard grab image";
        return result;
    }

    int source_width = 0;
    int source_height = 0;
    if (!ParseJpegSize(jpeg_payload.data(), jpeg_payload.size(), &source_width, &source_height)) {
        result.code = ToCode(SdkStatusCode::CaptureFailed);
        result.message = "hard grab image is not jpeg";
        return result;
    }

    const std::shared_ptr<editor::devices::Device> device = LookupDevice(device_id);
    int target_width = 0;
    int target_height = 0;
    const bool need_resize = ResolveCaptureExportResolution(
        device, source_width, source_height, &target_width, &target_height);
    const int final_width = need_resize ? target_width : source_width;
    const int final_height = need_resize ? target_height : source_height;
    const int actual_dpi = ResolveCaptureActualDpi(device);
    const uint64_t capture_token = next_capture_token_.fetch_add(1);
    const int64_t timestamp_ms = NowMs();
    const std::string output_dir = HardGrabDirectory();
    if (!EnsureDirectoryRecursive(output_dir)) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "failed to create hard grab output directory";
        return result;
    }
    const std::string output_path = HardGrabPath(capture_token, timestamp_ms, "original.jpg");
    if (output_path.empty()) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "failed to create hard grab output directory";
        return result;
    }
    if (!WriteCaptureJpegOutput(output_path,
                                jpeg_payload,
                                source_width,
                                source_height,
                                final_width,
                                final_height,
                                actual_dpi)) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = need_resize ? "failed to interpolate hard grab image" : "failed to write hard grab image";
        return result;
    }

    std::string laser_path;
    if (laser_payload != NULL && !laser_payload->empty()) {
        laser_path = HardGrabPath(capture_token, timestamp_ms, "laser.jpg");
        if (!WriteBytes(laser_path, *laser_payload)) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to write hard grab laser image";
            return result;
        }
    }

    result.captured = true;
    result.output_path = output_path;
    result.original_path = output_path;
    result.laser_path = laser_path;
    result.width = final_width;
    result.height = final_height;
    result.dpi = actual_dpi;
    result.size = FileSize(output_path);
    const DetectedRectCache rect_cache = GetDetectedRectCache(device_id);
    result.detected_rects = rect_cache.rects;
    result.detected_rects_source_width = rect_cache.source_width;
    result.detected_rects_source_height = rect_cache.source_height;
    result.scan_device_type = rect_cache.scan_device_type;
    SDK_PRIVATE_LOG_INFO("device.action_event hardgrab_capture_ready device={} laser={} dimensions={}x{} size={} output={}",
                         SafeDeviceId(device_id),
                         !laser_path.empty(),
                         result.width,
                         result.height,
                         result.size,
                         SafePathSummary(output_path));
    return result;
}

// 非 still-image 设备的硬拍回调只表示按键事件，真正图像来自随后到达的预览帧。
// 取到一帧后立即清除 pending，保证单次硬拍动作只提交一次自动采集事件。
bool PrivateDeviceProvider::TryCompleteHardGrabFrameCapture(const std::string& device_id, editor::devices::GenericFrame* frame) {
    PendingHardGrabCapture pending;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        const std::map<std::string, PendingHardGrabCapture>::iterator it = pending_hardgrab_captures_.find(device_id);
        if (it == pending_hardgrab_captures_.end()) {
            return false;
        }
        pending = it->second;
        pending_hardgrab_captures_.erase(it);
    }

    SdkDeviceActionEvent event;
    event.device_id = device_id;
    event.type = SdkDeviceActionType::HardGrab;
    event.auto_capture = true;
    event.timestamp_ms = pending.timestamp_ms > 0 ? pending.timestamp_ms : NowMs();

    if (frame == NULL) {
        event.capture.code = ToCode(SdkStatusCode::CaptureFailed);
        event.capture.message = "missing hard grab frame";
    } else {
        std::vector<uint8_t> jpeg_payload;
        if (!frame->raw_mjpeg.empty()) {
            jpeg_payload = frame->raw_mjpeg;
        } else if (!frame->native_mjpeg.empty()) {
            jpeg_payload = frame->native_mjpeg;
        } else if (!EncodeBgrFrameToJpeg(*frame, &jpeg_payload)) {
            event.capture.code = ToCode(SdkStatusCode::CaptureFailed);
            event.capture.message = "failed to encode hard grab frame";
        }
        if (!jpeg_payload.empty()) {
            event.capture = BuildHardGrabJpegCapture(device_id, jpeg_payload, NULL);
        }
    }

    PublishDeviceActionEvent(event);
    return true;
}

bool PrivateDeviceProvider::CompleteStillImageCapture(const std::string& device_id,
                                                      const editor::devices::StillImagePair& image_pair) {
    // still-image 回调拿到 JPEG 后，将其规范为设备默认导出尺寸并落盘；纸张、色彩
    // 和后置处理由 sdk_open capture task 的 FIFO worker 完成。
    std::unique_lock<std::mutex> storage_lifecycle_lock(capture_storage_lifecycle_mu_);
    std::shared_ptr<PendingCapture> pending;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        const std::map<std::string, std::shared_ptr<PendingCapture> >::const_iterator it = pending_captures_.find(device_id);
        if (it == pending_captures_.end() || !it->second->image_transfer_protocol) {
            return false;
        }
        pending = it->second;
    }

    SdkCaptureResult result;
    if (image_pair.first.empty() && image_pair.originalPath.empty()) {
        result.code = ToCode(SdkStatusCode::CaptureFailed);
        result.message = "empty still image";
        storage_lifecycle_lock.unlock();
        CompletePendingCapture(device_id, pending, result, "still_image");
        return true;
    }

    StillImagePayloadSelection selection;
    SelectStillImagePayloads(image_pair, &selection);
    const std::vector<uint8_t>* original = selection.original;
    const std::vector<uint8_t>* laser = selection.laser;

    if (original == NULL || original->empty()) {
        result.code = ToCode(SdkStatusCode::CaptureFailed);
        result.message = "empty still image payload";
        storage_lifecycle_lock.unlock();
        CompletePendingCapture(device_id, pending, result, "still_image");
        return true;
    }

    int source_width = 0;
    int source_height = 0;
    if (!ParseJpegSize(original->data(), original->size(), &source_width, &source_height)) {
        result.code = ToCode(SdkStatusCode::CaptureFailed);
        result.message = "still image is not jpeg";
        storage_lifecycle_lock.unlock();
        CompletePendingCapture(device_id, pending, result, "still_image");
        return true;
    }

    const std::shared_ptr<editor::devices::Device> device = LookupDevice(device_id);
    int target_width = 0;
    int target_height = 0;
    const bool need_resize = ResolveCaptureExportResolution(
        device, source_width, source_height, &target_width, &target_height);
    const int final_width = need_resize ? target_width : source_width;
    const int final_height = need_resize ? target_height : source_height;
    const int actual_dpi = ResolveCaptureActualDpi(device);
    const std::string output_dir = pending->request.output_dir.empty() ? "." : pending->request.output_dir;
    const std::string original_path = JoinPath(output_dir, "original.jpg");
    if (!WriteCaptureJpegOutput(original_path,
                                *original,
                                source_width,
                                source_height,
                                final_width,
                                final_height,
                                actual_dpi)) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = need_resize ? "failed to interpolate still image" : "failed to write still image";
        storage_lifecycle_lock.unlock();
        CompletePendingCapture(device_id, pending, result, "still_image");
        return true;
    }
    std::string laser_path;
    if (laser != NULL && !laser->empty()) {
        laser_path = JoinPath(output_dir, "laser.jpg");
        if (!WriteBytes(laser_path, *laser)) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to write laser image";
            storage_lifecycle_lock.unlock();
            CompletePendingCapture(device_id, pending, result, "still_image");
            return true;
        }
    }
    result.captured = true;
    result.content_type = "image/jpeg";
    result.output_path = original_path;
    result.original_path = original_path;
    result.laser_path = laser_path;
    result.width = final_width;
    result.height = final_height;
    result.dpi = actual_dpi;
    result.size = FileSize(original_path);
    const DetectedRectCache rect_cache = GetDetectedRectCache(device_id);
    result.detected_rects = rect_cache.rects;
    result.detected_rects_source_width = rect_cache.source_width;
    result.detected_rects_source_height = rect_cache.source_height;
    result.scan_device_type = rect_cache.scan_device_type;
    SDK_PRIVATE_LOG_INFO("device.capture still_saved device={} dimensions={}x{} dpi={} bytes={} laser_bytes={} output={} queued_processing=true",
                         SafeDeviceId(device_id),
                         final_width,
                         final_height,
                         actual_dpi,
                         result.size,
                         static_cast<uint64_t>(laser == NULL ? 0 : laser->size()),
                         SafePathSummary(original_path));
    storage_lifecycle_lock.unlock();
    CompletePendingCapture(device_id, pending, result, "still_image");
    return true;
}

std::unique_lock<std::mutex> PrivateDeviceProvider::TryLockCaptureStorageLifecycle() const {
    return std::unique_lock<std::mutex>(capture_storage_lifecycle_mu_, std::try_to_lock);
}

std::size_t PrivateDeviceProvider::ActiveCaptureCount() const {
    std::lock_guard<std::mutex> lock(state_mu_);
    return pending_captures_.size();
}

void PrivateDeviceProvider::HandleStillImageReceived(const std::string& device_id,
                                                     const editor::devices::StillImagePair& image_pair) {
    if (CompleteStillImageCapture(device_id, image_pair)) {
        return;
    }
    const int64_t now_ms = NowMs();
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        if (active_stream_sequences_.find(device_id) == active_stream_sequences_.end()) {
            SDK_PRIVATE_LOG_INFO("device.action_event still_image_ignored device={} reason=stream_inactive",
                                 SafeDeviceId(device_id));
            return;
        }
    }
    if (ShouldSuppressStillImageActionEvent(device_id, now_ms)) {
        SDK_PRIVATE_LOG_INFO("device.action_event still_image_ignored device={} reason=capture_suppressed",
                             SafeDeviceId(device_id));
        return;
    }
    if (image_pair.first.empty() && image_pair.originalPath.empty()) {
        SDK_PRIVATE_LOG_WARN("device.action_event still_image_ignored device={} reason=empty_image",
                             SafeDeviceId(device_id));
        return;
    }

    StillImagePayloadSelection selection;
    SelectStillImagePayloads(image_pair, &selection);
    const std::vector<uint8_t>* original = selection.original;
    const std::vector<uint8_t>* laser = selection.laser;

    SdkDeviceActionEvent event;
    event.device_id = device_id;
    event.type = SdkDeviceActionType::HardGrab;
    event.timestamp_ms = now_ms;
    event.auto_capture = true;
    if (original != NULL && !original->empty()) {
        event.capture = BuildHardGrabJpegCapture(device_id, *original, laser);
    } else {
        event.capture.code = ToCode(SdkStatusCode::CaptureFailed);
        event.capture.message = "still image path capture is not supported";
    }
    SDK_PRIVATE_LOG_INFO("device.action_event still_image_hard_detect device={} pair={} original_bytes={} laser_bytes={} captured={} output={}",
                         SafeDeviceId(device_id),
                         image_pair.isPair,
                         static_cast<uint64_t>(image_pair.first.size()),
                         static_cast<uint64_t>(image_pair.second.size()),
                         event.capture.captured,
                         SafePathSummary(event.capture.original_path));
    PublishDeviceActionEvent(event);
}

std::vector<SdkRect4P> PrivateDeviceProvider::DetectSinglePageRects(const std::string& device_id,
                                                                    editor::devices::GenericFrame* frame,
                                                                    int scan_device_type,
                                                                    bool multi_target_paging,
                                                                    const std::shared_ptr<std::atomic<uint64_t> >& expected_stream) {
    std::vector<SdkRect4P> rects;
    if (frame == NULL || frame->width <= 0 || frame->height <= 0) {
        UpdateDetectedRectCache(device_id, expected_stream, rects, 0, 0, scan_device_type);
        return rects;
    }
    const size_t expected_size = static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height) * 3;
    if (frame->data.size() < expected_size) {
        UpdateDetectedRectCache(
            device_id, expected_stream, rects, frame->width, frame->height, scan_device_type);
        return rects;
    }

    SdkStatusCode init_code = SdkStatusCode::Ok;
    std::string init_message;
    if (!graphic::GraphicAlgorithmRuntime::Instance().EnsureInitialized(init_code, init_message)) {
        SDK_PRIVATE_LOG_WARN("device.video_detect init_failed device={} code={} message={}",
                             SafeDeviceId(device_id),
                             ToCode(init_code),
                             init_message);
        UpdateDetectedRectCache(
            device_id, expected_stream, rects, frame->width, frame->height, scan_device_type);
        return rects;
    }

    try {
        cv::Mat source(frame->height, frame->width, CV_8UC3, const_cast<uint8_t*>(&frame->data[0]));
        cv::Mat output;
        std::vector<cv::Point2f> points;
        bool single_page_detection_succeeded = false;
        bool single_page_full_frame = false;
        {
            std::lock_guard<std::mutex> algorithm_lock(graphic::GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
            if (!multi_target_paging) {
                try {
                    czcv::single_page_detect_cut(source, output, points);
                    graphic::DetachCzcvOutputMat(output);
                    graphic::RehomeCzcvStdVector(points);
                    if (points.size() == 4) {
                        single_page_full_frame = IsFullFrameRect(
                            ToSdkRect(points), frame->width, frame->height);
                        single_page_detection_succeeded = !single_page_full_frame;
                    }
                } catch (const std::exception& e) {
                    SDK_PRIVATE_LOG_WARN(
                        "device.video_detect single_page_detect_cut_failed device={} error={}",
                        SafeDeviceId(device_id),
                        e.what());
                } catch (...) {
                    SDK_PRIVATE_LOG_WARN(
                        "device.video_detect single_page_detect_cut_failed device={} error=unknown",
                        SafeDeviceId(device_id));
                }
            }

            if (multi_target_paging || !single_page_detection_succeeded || points.size() != 4) {
                if (!multi_target_paging && !points.empty() && points.size() != 4) {
                    SDK_PRIVATE_LOG_WARN(
                        "device.video_detect single_page_detect_cut_invalid_points device={} count={}",
                        SafeDeviceId(device_id),
                        static_cast<uint64_t>(points.size()));
                }
                czcv::CutParams params;
                params.srcW = frame->width;
                params.srcH = frame->height;
                params.dstW = frame->width;
                params.dstH = frame->height;
                params.outChannels = 3;
                params.deviceType = static_cast<czcv::ScanDeviceType>(scan_device_type);
                params.isMultiplePage = multi_target_paging;
                czcv::page_keypoints_detect(source, params);
                RehomeCzcvDetectedRects(params);
                for (std::vector<czcv::CZCV_Rect4P>::const_iterator it = params.detectedRects.begin();
                     it != params.detectedRects.end();
                     ++it) {
                    SdkRect4P rect;
                    rect.left_top.x = it->leftTopP.x;
                    rect.left_top.y = it->leftTopP.y;
                    rect.right_top.x = it->rightTopP.x;
                    rect.right_top.y = it->rightTopP.y;
                    rect.right_down.x = it->rightDownP.x;
                    rect.right_down.y = it->rightDownP.y;
                    rect.left_down.x = it->leftDownP.x;
                    rect.left_down.y = it->leftDownP.y;
                    rects.push_back(rect);
                }
            }
        }
        if (!multi_target_paging && single_page_detection_succeeded && points.size() == 4) {
            rects.push_back(ToSdkRect(points));
        }
        if (!multi_target_paging) {
            rects.erase(
                std::remove_if(rects.begin(), rects.end(), [frame](const SdkRect4P& rect) {
                    return IsFullFrameRect(rect, frame->width, frame->height);
                }),
                rects.end());
        }
        const uint64_t sequence = expected_stream ? expected_stream->load() : 0;
        if (sequence == 1 || sequence % 300 == 0) {
            SDK_PRIVATE_LOG_INFO(
                "device.video_detect result device={} dimensions={}x{} points={} full_frame={} keypoints_fallback={} rects={}",
                SafeDeviceId(device_id), frame->width, frame->height,
                static_cast<uint64_t>(points.size()), single_page_full_frame,
                multi_target_paging || !single_page_detection_succeeded,
                static_cast<uint64_t>(rects.size()));
        }
    } catch (const std::exception& e) {
        SDK_PRIVATE_LOG_WARN("device.video_detect failed device={} error={}", SafeDeviceId(device_id), e.what());
    } catch (...) {
        SDK_PRIVATE_LOG_WARN("device.video_detect failed device={} error=unknown", SafeDeviceId(device_id));
    }
    UpdateDetectedRectCache(
        device_id, expected_stream, rects, frame->width, frame->height, scan_device_type);
    return rects;
}

void PrivateDeviceProvider::UpdateDetectedRectCache(
    const std::string& device_id,
    const std::shared_ptr<std::atomic<uint64_t> >& expected_stream,
    const std::vector<SdkRect4P>& rects,
    int source_width,
    int source_height,
    int scan_device_type) {
    std::lock_guard<std::mutex> lock(state_mu_);
    const std::map<std::string, std::shared_ptr<std::atomic<uint64_t> > >::const_iterator stream_it =
        active_stream_sequences_.find(device_id);
    const std::map<std::string, bool>::const_iterator detection_it =
        realtime_detect_rects_.find(device_id);
    const std::shared_ptr<std::atomic<uint64_t> > active_stream =
        stream_it == active_stream_sequences_.end()
            ? std::shared_ptr<std::atomic<uint64_t> >()
            : stream_it->second;
    const bool realtime_detection_enabled =
        detection_it != realtime_detect_rects_.end() && detection_it->second;
    const RealtimeRectCacheMutation mutation = SelectRealtimeRectCacheMutation(
        active_stream, realtime_detection_enabled, expected_stream, !rects.empty());
    if (mutation == RealtimeRectCacheMutation::Ignore) {
        return;
    }
    if (mutation == RealtimeRectCacheMutation::Clear) {
        detected_rect_caches_.erase(device_id);
        return;
    }

    DetectedRectCache cache;
    cache.rects = rects;
    cache.source_width = source_width;
    cache.source_height = source_height;
    cache.scan_device_type = scan_device_type;
    detected_rect_caches_[device_id] = cache;
}

PrivateDeviceProvider::DetectedRectCache PrivateDeviceProvider::GetDetectedRectCache(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(state_mu_);
    const std::map<std::string, DetectedRectCache>::const_iterator it = detected_rect_caches_.find(device_id);
    if (it == detected_rect_caches_.end()) {
        return DetectedRectCache();
    }
    return it->second;
}

bool PrivateDeviceProvider::RealtimeDetectRectsEnabled(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(state_mu_);
    const std::map<std::string, bool>::const_iterator it = realtime_detect_rects_.find(device_id);
    return it != realtime_detect_rects_.end() && it->second;
}

bool PrivateDeviceProvider::MultiTargetPagingEnabled(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(state_mu_);
    const std::map<std::string, bool>::const_iterator it = multi_target_paging_.find(device_id);
    return it != multi_target_paging_.end() && it->second;
}

void PrivateDeviceProvider::ResetTurnDetectSession(const std::string& device_id) {
    std::map<std::string, TurnDetectState>::iterator it = turn_detect_states_.find(device_id);
    if (it != turn_detect_states_.end()) {
        it->second.initialized = false;
    }
}

void PrivateDeviceProvider::DisarmStreamState(const std::string& device_id, bool erase_turn_detect) {
    std::lock_guard<std::mutex> lock(state_mu_);
    active_stream_sequences_.erase(device_id);
    realtime_detect_rects_.erase(device_id);
    multi_target_paging_.erase(device_id);
    detected_rect_caches_.erase(device_id);
    pending_hardgrab_captures_.erase(device_id);
    if (erase_turn_detect) {
        turn_detect_states_.erase(device_id);
        return;
    }
    std::map<std::string, TurnDetectState>::iterator it = turn_detect_states_.find(device_id);
    if (it != turn_detect_states_.end()) {
        it->second.initialized = false;
    }
}

bool PrivateDeviceProvider::ShouldSuppressStillImageActionEvent(const std::string& device_id, int64_t now_ms) {
    std::lock_guard<std::mutex> lock(state_mu_);
    std::map<std::string, int64_t>::iterator it = still_image_action_suppressed_until_ms_.find(device_id);
    if (it == still_image_action_suppressed_until_ms_.end()) {
        return false;
    }
    if (now_ms <= it->second) {
        return true;
    }
    still_image_action_suppressed_until_ms_.erase(it);
    return false;
}

void PrivateDeviceProvider::SuppressStillImageActionEvent(const std::string& device_id, int64_t until_ms) {
    std::lock_guard<std::mutex> lock(state_mu_);
    still_image_action_suppressed_until_ms_[device_id] = until_ms;
}

void PrivateDeviceProvider::DetectPageTurn(const std::string& device_id,
                                           editor::devices::GenericFrame* frame,
                                           int scan_device_type) {
    TurnDetectState state;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        const std::map<std::string, TurnDetectState>::const_iterator it = turn_detect_states_.find(device_id);
        if (it == turn_detect_states_.end() || !it->second.enabled) {
            return;
        }
        state = it->second;
    }
    if (frame == NULL || frame->width <= 0 || frame->height <= 0) {
        return;
    }
    const size_t expected_size = static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height) * 3;
    if (frame->data.size() < expected_size) {
        return;
    }

    SdkStatusCode init_code = SdkStatusCode::Ok;
    std::string init_message;
    if (!graphic::GraphicAlgorithmRuntime::Instance().EnsureInitialized(init_code, init_message)) {
        SDK_PRIVATE_LOG_WARN("device.turn_detect init_failed device={} code={} message={}",
                             SafeDeviceId(device_id),
                             ToCode(init_code),
                             init_message);
        return;
    }

    const int effective_scan_device_type = scan_device_type;
    bool detected = false;
    try {
        cv::Mat source(frame->height, frame->width, CV_8UC3, const_cast<uint8_t*>(&frame->data[0]));
        {
            std::lock_guard<std::mutex> algorithm_lock(graphic::GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
            if (!state.initialized) {
                czcv::init_page_turning(source, static_cast<czcv::ScanDeviceType>(effective_scan_device_type));
            } else {
                detected = czcv::has_detect_page_turning(source);
            }
        }
    } catch (const std::exception& e) {
        SDK_PRIVATE_LOG_WARN("device.turn_detect failed device={} error={}", SafeDeviceId(device_id), e.what());
        return;
    } catch (...) {
        SDK_PRIVATE_LOG_WARN("device.turn_detect failed device={} error=unknown", SafeDeviceId(device_id));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mu_);
        std::map<std::string, TurnDetectState>::iterator it = turn_detect_states_.find(device_id);
        if (it == turn_detect_states_.end() || !it->second.enabled) {
            return;
        }
        if (!it->second.initialized) {
            it->second.initialized = true;
            SDK_PRIVATE_LOG_INFO("device.turn_detect initialized device={} scan_device_type={}",
                                 SafeDeviceId(device_id),
                                 effective_scan_device_type);
            return;
        }
    }
    if (!detected) {
        return;
    }

    SdkDeviceActionEvent event;
    event.device_id = device_id;
    event.type = SdkDeviceActionType::PageTurn;
    event.auto_capture = false;
    event.timestamp_ms = NowMs();
    SDK_PRIVATE_LOG_INFO("device.turn_detect detected device={} auto_capture={} cooldown_ms={}",
                         SafeDeviceId(device_id),
                         event.auto_capture,
                         state.cooldown_ms);
    PublishDeviceActionEvent(event);
}

void PrivateDeviceProvider::PublishDeviceActionEvent(const SdkDeviceActionEvent& event) const {
    SdkDeviceActionEventCallback sink;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        sink = device_action_event_sink_;
    }
    SDK_PRIVATE_LOG_INFO("device.action_event publish device={} type={} auto_capture={}",
                         SafeDeviceId(event.device_id),
                         event.type == SdkDeviceActionType::HardGrab ? "hardgrab" : "turn_detect",
                         event.auto_capture);
    if (sink) {
        sink(event);
    }
}

void PrivateDeviceProvider::HandleHotplugEvent(const std::shared_ptr<editor::devices::Device>& device, bool is_added) {
    if (!device) {
        return;
    }

    const std::string device_id = device->getDeviceId();
    if (device_id.empty()) {
        return;
    }

    if (is_added) {
        {
            std::lock_guard<std::mutex> lock(state_mu_);
            recently_removed_device_ids_.erase(device_id);
        }
        SdkDeviceEvent event;
        event.device_id = device_id;
        event.type = "added";
        event.reason = "hotplug_added";
        event.timestamp_ms = NowMs();
        SDK_PRIVATE_LOG_INFO("device.lifecycle added device={}", SafeDeviceId(device_id));
        PublishDeviceEvent(event);
        return;
    }

    const bool was_opened = device->isOpened();
    const bool was_streaming = device->isStreaming();
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        if (!recently_removed_device_ids_.insert(device_id).second) {
            SDK_PRIVATE_LOG_INFO("device.lifecycle removed ignored duplicate device={}", SafeDeviceId(device_id));
            return;
        }
    }

    SdkCaptureResult capture_result;
    capture_result.code = ToCode(SdkStatusCode::StreamNotReady);
    capture_result.message = "device removed during capture";
    CompletePendingCapture(device_id, std::shared_ptr<PendingCapture>(), capture_result, "hotplug");
    DisarmStreamState(device_id, true);
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        selected_resolutions_.erase(device_id);
        still_image_action_suppressed_until_ms_.erase(device_id);
    }

#if defined(__APPLE__)
    // The macOS inventory removes the device before delivering this callback,
    // but an opened backend session may still own the helper's system-wide camera
    // lease. Closing with the callback's retained Device instance tears down that
    // session and releases the lease, so a subsequently attached AVF device can
    // be opened without restarting OpenSDK. Keep other platforms on their existing
    // hotplug cleanup paths.
    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
    if (manager != NULL) {
        manager->closeDevice(device);
        SDK_PRIVATE_LOG_INFO("device.lifecycle backend_cleanup device={} was_opened={} was_streaming={}",
                             SafeDeviceId(device_id),
                             was_opened,
                             was_streaming);
    }
#endif

    SdkDeviceEvent event;
    event.device_id = device_id;
    event.type = "removed";
    event.reason = "hotplug_removed";
    event.was_opened = was_opened;
    event.was_streaming = was_streaming;
    event.timestamp_ms = NowMs();
    SDK_PRIVATE_LOG_INFO("device.lifecycle removed device={} was_opened={} was_streaming={}",
                         SafeDeviceId(device_id),
                         was_opened,
                         was_streaming);
    PublishDeviceEvent(event);
}

void PrivateDeviceProvider::PublishDeviceEvent(const SdkDeviceEvent& event) const {
    SdkDeviceEventCallback sink;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        sink = device_event_sink_;
    }
    SDK_PRIVATE_LOG_INFO("device.lifecycle publish device={} type={} reason={} was_opened={} was_streaming={}",
                         SafeDeviceId(event.device_id),
                         event.type,
                         event.reason,
                         event.was_opened,
                         event.was_streaming);
    if (sink) {
        sink(event);
    }
}

SdkTurnDetectResult PrivateDeviceProvider::SetTurnDetect(const SdkTurnDetectRequest& request) {
    SDK_PRIVATE_LOG_INFO("device.turn_detect set begin device={} enabled={} auto_capture={} scan_device_type={} cooldown_ms={}",
                         SafeDeviceId(request.device_id),
                         request.enabled,
                         request.auto_capture,
                         request.scan_device_type,
                         request.cooldown_ms);
    SdkTurnDetectResult result;
    const std::shared_ptr<editor::devices::Device> device = LookupDevice(request.device_id);
    if (!device) {
        if (!request.enabled) {
            std::lock_guard<std::mutex> lock(state_mu_);
            turn_detect_states_.erase(request.device_id);
            result.applied = true;
            result.enabled = false;
            result.auto_capture = false;
            result.scan_device_type = request.scan_device_type;
            result.cooldown_ms = request.cooldown_ms;
            SDK_PRIVATE_LOG_INFO("device.turn_detect set end device={} enabled=false auto_capture=false cooldown_ms={} reason=device_not_found_disable",
                                 SafeDeviceId(request.device_id),
                                 result.cooldown_ms);
            return result;
        }
        result.code = ToCode(SdkStatusCode::DeviceNotFound);
        result.message = "device not found";
        SDK_PRIVATE_LOG_WARN("device.turn_detect set failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(state_mu_);
        if (request.enabled) {
            TurnDetectState& state = turn_detect_states_[request.device_id];
            state.enabled = true;
            state.initialized = false;
            state.auto_capture = false;
            state.scan_device_type = request.scan_device_type;
            state.cooldown_ms = request.cooldown_ms;
        } else {
            turn_detect_states_.erase(request.device_id);
        }
    }

    result.applied = true;
    result.enabled = request.enabled;
    result.auto_capture = false;
    result.scan_device_type = request.scan_device_type;
    result.cooldown_ms = request.cooldown_ms;
    SDK_PRIVATE_LOG_INFO("device.turn_detect set end device={} enabled={} auto_capture={} cooldown_ms={}",
                         SafeDeviceId(request.device_id),
                         result.enabled,
                         result.auto_capture,
                         result.cooldown_ms);
    return result;
}

SdkVideoFormatResult PrivateDeviceProvider::SetVideoFormat(const SdkVideoFormatRequest& request) {
    SDK_PRIVATE_LOG_INFO("device.video_format begin device={} requested={}x{} fps={} format={}",
                         SafeDeviceId(request.device_id),
                         request.width,
                         request.height,
                         request.fps,
                         request.pixel_format);
    SdkVideoFormatResult result;
    if (!IsSupportedVideoPixelFormat(request.pixel_format)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "unsupported pixel_format";
        return result;
    }
    result.code = ToCode(SdkStatusCode::UnsupportedMethod);
    result.message = "video provider not implemented";
    SDK_PRIVATE_LOG_WARN("device.video_format unsupported device={} code={}",
                         SafeDeviceId(request.device_id),
                         result.code);
    return result;
}

SdkVideoProfileResult PrivateDeviceProvider::SetVideoProfile(const SdkVideoProfileRequest& request) {
    SDK_PRIVATE_LOG_INFO("device.video_profile begin device={} page_processing={} realtime_detect_rects={}",
                         SafeDeviceId(request.device_id),
                         request.page_processing,
                         request.single_page_realtime_detect_rects);
    SdkVideoProfileResult result;
    const std::shared_ptr<editor::devices::Device> device = LookupDevice(request.device_id);
    if (!device) {
        result.code = ToCode(SdkStatusCode::DeviceNotFound);
        result.message = "device not found";
        SDK_PRIVATE_LOG_WARN("device.video_profile failed device={} code={} message={}",
                             SafeDeviceId(request.device_id),
                             result.code,
                             result.message);
        return result;
    }

    const bool realtime_detect_rects =
        request.page_processing == "single_page" && request.single_page_realtime_detect_rects;
    const bool multi_target_paging = realtime_detect_rects && request.single_page_multi_target_paging;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        if (active_stream_sequences_.find(request.device_id) == active_stream_sequences_.end()) {
            result.code = ToCode(SdkStatusCode::StreamNotReady);
            result.message = "video stream not running";
            SDK_PRIVATE_LOG_WARN("device.video_profile failed device={} code={} message={}",
                                 SafeDeviceId(request.device_id),
                                 result.code,
                                 result.message);
            return result;
        }
        realtime_detect_rects_[request.device_id] = realtime_detect_rects;
        multi_target_paging_[request.device_id] = multi_target_paging;
        if (!realtime_detect_rects) {
            detected_rect_caches_.erase(request.device_id);
        }
    }

    result.applied = true;
    result.page_processing = request.page_processing;
    result.single_page_realtime_detect_rects = realtime_detect_rects;
    result.single_page_multi_target_paging = multi_target_paging;
    SDK_PRIVATE_LOG_INFO("device.video_profile end device={} code={} applied={} realtime_detect_rects={}",
                         SafeDeviceId(request.device_id),
                         result.code,
                         result.applied,
                         result.single_page_realtime_detect_rects);
    return result;
}

SdkDeviceOpenResult PrivateDeviceProvider::BuildDeviceResult(const std::string& device_id, bool include_resolutions) {
    SdkDeviceOpenResult result;
    if (!EnsureDeviceManagerReady()) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }

    const std::shared_ptr<editor::devices::Device> device = LookupDevice(device_id);
    if (!device) {
        result.code = ToCode(SdkStatusCode::DeviceNotFound);
        result.message = "device not found";
        return result;
    }

    result.opened = device->isOpened();
    result.device = BuildDeviceDescriptor(device);
    if (include_resolutions) {
        bool opened_for_query = false;
        const std::vector<editor::devices::Resolution> resolutions = LoadSupportedResolutions(device, &opened_for_query);
        for (std::vector<editor::devices::Resolution>::const_iterator it = resolutions.begin();
             it != resolutions.end();
             ++it) {
            result.device.resolutions.push_back(ToSdkResolution(*it));
        }
        result.opened = device->isOpened();
    }
    return result;
}

std::vector<editor::devices::Resolution> PrivateDeviceProvider::LoadSupportedResolutions(
    const std::shared_ptr<editor::devices::Device>& device,
    bool* opened_for_query) const {
    std::vector<editor::devices::Resolution> resolutions;
    if (opened_for_query != NULL) {
        *opened_for_query = false;
    }
    if (!device) {
        return resolutions;
    }

    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
    if (manager == NULL) {
        return resolutions;
    }

    const bool was_opened = device->isOpened();
    if (!was_opened) {
        if (!manager->openDevice(device)) {
            return resolutions;
        }
        if (opened_for_query != NULL) {
            *opened_for_query = true;
        }
    }

    resolutions = manager->getDeviceSupportedResolutions(device);

    if (!was_opened) {
        manager->closeDevice(device);
    }
    return resolutions;
}

editor::devices::Resolution PrivateDeviceProvider::PickResolution(
    const std::shared_ptr<editor::devices::Device>& device,
    int width,
    int height,
    int fps) {
    bool opened_for_query = false;
    const std::vector<editor::devices::Resolution> resolutions = LoadSupportedResolutions(device, &opened_for_query);
    if (resolutions.empty()) {
        return editor::devices::Resolution();
    }

    for (std::vector<editor::devices::Resolution>::const_iterator it = resolutions.begin(); it != resolutions.end(); ++it) {
        if (width > 0 && height > 0) {
            if (it->width == width && it->height == height) {
                editor::devices::Resolution selected = *it;
                if (fps > 0) {
                    selected.fps = fps;
                }
                return selected;
            }
            continue;
        }
        if (it->isDefault) {
            editor::devices::Resolution selected = *it;
            if (fps > 0) {
                selected.fps = fps;
            }
            return selected;
        }
    }

    editor::devices::Resolution selected = resolutions.front();
    if (fps > 0) {
        selected.fps = fps;
    }
    return selected;
}

bool PrivateDeviceProvider::EnsureDeviceManagerReady() const {
    std::lock_guard<std::mutex> lock(init_mu_);
    if (initialized_) {
        return true;
    }

    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
    if (manager == NULL) {
        SDK_PRIVATE_LOG_ERROR("device.manager init_failed manager_null");
        return false;
    }

    if (!EnsureDirectoryRecursive(GetSdkRuntimeLogDir())) {
        SDK_PRIVATE_LOG_ERROR("device.manager init_failed log_dir={}", SafePathSummary(GetSdkRuntimeLogDir()));
        return false;
    }
    const std::string log_path = ResolveDeviceManagerLogPath();
    const std::string config_path = ResolveDeviceConfigPath();
    SDK_PRIVATE_LOG_INFO("device.manager init begin log_path={} config_path={}",
                         SafePathSummary(log_path),
                         SafePathSummary(config_path));
    if (!manager->initialize(log_path, config_path)) {
        SDK_PRIVATE_LOG_ERROR("device.manager init_failed log_path={} config_path={}",
                              SafePathSummary(log_path),
                              SafePathSummary(config_path));
        return false;
    }
    PrivateDeviceProvider* self = const_cast<PrivateDeviceProvider*>(this);
    manager->setHotplugCallback([self](std::shared_ptr<editor::devices::Device> device, bool is_added) {
        self->HandleHotplugEvent(device, is_added);
    });

    initialized_ = true;
    SDK_PRIVATE_LOG_INFO("device.manager init success");
    return true;
}

bool PrivateDeviceProvider::InitializeForRuntimeStartup() const {
    return EnsureDeviceManagerReady();
}

std::vector<SdkDeviceDescriptor> PrivateDeviceProvider::BuildDeviceDescriptors(
    const std::vector<std::shared_ptr<editor::devices::Device> >& devices) const {
    std::vector<SdkDeviceDescriptor> descriptors;
    descriptors.reserve(devices.size());
    for (std::vector<std::shared_ptr<editor::devices::Device> >::const_iterator it = devices.begin();
         it != devices.end();
         ++it) {
        if (*it) {
            descriptors.push_back(BuildDeviceDescriptor(*it));
        }
    }
    return descriptors;
}

SdkDeviceDescriptor PrivateDeviceProvider::BuildDeviceDescriptor(
    const std::shared_ptr<editor::devices::Device>& device) const {
    SdkDeviceDescriptor descriptor;
    if (!device) {
        return descriptor;
    }

    const editor::devices::DeviceInfo info = device->getDeviceInfo();
    const std::shared_ptr<editor::devices::DeviceConfigData> config = device->getDeviceConfig();
    descriptor.device_id = device->getDeviceId();
    descriptor.model = config && !config->deviceName.empty() ? config->deviceName : info.product;
    descriptor.display_name = FormatDisplayName(info, config);
    descriptor.vid = info.vid;
    descriptor.pid = info.pid;
    descriptor.status = device->isStreaming() ? "streaming" : (device->isOpened() ? "opened" : "online");
    descriptor.authorized = false;
    descriptor.supports_video = true;
    descriptor.image_transfer_protocol = HasBoolFeature(config, "ImageTransferProtocol");
    descriptor.capture_output =
        BuildCaptureOutputCapabilities(config, descriptor.image_transfer_protocol);
    return descriptor;
}

std::shared_ptr<editor::devices::Device> PrivateDeviceProvider::LookupDevice(const std::string& device_id) const {
    if (!EnsureDeviceManagerReady()) {
        return std::shared_ptr<editor::devices::Device>();
    }

    editor::devices::DeviceManager* manager = editor::devices::DeviceManager::GetInstance();
    if (manager == NULL) {
        return std::shared_ptr<editor::devices::Device>();
    }
    return manager->findDeviceById(device_id);
}

std::string PrivateDeviceProvider::ResolveDeviceManagerLogPath() const {
    return JoinPath(GetSdkRuntimeLogDir(), "sdk_private_devices.log");
}

std::string PrivateDeviceProvider::ResolveDeviceConfigPath() const {
#if defined(OS_OHOS)
    // The Harmony host stages the bundled device configuration into the
    // service runtime directory before the provider is initialized. Host
    // source paths and process cwd are not stable inside an installed HAP.
    return device_frame_codec::OhosDeviceConfigPath(GetSdkRuntimeDir());
#else
    const std::string exe_config_path = JoinPath(GetSdkExecutableDir(), "configs/device_config_compact.conf");
    if (FileExists(exe_config_path)) {
        return exe_config_path;
    }

    const std::string cwd_config_path = "configs/device_config_compact.conf";
    if (FileExists(cwd_config_path)) {
        return cwd_config_path;
    }

#ifdef SDK_PRIVATE_DEVICE_CONFIG_SOURCE_PATH
    const std::string source_config_path = SDK_PRIVATE_DEVICE_CONFIG_SOURCE_PATH;
    if (FileExists(source_config_path)) {
        return source_config_path;
    }
#endif

    return cwd_config_path;
#endif
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
