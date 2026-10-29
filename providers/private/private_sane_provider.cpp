// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "private_sane_provider.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <ctime>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"
#include <opencv2/opencv.hpp>
#include "support/private_sdk_logger.h"
#include "runtime/sdk_runtime_paths.h"
#include "sdk_status_code.h"

#if defined(__linux__)
#include "module/sane/sane_device.hpp"
#include "module/sane/sane_device_finder.h"
#endif

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

using Json = nlohmann::json;

std::string NowString() {
    return std::to_string(static_cast<long long>(std::time(NULL)));
}

std::string SanitizeKey(std::string value) {
    for (std::string::iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char ch = static_cast<unsigned char>(*it);
        if (!std::isalnum(ch)) {
            *it = '_';
        }
    }
    return value.empty() ? "sane" : value;
}

std::string BackendFromName(const std::string& name) {
    const std::string::size_type pos = name.find(':');
    return pos == std::string::npos ? name : name.substr(0, pos);
}

std::string DeviceIdFromName(const std::string& name) {
    return "sane:" + SanitizeKey(name);
}

Json JsonFromEncoded(const std::string& encoded) {
    if (encoded.empty()) {
        return nullptr;
    }
    try {
        return Json::parse(encoded);
    } catch (...) {
        return encoded;
    }
}

std::string EncodeJson(const Json& value) {
    return value.dump();
}

#if !defined(__linux__)
SdkSaneStatusResult UnavailableResult() {
    SdkSaneStatusResult result;
    result.code = ToCode(SdkStatusCode::SaneNotAvailable);
    result.message = "SANE is only available on Linux";
#if defined(__linux__)
    result.platform = "linux";
#elif defined(_WIN32)
    result.platform = "windows";
#elif defined(__APPLE__)
    result.platform = "macos";
#else
    result.platform = "unknown";
#endif
    result.supported_platforms.push_back("linux");
    result.available = false;
    result.reason = "SANE provider is unavailable on this platform";
            return result;
        }
#endif

SdkSaneDevice BuildDevice(const std::string& name,
                          const std::string& vendor,
                          const std::string& model,
                          const std::string& type) {
    SdkSaneDevice device;
    device.device_name = name;
    device.device_id = DeviceIdFromName(name);
    device.vendor = vendor;
    device.model = model;
    device.type = type;
    device.backend = BackendFromName(name);
    device.status = "online";
    device.discovery_source = "sane_backend";
    device.openable = true;
    return device;
}

#if defined(__linux__)
SdkSaneDevice BuildFinderDevice(const SaneDeviceInfo& info) {
    std::string name = !info.device_name.empty() ? info.device_name : info.device_path;
    if (name.empty()) {
        std::ostringstream generated;
        generated << "usb:" << std::hex << info.vendor_id << ":" << info.product_id
                  << ":" << std::dec << static_cast<int>(info.bus_number)
                  << ":" << static_cast<int>(info.device_address);
        name = generated.str();
    }

    SdkSaneDevice device;
    device.device_name = name;
    device.device_id = DeviceIdFromName("finder:" + name);
    device.vendor = info.vendor;
    device.model = !info.product.empty() ? info.product : info.chipset;
    device.type = CSaneDeviceFinder::getDeviceTypeString(info.device_type);
    device.backend = "finder";
    device.status = "detected";
    device.discovery_source = "finder";
    device.openable = false;
    return device;
}
#endif

bool SameDevice(const SdkSaneDevice& left, const SdkSaneDevice& right) {
    return left.device_id == right.device_id &&
           left.device_name == right.device_name &&
           left.vendor == right.vendor &&
           left.model == right.model &&
           left.type == right.type &&
           left.backend == right.backend &&
           left.status == right.status &&
           left.discovery_source == right.discovery_source &&
           left.openable == right.openable;
}

bool SameDeviceMap(const std::map<std::string, SdkSaneDevice>& left,
                   const std::map<std::string, SdkSaneDevice>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::map<std::string, SdkSaneDevice>::const_iterator it = left.begin(); it != left.end(); ++it) {
        std::map<std::string, SdkSaneDevice>::const_iterator found = right.find(it->first);
        if (found == right.end() || !SameDevice(it->second, found->second)) {
            return false;
        }
    }
    return true;
}

std::vector<SdkSaneDevice> MapValues(const std::map<std::string, SdkSaneDevice>& devices) {
    std::vector<SdkSaneDevice> result;
    result.reserve(devices.size());
    for (std::map<std::string, SdkSaneDevice>::const_iterator it = devices.begin(); it != devices.end(); ++it) {
        result.push_back(it->second);
    }
    return result;
}

SdkSaneProfile ProfileFromJson(const Json& json) {
    SdkSaneProfile profile;
    if (!json.is_object()) {
        return profile;
    }
    profile.profile_id = json.value("profile_id", "");
    profile.device_key = json.value("device_key", "");
    profile.name = json.value("name", "");
    profile.created_at = json.value("created_at", "");
    profile.updated_at = json.value("updated_at", "");
    const Json options = json.value("options", Json::object());
    if (options.is_object()) {
        for (Json::const_iterator it = options.begin(); it != options.end(); ++it) {
            SdkSaneOptionSetItem item;
            item.key = it.key();
            item.value_json = EncodeJson(*it);
            profile.options.push_back(item);
        }
    }
    return profile;
}

Json ProfileToJson(const SdkSaneProfile& profile) {
    Json options = Json::object();
    for (std::vector<SdkSaneOptionSetItem>::const_iterator it = profile.options.begin();
         it != profile.options.end();
         ++it) {
        const std::string key = !it->key.empty() ? it->key : std::to_string(it->index);
        options[key] = JsonFromEncoded(it->value_json);
    }
    return Json{{"profile_id", profile.profile_id},
                {"device_key", profile.device_key},
                {"name", profile.name},
                {"options", options},
                {"created_at", profile.created_at},
                {"updated_at", profile.updated_at}};
}

bool WriteTextFile(const std::string& path, const std::string& content) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << content;
    return static_cast<bool>(out);
}

std::string ProfileStorePath() {
    return JoinPath(JoinPath(GetSdkOpenWorkDir(), "sane"), "profiles.json");
}

void LoadProfiles(std::map<std::string, SdkSaneProfile>* profiles) {
    if (profiles == NULL) {
        return;
    }
    std::ifstream in(ProfileStorePath().c_str(), std::ios::binary);
    if (!in) {
        return;
    }
    Json root;
    try {
        in >> root;
    } catch (...) {
        return;
    }
    if (!root.is_array()) {
        return;
    }
    for (Json::const_iterator it = root.begin(); it != root.end(); ++it) {
        const SdkSaneProfile profile = ProfileFromJson(*it);
        if (!profile.profile_id.empty()) {
            (*profiles)[profile.profile_id] = profile;
        }
    }
}

bool SaveProfiles(const std::map<std::string, SdkSaneProfile>& profiles) {
    const std::string dir = JoinPath(GetSdkOpenWorkDir(), "sane");
    if (!EnsureDirectoryRecursive(dir)) {
        return false;
    }
    Json root = Json::array();
    for (std::map<std::string, SdkSaneProfile>::const_iterator it = profiles.begin(); it != profiles.end(); ++it) {
        root.push_back(ProfileToJson(it->second));
    }
    return WriteTextFile(ProfileStorePath(), root.dump(2));
}

uint64_t FileSize(const std::string& path) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

std::string ExtensionFromPath(const std::string& path) {
    const std::string::size_type slash = path.find_last_of("/\\");
    const std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::string::size_type dot = leaf.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= leaf.size()) {
        return "";
    }
    std::string extension = leaf.substr(dot + 1);
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

std::string ContentTypeForImage(const std::string& extension) {
    if (extension == "png") {
        return "image/png";
    }
    if (extension == "tiff") {
        return "image/tiff";
    }
    return "image/jpeg";
}

std::string NormalizeText(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsAdfLikeSource(const std::string& source) {
    const std::string value = NormalizeText(source);
    return value.find("adf") != std::string::npos ||
           value.find("feeder") != std::string::npos ||
           value.find("automatic document feeder") != std::string::npos ||
           value.find("duplex") != std::string::npos;
}

std::string CurrentTimeString() {
    return NowString();
}

std::string ParentPath(const std::string& path) {
    const std::string::size_type slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash);
}

std::string StemFromPath(const std::string& path) {
    const std::string::size_type slash = path.find_last_of("/\\");
    const std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::string::size_type dot = leaf.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return leaf.empty() ? "page" : leaf;
    }
    return leaf.substr(0, dot);
}

std::string FormatPageNumber(int page_number) {
    std::ostringstream stream;
    stream << std::setw(3) << std::setfill('0') << page_number;
    return stream.str();
}

std::string BuildSanePageOutputPath(const SdkSaneScanRequest& request,
                                    const std::string& output_dir,
                                    const std::string& image_extension,
                                    int page_number) {
    if (request.output_type == "images" && !request.output_path.empty()) {
        if (page_number == 1) {
            return request.output_path;
        }
        const std::string extension = ExtensionFromPath(request.output_path).empty()
                                          ? image_extension
                                          : ExtensionFromPath(request.output_path);
        std::ostringstream name;
        name << StemFromPath(request.output_path) << "_page" << page_number << "." << extension;
        return JoinPath(ParentPath(request.output_path), name.str());
    }
    return JoinPath(output_dir, "page-" + FormatPageNumber(page_number) + "." + image_extension);
}

#if defined(__linux__)
std::string ReadSaneStringOption(SaneDevice* device, const std::vector<std::string>& names) {
    if (!device) {
        return "";
    }
    for (int index = 0;; ++index) {
        const SANE_Option_Descriptor* descriptor = device->getOptionDescriptor(index);
        if (!descriptor) {
            break;
        }
        const std::string option_name = descriptor->name ? NormalizeText(descriptor->name) : "";
        const std::string option_title = descriptor->title ? NormalizeText(descriptor->title) : "";
        bool matched = false;
        for (std::vector<std::string>::const_iterator it = names.begin(); it != names.end(); ++it) {
            const std::string wanted = NormalizeText(*it);
            if (option_name == wanted || option_title == wanted) {
                matched = true;
                break;
            }
        }
        if (!matched || descriptor->type != SANE_TYPE_STRING) {
            continue;
        }
        std::string value;
        if (device->getOptionString(index, &value)) {
            return value;
        }
    }
    return "";
}

bool ApplySaneOptionSetItem(SaneDevice* device, const SdkSaneOptionSetItem& option, std::string* error) {
    if (!device) {
        if (error) *error = "device not open";
        return false;
    }
    int option_index = option.index;
    if (option_index < 0 && !option.key.empty()) {
        for (int index = 0;; ++index) {
            const SANE_Option_Descriptor* descriptor = device->getOptionDescriptor(index);
            if (!descriptor) {
                break;
            }
            if (descriptor->name && option.key == descriptor->name) {
                option_index = index;
                break;
            }
        }
    }
    if (option_index < 0) {
        if (error) *error = "option not found: " + option.key;
        return false;
    }
    const SANE_Option_Descriptor* descriptor = device->getOptionDescriptor(option_index);
    if (!descriptor || !(descriptor->cap & SANE_CAP_SOFT_SELECT)) {
        if (error) *error = "option is not settable: " + option.key;
        return false;
    }
    const Json value = JsonFromEncoded(option.value_json);
    bool ok = false;
    if (descriptor->type == SANE_TYPE_BOOL || descriptor->type == SANE_TYPE_INT) {
        const int int_value = value.is_boolean() ? (value.get<bool>() ? 1 : 0) : (value.is_number() ? value.get<int>() : 0);
        ok = device->setOptionInt(option_index, int_value);
    } else if (descriptor->type == SANE_TYPE_FIXED) {
        const double double_value = value.is_number() ? value.get<double>() : 0.0;
        ok = device->setOptionDouble(option_index, double_value);
    } else if (descriptor->type == SANE_TYPE_STRING) {
        const std::string string_value = value.is_string() ? value.get<std::string>() : value.dump();
        ok = device->setOptionString(option_index, string_value);
    }
    if (!ok && error) {
        *error = "failed to set option: " + option.key;
    }
    return ok;
}

cv::Mat BuildSaneImage(const std::vector<SANE_Byte>& page_data, const SANE_Parameters& params) {
    if (params.pixels_per_line <= 0 || params.lines <= 0 || page_data.empty()) {
        return cv::Mat();
    }
    if (params.depth == 1) {
        cv::Mat gray(params.lines, params.pixels_per_line, CV_8UC1, cv::Scalar(255));
        for (int y = 0; y < params.lines; ++y) {
            const size_t row_offset = static_cast<size_t>(y) * static_cast<size_t>(params.bytes_per_line);
            if (row_offset >= page_data.size()) {
                break;
            }
            for (int x = 0; x < params.pixels_per_line; ++x) {
                const size_t byte_index = row_offset + static_cast<size_t>(x / 8);
                if (byte_index >= page_data.size()) {
                    break;
                }
                const int bit_index = 7 - (x % 8);
                gray.at<unsigned char>(y, x) = (page_data[byte_index] & (1 << bit_index)) ? 0 : 255;
            }
        }
        cv::Mat bgr;
        cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }

    const int cv_type = params.format == SANE_FRAME_RGB ? CV_8UC3 : CV_8UC1;
    const size_t expected_bytes = static_cast<size_t>(params.lines) * static_cast<size_t>(params.bytes_per_line);
    if (page_data.size() < expected_bytes) {
        return cv::Mat();
    }
    cv::Mat source(params.lines, params.pixels_per_line, cv_type, const_cast<SANE_Byte*>(page_data.data()), params.bytes_per_line);
    cv::Mat output;
    if (cv_type == CV_8UC1) {
        cv::cvtColor(source, output, cv::COLOR_GRAY2BGR);
    } else {
        cv::cvtColor(source, output, cv::COLOR_RGB2BGR);
    }
    return output.clone();
}
#endif

template <typename Result>
Result ProviderUnavailable() {
    Result result;
    result.code = ToCode(SdkStatusCode::SaneNotAvailable);
    result.message = "SANE is only available on Linux";
    return result;
}

} // namespace

struct PrivateSaneProvider::Impl {
    std::mutex mu;
    int generation = 1;
    int next_session = 1;
    int next_task = 1;
    int next_profile = 1;
    std::map<std::string, SdkSaneDevice> devices_by_id;
    std::map<std::string, SdkSaneDevice> detected_devices_by_id;
    std::map<std::string, SdkSaneProfile> profiles;
    std::map<std::string, SdkSaneScanTask> tasks;
    std::map<std::string, std::string> active_task_by_session;
    std::map<std::string, bool> cancel_requested_by_task;
#if defined(__linux__)
    std::unique_ptr<SaneManager> manager;
    std::unique_ptr<CSaneDeviceFinder> finder;
    std::map<std::string, std::unique_ptr<SaneDevice> > sessions;
    std::map<std::string, SdkSaneDevice> session_devices;
    std::set<std::string> watchers;
    std::thread watch_thread;
    std::vector<std::thread> scan_threads;
    bool watch_stop = false;
    bool watch_running = false;
#endif
    SdkSaneDeviceEventCallback event_sink;
    SdkSaneScanTaskEventCallback scan_task_event_sink;

#if defined(__linux__)
    void DiscoverDevicesLocked(std::map<std::string, SdkSaneDevice>* devices,
                               std::map<std::string, SdkSaneDevice>* detected_devices) {
        std::map<std::string, SdkSaneDevice> discovered;
        std::map<std::string, SdkSaneDevice> detected;
        if (manager) {
            manager->updateDeviceList();
            const std::vector<SANE_Device> sane_devices = manager->listDevices();
            for (std::vector<SANE_Device>::const_iterator it = sane_devices.begin(); it != sane_devices.end(); ++it) {
                SdkSaneDevice device = BuildDevice(it->name ? it->name : "",
                                                   it->vendor ? it->vendor : "",
                                                   it->model ? it->model : "",
                                                   it->type ? it->type : "");
                discovered[device.device_id] = device;
            }
        }
        if (finder) {
            try {
                const std::vector<SaneDeviceInfo> finder_devices = finder->findDevices();
                for (std::vector<SaneDeviceInfo>::const_iterator it = finder_devices.begin(); it != finder_devices.end(); ++it) {
                    SdkSaneDevice device = BuildFinderDevice(*it);
                    if (!device.device_id.empty()) {
                        detected[device.device_id] = device;
                    }
                }
            } catch (const std::exception& e) {
                SDK_PRIVATE_LOG_WARN("sane finder failed: {}", e.what());
            }
        }
        if (devices) {
            *devices = discovered;
        }
        if (detected_devices) {
            *detected_devices = detected;
        }
    }

    bool RefreshDevicesLocked() {
        std::map<std::string, SdkSaneDevice> next_devices;
        std::map<std::string, SdkSaneDevice> next_detected_devices;
        DiscoverDevicesLocked(&next_devices, &next_detected_devices);
        const bool changed = !SameDeviceMap(next_devices, devices_by_id) ||
                             !SameDeviceMap(next_detected_devices, detected_devices_by_id);
        devices_by_id = next_devices;
        detected_devices_by_id = next_detected_devices;
        if (changed) {
            ++generation;
        }
        return changed;
    }

    std::vector<SdkSaneDeviceEvent> BuildEventsLocked(const std::string& event_name,
                                                      const std::map<std::string, SdkSaneDevice>& previous,
                                                      const std::map<std::string, SdkSaneDevice>& current) const {
        std::vector<SdkSaneDevice> added;
        std::vector<SdkSaneDevice> removed;
        for (std::map<std::string, SdkSaneDevice>::const_iterator it = current.begin(); it != current.end(); ++it) {
            if (previous.find(it->first) == previous.end()) {
                added.push_back(it->second);
            }
        }
        for (std::map<std::string, SdkSaneDevice>::const_iterator it = previous.begin(); it != previous.end(); ++it) {
            if (current.find(it->first) == current.end()) {
                removed.push_back(it->second);
            }
        }

        std::vector<SdkSaneDeviceEvent> events;
        for (std::set<std::string>::const_iterator it = watchers.begin(); it != watchers.end(); ++it) {
            SdkSaneDeviceEvent event;
            event.connection_id = *it;
            event.event_name = event_name;
            event.generation = generation;
            event.devices = MapValues(current);
            event.detected_devices = MapValues(detected_devices_by_id);
            event.added_devices = added;
            event.removed_devices = removed;
            events.push_back(event);
        }
        return events;
    }
#endif

    void StoreTaskLocked(const SdkSaneScanTask& task) {
        tasks[task.task_id] = task;
    }

    void EmitTaskEvent(const SdkSaneScanTask& task, int code = ToCode(SdkStatusCode::Ok), const std::string& message = "ok") {
        SdkSaneScanTaskEventCallback sink;
        {
            std::lock_guard<std::mutex> lock(mu);
            sink = scan_task_event_sink;
        }
        if (!sink || task.connection_id.empty()) {
            return;
        }
        SdkSaneScanTaskEvent event;
        event.code = code;
        event.message = message;
        event.connection_id = task.connection_id;
        event.event_name = "sane.scan_changed";
        event.task = task;
        sink(event);
    }

    void UpdateTaskAndEmit(const std::string& task_id,
                           const std::function<void(SdkSaneScanTask*)>& updater,
                           int code = ToCode(SdkStatusCode::Ok),
                           const std::string& message = "ok") {
        SdkSaneScanTask task;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(mu);
            std::map<std::string, SdkSaneScanTask>::iterator it = tasks.find(task_id);
            if (it == tasks.end()) {
                return;
            }
            if (updater) {
                updater(&it->second);
            }
            it->second.updated_at = CurrentTimeString();
            task = it->second;
            found = true;
        }
        if (found) {
            EmitTaskEvent(task, code, message);
        }
    }
};

PrivateSaneProvider::PrivateSaneProvider()
    : impl_(new Impl()) {
#if defined(__linux__)
    try {
        impl_->manager.reset(new SaneManager(false, nullptr));
        impl_->finder.reset(new CSaneDeviceFinder(0));
        impl_->RefreshDevicesLocked();
    } catch (const std::exception& e) {
        SDK_PRIVATE_LOG_WARN("sane init failed: {}", e.what());
        impl_->manager.reset();
        impl_->finder.reset();
    }
#endif
    LoadProfiles(&impl_->profiles);
}

PrivateSaneProvider::~PrivateSaneProvider() {
#if defined(__linux__)
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        impl_->watch_stop = true;
        for (std::map<std::string, bool>::iterator it = impl_->cancel_requested_by_task.begin();
             it != impl_->cancel_requested_by_task.end();
             ++it) {
            it->second = true;
        }
        for (std::map<std::string, std::unique_ptr<SaneDevice> >::iterator it = impl_->sessions.begin();
             it != impl_->sessions.end();
             ++it) {
            if (it->second) {
                it->second->cancelScan();
            }
        }
    }
    if (impl_->watch_thread.joinable()) {
        impl_->watch_thread.join();
    }
    for (std::vector<std::thread>::iterator it = impl_->scan_threads.begin();
         it != impl_->scan_threads.end();
         ++it) {
        if (it->joinable()) {
            it->join();
        }
    }
#endif
}

std::string PrivateSaneProvider::ProviderName() const {
    return "private-sane-provider";
}

void PrivateSaneProvider::SetDeviceEventSink(SdkSaneDeviceEventCallback sink) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->event_sink = std::move(sink);
}

void PrivateSaneProvider::SetScanTaskEventSink(SdkSaneScanTaskEventCallback sink) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scan_task_event_sink = std::move(sink);
}

SdkSaneStatusResult PrivateSaneProvider::GetStatus() {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(impl_->mu);
    SdkSaneStatusResult result;
    result.platform = "linux";
    result.supported_platforms.push_back("linux");
    result.available = static_cast<bool>(impl_->manager);
    if (!impl_->manager) {
        result.code = ToCode(SdkStatusCode::SaneNotAvailable);
        result.message = "SANE provider is not ready";
        result.reason = "libsane initialization failed";
        return result;
    }
    result.sane_major = impl_->manager->majorVersion();
    result.sane_minor = impl_->manager->minorVersion();
    result.sane_version = std::to_string(result.sane_major) + "." + std::to_string(result.sane_minor);
    result.reason = "ok";
    return result;
#else
    return UnavailableResult();
#endif
}

SdkSaneListResult PrivateSaneProvider::ListDevices(const SdkSaneListRequest& request) {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (!impl_->manager) {
        return ProviderUnavailable<SdkSaneListResult>();
    }
    if (request.refresh) {
        impl_->RefreshDevicesLocked();
    }
    SdkSaneListResult result;
    result.generation = impl_->generation;
    result.devices = MapValues(impl_->devices_by_id);
    if (request.include_detected) {
        result.detected_devices = MapValues(impl_->detected_devices_by_id);
    }
    return result;
#else
    return ProviderUnavailable<SdkSaneListResult>();
#endif
}

SdkSaneWatchResult PrivateSaneProvider::WatchStart(const SdkSaneWatchRequest& request) {
    SdkSaneWatchResult result;
#if defined(__linux__)
    SdkSaneDeviceEventCallback sink;
    SdkSaneDeviceEvent snapshot;
    bool should_start_thread = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (!impl_->manager || request.connection_id.empty()) {
            result.code = ToCode(SdkStatusCode::SaneNotAvailable);
            result.message = !impl_->manager ? "SANE provider is not ready" : "connection_id required";
            return result;
        }
        const std::map<std::string, SdkSaneDevice> previous = impl_->devices_by_id;
        impl_->RefreshDevicesLocked();
        impl_->watchers.insert(request.connection_id);
        impl_->watch_stop = false;
        should_start_thread = !impl_->watch_running;
        if (should_start_thread) {
            impl_->watch_running = true;
        }
        result.watching = true;
        result.generation = impl_->generation;
        sink = impl_->event_sink;

        snapshot.connection_id = request.connection_id;
        snapshot.event_name = "sane.device_snapshot";
        snapshot.generation = impl_->generation;
        snapshot.devices = MapValues(impl_->devices_by_id);
        snapshot.detected_devices = MapValues(impl_->detected_devices_by_id);
        for (std::map<std::string, SdkSaneDevice>::const_iterator it = impl_->devices_by_id.begin();
             it != impl_->devices_by_id.end();
             ++it) {
            if (previous.find(it->first) == previous.end()) {
                snapshot.added_devices.push_back(it->second);
            }
        }
    }
    if (should_start_thread) {
        impl_->watch_thread = std::thread([this]() {
            RunWatchLoop();
        });
    }
    if (sink) {
        sink(snapshot);
    }
#else
    result.code = ToCode(SdkStatusCode::SaneNotAvailable);
    result.message = "SANE is only available on Linux";
#endif
    return result;
}

SdkSaneWatchResult PrivateSaneProvider::WatchStop(const SdkSaneWatchRequest& request) {
    SdkSaneWatchResult result;
#if defined(__linux__)
    std::thread old_thread;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (!request.connection_id.empty()) {
            impl_->watchers.erase(request.connection_id);
        }
        result.watching = false;
        result.generation = impl_->generation;
        if (impl_->watchers.empty() && impl_->watch_running) {
            impl_->watch_stop = true;
            old_thread = std::move(impl_->watch_thread);
            impl_->watch_running = false;
        }
    }
    if (old_thread.joinable()) {
        old_thread.join();
    }
#else
    result.code = ToCode(SdkStatusCode::SaneNotAvailable);
    result.message = "SANE is only available on Linux";
#endif
    return result;
}

void PrivateSaneProvider::RunWatchLoop() {
#if defined(__linux__)
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        SdkSaneDeviceEventCallback sink;
        std::vector<SdkSaneDeviceEvent> events;
        {
            std::lock_guard<std::mutex> lock(impl_->mu);
            if (impl_->watch_stop) {
                impl_->watch_running = false;
                return;
            }
            if (impl_->watchers.empty() || !impl_->manager) {
                continue;
            }
            const std::map<std::string, SdkSaneDevice> previous = impl_->devices_by_id;
            if (!impl_->RefreshDevicesLocked()) {
                continue;
            }
            sink = impl_->event_sink;
            events = impl_->BuildEventsLocked("sane.device_changed", previous, impl_->devices_by_id);
        }

        if (!sink) {
            continue;
        }
        for (std::vector<SdkSaneDeviceEvent>::const_iterator it = events.begin(); it != events.end(); ++it) {
            sink(*it);
        }
    }
#endif
}

SdkSaneOpenResult PrivateSaneProvider::OpenDevice(const SdkSaneOpenRequest& request) {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (!impl_->manager) {
        return ProviderUnavailable<SdkSaneOpenResult>();
    }
    SdkSaneDevice device;
    std::string device_name = request.device_name;
    if (device_name.empty()) {
        impl_->RefreshDevicesLocked();
        std::map<std::string, SdkSaneDevice>::const_iterator it = impl_->devices_by_id.find(request.device_id);
        if (it == impl_->devices_by_id.end() || !it->second.openable) {
            SdkSaneOpenResult result;
            result.code = ToCode(SdkStatusCode::SaneDeviceNotFound);
            result.message = impl_->detected_devices_by_id.find(request.device_id) != impl_->detected_devices_by_id.end() || (it != impl_->devices_by_id.end() && !it->second.openable)
                                 ? "SANE device was detected but is not openable by a SANE backend"
                                 : "SANE device not found";
            return result;
        }
        device = it->second;
        device_name = device.device_name;
    } else {
        device = BuildDevice(device_name, "", "", "");
    }
    try {
        std::unique_ptr<SaneDevice> sane_device(new SaneDevice(device_name));
        const std::string session_id = "sane-session-" + std::to_string(impl_->next_session++);
        impl_->sessions[session_id] = std::move(sane_device);
        impl_->session_devices[session_id] = device;
        SdkSaneOpenResult result;
        result.opened = true;
        result.session_id = session_id;
        result.device = device;
        return result;
    } catch (const std::exception& e) {
        SdkSaneOpenResult result;
        result.code = ToCode(SdkStatusCode::DeviceOpenFailed);
        result.message = e.what();
        return result;
    }
#else
    return ProviderUnavailable<SdkSaneOpenResult>();
#endif
}

SdkSaneCloseResult PrivateSaneProvider::CloseDevice(const SdkSaneCloseRequest& request) {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(impl_->mu);
    SdkSaneCloseResult result;
    std::map<std::string, std::unique_ptr<SaneDevice> >::iterator it = impl_->sessions.find(request.session_id);
    if (it == impl_->sessions.end()) {
        result.closed = false;
        result.was_opened = false;
        return result;
    }
    if (impl_->active_task_by_session.find(request.session_id) != impl_->active_task_by_session.end()) {
        result.code = ToCode(SdkStatusCode::SaneDeviceBusy);
        result.message = "SANE scan is running; cancel the task before closing the device";
        result.closed = false;
        result.was_opened = true;
        return result;
    }
    impl_->sessions.erase(it);
    impl_->session_devices.erase(request.session_id);
    result.closed = true;
    result.was_opened = true;
    return result;
#else
    return ProviderUnavailable<SdkSaneCloseResult>();
#endif
}

SdkSaneGetOptionsResult PrivateSaneProvider::GetOptions(const SdkSaneGetOptionsRequest& request) {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(impl_->mu);
    SdkSaneGetOptionsResult result;
    std::map<std::string, std::unique_ptr<SaneDevice> >::iterator session = impl_->sessions.find(request.session_id);
    if (session == impl_->sessions.end()) {
        result.code = ToCode(SdkStatusCode::DeviceNotOpen);
        result.message = "SANE device session not open";
        return result;
    }
    std::string group = "General";
    for (int index = 0;; ++index) {
        const SANE_Option_Descriptor* descriptor = session->second->getOptionDescriptor(index);
        if (!descriptor) {
            break;
        }
        SdkSaneOption option;
        option.index = index;
        option.name = descriptor->name ? descriptor->name : "";
        option.title = descriptor->title ? descriptor->title : option.name;
        option.description = descriptor->desc ? descriptor->desc : "";
        option.group = group;
        option.readonly = !(descriptor->cap & SANE_CAP_SOFT_SELECT);
        option.settable = !option.readonly;
        option.inactive = descriptor->cap & SANE_CAP_INACTIVE;
        option.automatic = descriptor->cap & SANE_CAP_AUTOMATIC;
        option.advanced = descriptor->cap & SANE_CAP_ADVANCED;
        switch (descriptor->type) {
            case SANE_TYPE_BOOL:
                option.type = "bool";
                break;
            case SANE_TYPE_INT:
                option.type = "int";
                break;
            case SANE_TYPE_FIXED:
                option.type = "fixed";
                break;
            case SANE_TYPE_STRING:
                option.type = "string";
                break;
            case SANE_TYPE_BUTTON:
                option.type = "button";
                break;
            case SANE_TYPE_GROUP:
                option.type = "group";
                group = option.title.empty() ? option.name : option.title;
                break;
            default:
                option.type = "unknown";
                break;
        }
        if (descriptor->constraint_type == SANE_CONSTRAINT_RANGE && descriptor->constraint.range) {
            option.constraint.type = "range";
            option.constraint.min = descriptor->type == SANE_TYPE_FIXED ? SANE_UNFIX(descriptor->constraint.range->min) : descriptor->constraint.range->min;
            option.constraint.max = descriptor->type == SANE_TYPE_FIXED ? SANE_UNFIX(descriptor->constraint.range->max) : descriptor->constraint.range->max;
            option.constraint.quant = descriptor->type == SANE_TYPE_FIXED ? SANE_UNFIX(descriptor->constraint.range->quant) : descriptor->constraint.range->quant;
        } else if (descriptor->constraint_type == SANE_CONSTRAINT_WORD_LIST && descriptor->constraint.word_list) {
            option.constraint.type = "list";
            const int count = descriptor->constraint.word_list[0];
            for (int i = 1; i <= count; ++i) {
                const double value = descriptor->type == SANE_TYPE_FIXED ? SANE_UNFIX(descriptor->constraint.word_list[i]) : descriptor->constraint.word_list[i];
                option.constraint.values_json.push_back(EncodeJson(value));
            }
        } else if (descriptor->constraint_type == SANE_CONSTRAINT_STRING_LIST && descriptor->constraint.string_list) {
            option.constraint.type = "string_list";
            for (int i = 0; descriptor->constraint.string_list[i]; ++i) {
                option.constraint.values_json.push_back(EncodeJson(std::string(descriptor->constraint.string_list[i])));
            }
        }
        if (option.type == "bool" || option.type == "int") {
            int value = 0;
            if (session->second->getOptionInt(index, &value)) {
                option.value_json = option.type == "bool" ? EncodeJson(value != 0) : EncodeJson(value);
            }
        } else if (option.type == "fixed") {
            double value = 0.0;
            if (session->second->getOptionDouble(index, &value)) {
                option.value_json = EncodeJson(value);
            }
        } else if (option.type == "string") {
            std::string value;
            if (session->second->getOptionString(index, &value)) {
                option.value_json = EncodeJson(value);
            }
        }
        result.options.push_back(option);
    }
    return result;
#else
    return ProviderUnavailable<SdkSaneGetOptionsResult>();
#endif
}

SdkSaneSetOptionsResult PrivateSaneProvider::SetOptions(const SdkSaneSetOptionsRequest& request) {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(impl_->mu);
    SdkSaneSetOptionsResult result;
    std::map<std::string, std::unique_ptr<SaneDevice> >::iterator session = impl_->sessions.find(request.session_id);
    if (session == impl_->sessions.end()) {
        result.code = ToCode(SdkStatusCode::DeviceNotOpen);
        result.message = "SANE device session not open";
        return result;
    }
    for (std::vector<SdkSaneOptionSetItem>::const_iterator it = request.options.begin(); it != request.options.end(); ++it) {
        int option_index = it->index;
        if (option_index < 0 && !it->key.empty()) {
            for (int index = 0;; ++index) {
                const SANE_Option_Descriptor* descriptor = session->second->getOptionDescriptor(index);
                if (!descriptor) {
                    break;
                }
                if (descriptor->name && it->key == descriptor->name) {
                    option_index = index;
                    break;
                }
            }
        }
        SdkSaneOptionSetResultItem item;
        item.key = it->key;
        item.index = option_index;
        item.value_json = it->value_json;
        if (option_index < 0) {
            item.status = "rejected";
            item.message = "option not found";
            result.results.push_back(item);
            continue;
        }
        const SANE_Option_Descriptor* descriptor = session->second->getOptionDescriptor(option_index);
        if (!descriptor || !(descriptor->cap & SANE_CAP_SOFT_SELECT)) {
            item.status = "rejected";
            item.message = "option is not settable";
            result.results.push_back(item);
            continue;
        }
        const Json value = JsonFromEncoded(it->value_json);
        bool ok = false;
        if (descriptor->type == SANE_TYPE_BOOL || descriptor->type == SANE_TYPE_INT) {
            const int int_value = value.is_boolean() ? (value.get<bool>() ? 1 : 0) : (value.is_number() ? value.get<int>() : 0);
            ok = session->second->setOptionInt(option_index, int_value);
        } else if (descriptor->type == SANE_TYPE_FIXED) {
            const double double_value = value.is_number() ? value.get<double>() : 0.0;
            ok = session->second->setOptionDouble(option_index, double_value);
        } else if (descriptor->type == SANE_TYPE_STRING) {
            const std::string string_value = value.is_string() ? value.get<std::string>() : value.dump();
            ok = session->second->setOptionString(option_index, string_value);
        }
        item.status = ok ? "applied" : "rejected";
        item.message = ok ? "ok" : "failed to set option";
        result.applied = result.applied || ok;
        result.results.push_back(item);
    }
    return result;
#else
    return ProviderUnavailable<SdkSaneSetOptionsResult>();
#endif
}

SdkSaneProfileListResult PrivateSaneProvider::ListProfiles(const SdkSaneProfileRequest&) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    SdkSaneProfileListResult result;
    for (std::map<std::string, SdkSaneProfile>::const_iterator it = impl_->profiles.begin(); it != impl_->profiles.end(); ++it) {
        result.profiles.push_back(it->second);
    }
    return result;
}

SdkSaneProfileResult PrivateSaneProvider::SaveProfile(const SdkSaneProfileRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    SdkSaneProfileResult result;
    SdkSaneProfile profile;
    profile.profile_id = request.profile_id.empty() ? "sane-profile-" + std::to_string(impl_->next_profile++) : request.profile_id;
    profile.device_key = !request.device_key.empty() ? request.device_key : (!request.device_id.empty() ? request.device_id : DeviceIdFromName(request.device_name));
    profile.name = request.name;
    profile.options = request.options;
    profile.created_at = NowString();
    profile.updated_at = profile.created_at;
    impl_->profiles[profile.profile_id] = profile;
    SaveProfiles(impl_->profiles);
    result.saved = true;
    result.profile = profile;
    return result;
}

SdkSaneProfileResult PrivateSaneProvider::ApplyProfile(const SdkSaneProfileRequest& request) {
    SdkSaneProfile profile;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        std::map<std::string, SdkSaneProfile>::const_iterator it = impl_->profiles.find(request.profile_id);
        if (it == impl_->profiles.end()) {
            SdkSaneProfileResult result;
            result.code = ToCode(SdkStatusCode::SaneProfileNotFound);
            result.message = "SANE profile not found";
            return result;
        }
        profile = it->second;
    }
    SdkSaneProfileResult result;
    if (!request.session_id.empty()) {
        SdkSaneSetOptionsRequest set_request;
        set_request.session_id = request.session_id;
        set_request.options = profile.options;
        const SdkSaneSetOptionsResult set_result = SetOptions(set_request);
        if (!IsOkStatusCode(set_result.code)) {
            result.code = set_result.code;
            result.message = set_result.message;
            result.profile = profile;
            return result;
        }
    }
    result.applied = true;
    result.profile = profile;
    return result;
}

SdkSaneProfileResult PrivateSaneProvider::DeleteProfile(const SdkSaneProfileRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    SdkSaneProfileResult result;
    std::map<std::string, SdkSaneProfile>::iterator it = impl_->profiles.find(request.profile_id);
    if (it == impl_->profiles.end()) {
        result.code = ToCode(SdkStatusCode::SaneProfileNotFound);
        result.message = "SANE profile not found";
        return result;
    }
    result.deleted = true;
    result.profile = it->second;
    impl_->profiles.erase(it);
    SaveProfiles(impl_->profiles);
    return result;
}

SdkSaneScanResult PrivateSaneProvider::Scan(const SdkSaneScanRequest& request) {
#if defined(__linux__)
    SdkSaneScanResult result;
    std::string task_id;
    std::string output_dir;
    std::string image_extension = request.output_format.empty() ? "jpg" : request.output_format;
    std::transform(image_extension.begin(), image_extension.end(), image_extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (image_extension == "jpeg") {
        image_extension = "jpg";
    } else if (image_extension == "tif") {
        image_extension = "tiff";
    }
    if (request.output_type == "pdf" || request.output_type == "ofd" || request.output_type == "tiff") {
        image_extension = "jpg";
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (impl_->sessions.find(request.session_id) == impl_->sessions.end()) {
            result.code = ToCode(SdkStatusCode::DeviceNotOpen);
            result.message = "SANE device session not open";
            return result;
        }
        if (impl_->active_task_by_session.find(request.session_id) != impl_->active_task_by_session.end()) {
            result.code = ToCode(SdkStatusCode::SaneDeviceBusy);
            result.message = "SANE scan task already running for this session";
            return result;
        }
        task_id = "sane-scan-" + std::to_string(impl_->next_task++);
        output_dir = request.output_dir.empty() ? GetSdkOpenTaskAssetDir("sane", task_id, "assets") : request.output_dir;
        if (!EnsureDirectoryRecursive(output_dir)) {
            result.code = ToCode(SdkStatusCode::InternalError);
            result.message = "failed to create SANE output directory";
            return result;
        }

        SdkSaneScanTask task;
        task.task_id = task_id;
        task.connection_id = request.connection_id;
        task.session_id = request.session_id;
        task.status = "queued";
        task.phase = "queued";
        task.progress = 0;
        task.output_type = request.output_type.empty() ? "images" : request.output_type;
        task.output_format = image_extension;
        task.output_dir = output_dir;
        task.export_type = request.export_type.empty() ? "multi-page" : request.export_type;
        task.output_path = request.output_path;
        task.message = "queued";
        task.started_at = CurrentTimeString();
        task.updated_at = task.started_at;
        impl_->tasks[task_id] = task;
        impl_->active_task_by_session[request.session_id] = task_id;
        impl_->cancel_requested_by_task[task_id] = false;
        result.accepted = true;
        result.task_id = task_id;
        result.task = task;
        impl_->scan_threads.push_back(std::thread([this, request, task_id, output_dir, image_extension]() {
            RunScanTask(request, task_id, output_dir, image_extension);
        }));
    }

    impl_->EmitTaskEvent(result.task);
    return result;
#else
    return ProviderUnavailable<SdkSaneScanResult>();
#endif
}

void PrivateSaneProvider::RunScanTask(const SdkSaneScanRequest& request,
                                      const std::string& task_id,
                                      const std::string& output_dir,
                                      const std::string& image_extension) {
#if defined(__linux__)
    const int kMaxPages = 1000;
    SaneDevice* device = NULL;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        std::map<std::string, std::unique_ptr<SaneDevice> >::iterator session = impl_->sessions.find(request.session_id);
        if (session != impl_->sessions.end()) {
            device = session->second.get();
        }
    }
    if (!device) {
        impl_->UpdateTaskAndEmit(task_id, [this, &request](SdkSaneScanTask* task) {
            task->status = "failed";
            task->phase = "failed";
            task->progress = 100;
            task->message = "SANE device session not open";
            task->error = task->message;
            impl_->active_task_by_session.erase(request.session_id);
        }, ToCode(SdkStatusCode::DeviceNotOpen), "SANE device session not open");
        return;
    }

    impl_->UpdateTaskAndEmit(task_id, [](SdkSaneScanTask* task) {
        task->status = "running";
        task->phase = "applying-options";
        task->message = "applying options";
    });

    for (std::vector<SdkSaneOptionSetItem>::const_iterator it = request.options.begin();
         it != request.options.end();
         ++it) {
        std::string error;
        if (!ApplySaneOptionSetItem(device, *it, &error)) {
            impl_->UpdateTaskAndEmit(task_id, [this, &request, &error](SdkSaneScanTask* task) {
                task->status = "failed";
                task->phase = "failed";
                task->progress = 100;
                task->message = error;
                task->error = error;
                impl_->active_task_by_session.erase(request.session_id);
            }, ToCode(SdkStatusCode::SaneOptionInvalid), error);
            return;
        }
    }

    const std::string source = ReadSaneStringOption(device, {"source", "scan source"});
    const bool multi_page = IsAdfLikeSource(source);
    std::vector<SANE_Byte> buffer(64 * 1024);
    int page_number = 1;
    bool scan_complete = false;
    while (!scan_complete) {
        bool cancel_requested = false;
        {
            std::lock_guard<std::mutex> lock(impl_->mu);
            cancel_requested = impl_->cancel_requested_by_task[task_id];
        }
        if (cancel_requested) {
            device->cancelScan();
            impl_->UpdateTaskAndEmit(task_id, [this, &request](SdkSaneScanTask* task) {
                task->status = "cancelled";
                task->phase = "cancelled";
                task->progress = 100;
                task->cancel_requested = true;
                task->message = "cancelled";
                impl_->active_task_by_session.erase(request.session_id);
            });
            return;
        }
        if (page_number > kMaxPages) {
            device->cancelScan();
            impl_->UpdateTaskAndEmit(task_id, [this, &request](SdkSaneScanTask* task) {
                task->status = "failed";
                task->phase = "failed";
                task->progress = 100;
                task->message = "SANE scan reached the internal page limit";
                task->error = task->message;
                impl_->active_task_by_session.erase(request.session_id);
            }, ToCode(SdkStatusCode::SaneScanFailed), "SANE scan reached the internal page limit");
            return;
        }

        impl_->UpdateTaskAndEmit(task_id, [page_number](SdkSaneScanTask* task) {
            task->status = "running";
            task->phase = "scanning";
            task->current_page = page_number;
            task->message = "scanning page " + std::to_string(page_number);
        });

        SANE_Status status = device->startScan();
        if (status == SANE_STATUS_NO_DOCS) {
            device->cancelScan();
            break;
        }
        if (status != SANE_STATUS_GOOD) {
            device->cancelScan();
            if (page_number == 1) {
                const std::string message = SaneDevice::strStatus(status);
                impl_->UpdateTaskAndEmit(task_id, [this, &request, &message](SdkSaneScanTask* task) {
                    task->status = "failed";
                    task->phase = "failed";
                    task->progress = 100;
                    task->message = message;
                    task->error = message;
                    impl_->active_task_by_session.erase(request.session_id);
                }, ToCode(SdkStatusCode::SaneScanFailed), message);
                return;
            }
            break;
        }

        SANE_Parameters params;
        status = device->getParameters(params);
        if (status != SANE_STATUS_GOOD) {
            device->cancelScan();
            const std::string message = SaneDevice::strStatus(status);
            impl_->UpdateTaskAndEmit(task_id, [this, &request, &message](SdkSaneScanTask* task) {
                task->status = "failed";
                task->phase = "failed";
                task->progress = 100;
                task->message = message;
                task->error = message;
                impl_->active_task_by_session.erase(request.session_id);
            }, ToCode(SdkStatusCode::SaneScanFailed), message);
            return;
        }

        std::vector<SANE_Byte> page_data;
        while (true) {
            SANE_Int length = 0;
            status = device->readData(buffer.data(), static_cast<SANE_Int>(buffer.size()), length);
            if (status == SANE_STATUS_EOF) {
                break;
            }
            if (status == SANE_STATUS_NO_DOCS) {
                scan_complete = true;
                break;
            }
            if (status != SANE_STATUS_GOOD) {
                device->cancelScan();
                if (page_number == 1) {
                    const std::string message = SaneDevice::strStatus(status);
                    impl_->UpdateTaskAndEmit(task_id, [this, &request, &message](SdkSaneScanTask* task) {
                        task->status = "failed";
                        task->phase = "failed";
                        task->progress = 100;
                        task->message = message;
                        task->error = message;
                        impl_->active_task_by_session.erase(request.session_id);
                    }, ToCode(SdkStatusCode::SaneScanFailed), message);
                    return;
                }
                scan_complete = true;
                break;
            }
            if (length > 0) {
                page_data.insert(page_data.end(), buffer.begin(), buffer.begin() + length);
            }
        }

        if (!page_data.empty()) {
            const std::string output_path = BuildSanePageOutputPath(request, output_dir, image_extension, page_number);
            if (!EnsureDirectoryRecursive(ParentPath(output_path))) {
                device->cancelScan();
                impl_->UpdateTaskAndEmit(task_id, [this, &request](SdkSaneScanTask* task) {
                    task->status = "failed";
                    task->phase = "failed";
                    task->progress = 100;
                    task->message = "failed to create SANE output directory";
                    task->error = task->message;
                    impl_->active_task_by_session.erase(request.session_id);
                }, ToCode(SdkStatusCode::InternalError), "failed to create SANE output directory");
                return;
            }
            const cv::Mat scanned = BuildSaneImage(page_data, params);
            if (scanned.empty() || !cv::imwrite(output_path, scanned)) {
                device->cancelScan();
                impl_->UpdateTaskAndEmit(task_id, [this, &request](SdkSaneScanTask* task) {
                    task->status = "failed";
                    task->phase = "failed";
                    task->progress = 100;
                    task->message = "failed to encode SANE scan image";
                    task->error = task->message;
                    impl_->active_task_by_session.erase(request.session_id);
                }, ToCode(SdkStatusCode::SaneScanFailed), "failed to encode SANE scan image");
                return;
            }

            impl_->UpdateTaskAndEmit(task_id, [output_path, page_number](SdkSaneScanTask* task) {
                task->phase = "saving-page";
                task->last_page_path = output_path;
                task->page_count = static_cast<int>(task->output_paths.size()) + 1;
                task->current_page = page_number;
                if (task->output_type == "images" && task->output_path.empty()) {
                    task->output_path = output_path;
                }
                task->output_paths.push_back(output_path);
                SdkCaptureAsset asset;
                asset.asset_id = "asset-sane-page-" + FormatPageNumber(page_number);
                asset.kind = "sane_scan_page";
                asset.path = output_path;
                asset.content_type = ContentTypeForImage(ExtensionFromPath(output_path));
                asset.size = FileSize(output_path);
                task->assets.push_back(asset);
                task->message = "saved page " + std::to_string(page_number);
            });
            ++page_number;
        } else if (page_number == 1) {
            device->cancelScan();
            impl_->UpdateTaskAndEmit(task_id, [this, &request](SdkSaneScanTask* task) {
                task->status = "failed";
                task->phase = "failed";
                task->progress = 100;
                task->message = "No documents";
                task->error = task->message;
                impl_->active_task_by_session.erase(request.session_id);
            }, ToCode(SdkStatusCode::SaneScanFailed), "No documents");
            return;
        }

        if (!multi_page || scan_complete) {
            device->cancelScan();
            break;
        }
    }

    SdkSaneScanTask final_task;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        std::map<std::string, SdkSaneScanTask>::iterator it = impl_->tasks.find(task_id);
        if (it == impl_->tasks.end()) {
            return;
        }
        if (it->second.output_paths.empty()) {
            it->second.status = "failed";
            it->second.phase = "failed";
            it->second.progress = 100;
            it->second.message = "No documents";
            it->second.error = it->second.message;
        } else {
            it->second.status = "completed";
            it->second.phase = "completed";
            it->second.progress = 100;
            it->second.page_count = static_cast<int>(it->second.output_paths.size());
            it->second.message = "scan completed";
        }
        it->second.updated_at = CurrentTimeString();
        final_task = it->second;
        impl_->active_task_by_session.erase(request.session_id);
        impl_->cancel_requested_by_task.erase(task_id);
    }
    impl_->EmitTaskEvent(final_task,
                         final_task.status == "completed" ? ToCode(SdkStatusCode::Ok) : ToCode(SdkStatusCode::SaneScanFailed),
                         final_task.message);
#endif
}

SdkSaneScanResult PrivateSaneProvider::GetScan(const SdkSaneScanGetRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    SdkSaneScanResult result;
    std::map<std::string, SdkSaneScanTask>::const_iterator it = impl_->tasks.find(request.task_id);
    if (it == impl_->tasks.end()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "SANE scan task not found";
        return result;
    }
    result.task_id = request.task_id;
    result.task = it->second;
    result.accepted = !request.task_id.empty();
    return result;
}

SdkSaneScanResult PrivateSaneProvider::CancelScan(const SdkSaneScanCancelRequest& request) {
#if defined(__linux__)
    SdkSaneScanResult result;
    SaneDevice* device = NULL;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        std::map<std::string, SdkSaneScanTask>::iterator it = impl_->tasks.find(request.task_id);
        if (it == impl_->tasks.end()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "SANE scan task not found";
            return result;
        }
        it->second.cancel_requested = true;
        it->second.message = "cancel requested";
        impl_->cancel_requested_by_task[request.task_id] = true;
        std::map<std::string, std::unique_ptr<SaneDevice> >::iterator session = impl_->sessions.find(it->second.session_id);
        if (session != impl_->sessions.end()) {
            device = session->second.get();
        }
        result.task_id = request.task_id;
        result.task = it->second;
        result.accepted = true;
    }
    if (device) {
        device->cancelScan();
    }
    impl_->EmitTaskEvent(result.task);
    return result;
#else
    return ProviderUnavailable<SdkSaneScanResult>();
#endif
}

#if defined(__linux__)
std::size_t PrivateSaneProvider::ActiveTaskCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    // active_task_by_session 只在扫描线程完全退出后移除，恰好对应仍可能写入输出目录的任务。
    return impl_->active_task_by_session.size();
}

std::size_t PrivateSaneProvider::ClearFinishedTasks() {
    std::lock_guard<std::mutex> lock(impl_->mu);
    std::size_t count = 0;
    for (std::map<std::string, SdkSaneScanTask>::iterator it = impl_->tasks.begin(); it != impl_->tasks.end();) {
        const std::string& status = it->second.status;
        if (status != "completed" && status != "failed" && status != "cancelled") {
            ++it;
            continue;
        }
        impl_->cancel_requested_by_task.erase(it->first);
        it = impl_->tasks.erase(it);
        ++count;
    }
    return count;
}
#endif

} // namespace private_impl
} // namespace sdk
} // namespace editor
