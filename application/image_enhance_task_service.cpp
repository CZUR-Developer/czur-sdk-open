// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "image_enhance_task_service.h"

#include "image_enhance_quota.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

#include "sdk_logger.h"
#include "sdk_runtime_paths.h"
#include "sdk_status_code.h"

namespace editor {
namespace sdk {

namespace {

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

uint64_t FileSize(const std::string& path) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

std::string JoinLocalPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") {
        return name;
    }
    if (dir[dir.size() - 1] == '/') {
        return dir + name;
    }
    return dir + "/" + name;
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

std::string NormalizeFormat(const std::string& format) {
    std::string value = format;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "jpeg") {
        return "jpg";
    }
    if (value == "tif") {
        return "tiff";
    }
    if (value.empty()) {
        return "jpg";
    }
    return value;
}

std::string ExtensionForFormat(const std::string& format) {
    const std::string normalized = NormalizeFormat(format);
    return normalized == "jpeg" ? "jpg" : normalized;
}

std::string ContentTypeForFormat(const std::string& format) {
    const std::string normalized = NormalizeFormat(format);
    if (normalized == "png") {
        return "image/png";
    }
    if (normalized == "tiff") {
        return "image/tiff";
    }
    if (normalized == "pdf") {
        return "application/pdf";
    }
    if (normalized == "ofd") {
        return "application/ofd";
    }
    return "image/jpeg";
}

Json ParseJsonOrObject(const std::string& value) {
    if (value.empty()) {
        return Json::object();
    }
    try {
        return Json::parse(value);
    } catch (...) {
        return Json::object();
    }
}

bool CopyFile(const std::string& input_path, const std::string& output_path) {
    std::ifstream in(input_path.c_str(), std::ios::binary);
    std::ofstream out(output_path.c_str(), std::ios::binary);
    if (!in || !out) {
        return false;
    }
    out << in.rdbuf();
    return static_cast<bool>(out);
}

std::string PageOutputPath(const std::string& output_dir, int index, const std::string& format) {
    std::ostringstream name;
    name << "enhanced-page-" << std::setw(3) << std::setfill('0') << index << "." << ExtensionForFormat(format);
    return JoinLocalPath(output_dir, name.str());
}

bool IsDocumentTarget(const std::string& target) {
    const std::string value = NormalizeFormat(target);
    return value == "pdf" || value == "ofd" || value == "tiff";
}

Json BuildAssetJsonLocal(const SdkCaptureAsset& asset) {
    return Json{{"asset_id", asset.asset_id},
                {"kind", asset.kind},
                {"path", asset.path},
                {"url", asset.url},
                {"download_url", asset.download_url},
                {"content_type", asset.content_type},
                {"width", asset.width},
                {"height", asset.height},
                {"size", asset.size}};
}

} // namespace

Json BuildImageEnhanceCapabilityProviderJson(const SdkImageEnhanceCapabilityResult& provider) {
    Json capabilities = Json::array();
    for (std::vector<SdkImageEnhanceCapability>::const_iterator it = provider.capabilities.begin();
         it != provider.capabilities.end();
         ++it) {
        Json source_types = Json::array();
        for (std::vector<std::string>::const_iterator source_it = it->source_types.begin();
             source_it != it->source_types.end();
             ++source_it) {
            source_types.push_back(*source_it);
        }
        Json localized_en = Json{{"title", it->title},
                                 {"description", it->description}};
        if (!it->unavailable_reason.empty()) {
            localized_en["unavailable_reason"] = it->unavailable_reason;
        }
        Json localized_zh = Json{{"title", it->title_zh_cn.empty() ? it->title : it->title_zh_cn},
                                 {"description", it->description_zh_cn.empty() ? it->description : it->description_zh_cn}};
        if (!it->unavailable_reason.empty() || !it->unavailable_reason_zh_cn.empty()) {
            localized_zh["unavailable_reason"] =
                it->unavailable_reason_zh_cn.empty() ? it->unavailable_reason : it->unavailable_reason_zh_cn;
        }
        capabilities.push_back(Json{{"type", it->type},
                                    {"title", it->title},
                                    {"description", it->description},
                                    {"i18n_key", it->i18n_key},
                                    {"localized", Json{{"en", localized_en},
                                                       {"zh-CN", localized_zh}}},
                                    {"category", it->category},
                                    {"runtime", it->runtime},
                                    {"available", it->available},
                                    {"unavailable_reason", it->unavailable_reason},
                                    {"requires_capability", it->requires_capability},
                                    {"quota_unit", it->quota_unit},
                                    {"input", Json{{"source_types", source_types},
                                                   {"min_pages", it->min_pages},
                                                   {"max_pages", it->max_pages}}},
                                    {"output", Json{{"page_effect", it->page_effect},
                                                    {"metadata", it->metadata}}},
                                    {"defaults", ParseJsonOrObject(it->defaults_json)},
                                    {"schema", ParseJsonOrObject(it->schema_json)},
                                    {"order_hint", it->order_hint},
                                    {"version", it->version}});
    }
    return Json{{"provider", provider.provider},
                {"kind", provider.kind},
                {"available", provider.available},
                {"capabilities", capabilities}};
}

Json BuildImageEnhanceTaskJson(const SdkImageEnhanceTaskSnapshot& task) {
    Json pages = Json::array();
    for (std::vector<SdkImageEnhancePage>::const_iterator it = task.pages.begin(); it != task.pages.end(); ++it) {
        pages.push_back(Json{{"source_index", it->source_index},
                             {"output_index", it->output_index},
                             {"path", it->path},
                             {"dropped", it->dropped},
                             {"metadata", ParseJsonOrObject(it->metadata_json)}});
    }
    Json steps = Json::array();
    for (std::vector<SdkImageEnhanceStepSnapshot>::const_iterator it = task.steps.begin(); it != task.steps.end(); ++it) {
        steps.push_back(Json{{"id", it->id},
                             {"type", it->type},
                             {"status", it->status},
                             {"provider", it->provider},
                             {"input_page_count", it->input_page_count},
                             {"output_page_count", it->output_page_count},
                             {"metadata", ParseJsonOrObject(it->metadata_json)},
                             {"warnings", it->warnings},
                             {"message", it->message}});
    }
    Json output_paths = Json::array();
    for (std::vector<std::string>::const_iterator it = task.output_paths.begin(); it != task.output_paths.end(); ++it) {
        output_paths.push_back(*it);
    }
    Json assets = Json::array();
    for (std::vector<SdkCaptureAsset>::const_iterator it = task.assets.begin(); it != task.assets.end(); ++it) {
        assets.push_back(BuildAssetJsonLocal(*it));
    }
    return Json{{"task_id", task.task_id},
                {"status", task.status},
                {"phase", task.phase},
                {"progress", task.progress},
                {"input_page_count", task.input_page_count},
                {"output_page_count", task.output_page_count},
                {"pages", pages},
                {"steps", steps},
                {"output_path", task.output_path},
                {"output_paths", output_paths},
                {"output_type", task.output_type},
                {"output_format", task.output_format},
                {"export_type", task.export_type},
                {"assets", assets},
                {"warnings", task.warnings},
                {"error", task.error},
                {"cancel_requested", task.cancel_requested}};
}

ImageEnhanceTaskService::ImageEnhanceTaskService(const ProviderBundle& providers, const std::string& asset_base_url)
    : providers_(providers),
      asset_base_url_(asset_base_url),
      next_task_seq_(1) {}

ImageEnhanceTaskService::~ImageEnhanceTaskService() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (std::map<std::string, SdkImageEnhanceTaskSnapshot>::const_iterator it = tasks_.begin();
             it != tasks_.end();
             ++it) {
            cancel_requested_.insert(it->first);
        }
    }
    for (std::vector<std::thread>::iterator it = workers_.begin(); it != workers_.end(); ++it) {
        if (it->joinable()) {
            it->join();
        }
    }
}

void ImageEnhanceTaskService::SetEventSink(EventSink sink) {
    std::lock_guard<std::mutex> lock(mu_);
    event_sink_ = std::move(sink);
}

SdkImageEnhanceTaskResult ImageEnhanceTaskService::StartTask(const SdkImageEnhanceTaskRequest& request) {
    return StartTask(request, OutputPublisher());
}

SdkImageEnhanceTaskResult ImageEnhanceTaskService::StartTask(const SdkImageEnhanceTaskRequest& request,
                                                          OutputPublisher publisher) {
    SdkImageEnhanceTaskResult result;
    if (request.input_paths.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "image.enhance source is empty";
        return result;
    }
    for (std::vector<std::string>::const_iterator it = request.input_paths.begin(); it != request.input_paths.end(); ++it) {
        if (!FileExists(*it)) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "image.enhance input file does not exist: " + *it;
            return result;
        }
    }
    if (!providers_.image_enhance_provider) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "image enhance provider is not available";
        return result;
    }

    const std::string task_id = NextTaskId();
    const std::string output_dir = request.output_dir.empty()
                                       ? GetSdkOpenTaskAssetDir("image-enhance", task_id, "assets")
                                       : request.output_dir;
    if (!EnsureDirectoryRecursive(output_dir)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "failed to create image enhance output directory";
        return result;
    }

    SdkImageEnhanceTaskRequest task_request = request;
    task_request.output_dir = output_dir;
    if (task_request.pipeline.target.output_dir.empty()) {
        task_request.pipeline.target.output_dir = output_dir;
    }
    if (task_request.pipeline.target.type.empty()) {
        task_request.pipeline.target.type = "images";
    }
    if (task_request.pipeline.target.format.empty()) {
        task_request.pipeline.target.format = "jpg";
    }
    if (task_request.pipeline.target.export_type.empty()) {
        task_request.pipeline.target.export_type = IsDocumentTarget(task_request.pipeline.target.type) ? "multi-page" : "single-page";
    }

    SdkImageEnhanceTaskSnapshot task;
    task.task_id = task_id;
    task.connection_id = request.connection_id;
    task.status = "queued";
    task.phase = "queued";
    task.input_page_count = static_cast<int>(request.input_paths.size());
    task.output_type = NormalizeFormat(task_request.pipeline.target.type);
    task.output_format = NormalizeFormat(IsDocumentTarget(task.output_type) ? task.output_type : task_request.pipeline.target.format);
    task.export_type = task_request.pipeline.target.export_type;
    for (std::size_t i = 0; i < request.input_paths.size(); ++i) {
        SdkImageEnhancePage page;
        page.source_index = static_cast<int>(i + 1);
        page.output_index = static_cast<int>(i + 1);
        page.path = request.input_paths[i];
        task.pages.push_back(page);
    }
    // Prepare every caller-visible result before the worker is started.  A
    // snapshot copy can allocate; after submission such an exception must not
    // make the caller believe the task was rejected while the worker runs.
    result.accepted = true;
    result.task_id = task_id;
    result.task = task;
    {
        std::lock_guard<std::mutex> lock(mu_);
        // Reserve before starting the worker.  std::thread construction starts
        // execution immediately, so a later vector allocation failure must not
        // leave a running worker without an owner.
        workers_.reserve(workers_.size() + 1);
        try {
            tasks_[task_id] = task;
            active_worker_task_ids_.insert(task_id);
            std::thread worker(&ImageEnhanceTaskService::RunTask, this, task_id, task_request, std::move(publisher));
            workers_.push_back(std::move(worker));
        } catch (...) {
            active_worker_task_ids_.erase(task_id);
            tasks_.erase(task_id);
            throw;
        }
    }
    // Submission has already succeeded once the worker is owned by workers_.
    // An event sink is application code and must not turn that success into a
    // caller-visible submission failure.
    PublishEvent(task);
    return result;
}

SdkImageEnhanceTaskSnapshot ImageEnhanceTaskService::GetTask(const std::string& connection_id, const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    SdkImageEnhanceTaskSnapshot task = GetTaskUnlocked(task_id);
    if (!IsOkStatusCode(task.code)) {
        return task;
    }
    if (task.connection_id != connection_id) {
        task.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        task.message = "task belongs to another connection";
        return task;
    }
    AttachAssetUrls(task.task_id, &task.assets);
    return task;
}

SdkImageEnhanceTaskResult ImageEnhanceTaskService::CancelTask(const std::string& connection_id, const SdkImageEnhanceCancelRequest& request) {
    SdkImageEnhanceTaskResult result;
    SdkImageEnhanceTaskSnapshot task;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::map<std::string, SdkImageEnhanceTaskSnapshot>::iterator it = tasks_.find(request.task_id);
        if (it == tasks_.end()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "image enhance task not found";
            return result;
        }
        if (it->second.connection_id != connection_id) {
            result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
            result.message = "task belongs to another connection";
            return result;
        }
        cancel_requested_.insert(request.task_id);
        it->second.cancel_requested = true;
        if (it->second.status == "queued") {
            it->second.status = "cancelled";
            it->second.phase = "cancelled";
            it->second.progress = 100;
        }
        task = it->second;
    }
    result.accepted = true;
    result.task_id = request.task_id;
    result.task = task;
    PublishEvent(task);
    return result;
}

void ImageEnhanceTaskService::CancelAndWait(const std::string& connection_id) {
    std::vector<SdkImageEnhanceTaskSnapshot> cancelled_events;
    std::set<std::string> target_task_ids;
    {
        std::unique_lock<std::mutex> lock(mu_);
        for (std::set<std::string>::const_iterator it = active_worker_task_ids_.begin();
             it != active_worker_task_ids_.end();
             ++it) {
            std::map<std::string, SdkImageEnhanceTaskSnapshot>::iterator task_it = tasks_.find(*it);
            if (!connection_id.empty() &&
                (task_it == tasks_.end() || task_it->second.connection_id != connection_id)) {
                continue;
            }
            target_task_ids.insert(*it);
            cancel_requested_.insert(*it);
            if (task_it != tasks_.end()) {
                task_it->second.cancel_requested = true;
                if (task_it->second.status == "queued") {
                    task_it->second.status = "cancelled";
                    task_it->second.phase = "cancelled";
                    task_it->second.progress = 100;
                    cancelled_events.push_back(task_it->second);
                }
            }
        }
        worker_cv_.wait(lock, [this, &target_task_ids]() {
            for (std::set<std::string>::const_iterator it = target_task_ids.begin();
                 it != target_task_ids.end();
                 ++it) {
                if (active_worker_task_ids_.find(*it) != active_worker_task_ids_.end()) {
                    return false;
                }
            }
            return true;
        });
    }
    for (std::vector<SdkImageEnhanceTaskSnapshot>::const_iterator it = cancelled_events.begin();
         it != cancelled_events.end();
         ++it) {
        PublishEvent(*it);
    }
}

std::size_t ImageEnhanceTaskService::ActiveTaskCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return active_worker_task_ids_.size();
}

std::size_t ImageEnhanceTaskService::ClearFinishedTasks() {
    std::lock_guard<std::mutex> lock(mu_);
    std::size_t count = 0;
    for (std::map<std::string, SdkImageEnhanceTaskSnapshot>::iterator it = tasks_.begin(); it != tasks_.end();) {
        if (active_worker_task_ids_.find(it->first) != active_worker_task_ids_.end()) {
            ++it;
            continue;
        }
        cancel_requested_.erase(it->first);
        it = tasks_.erase(it);
        ++count;
    }
    return count;
}

void ImageEnhanceTaskService::RunTask(const std::string& task_id, SdkImageEnhanceTaskRequest request,
                                     OutputPublisher publisher) noexcept {
    struct WorkerExitGuard {
        ImageEnhanceTaskService* service;
        const std::string* task_id;

        ~WorkerExitGuard() noexcept {
            try {
                service->MarkWorkerExited(*task_id);
            } catch (...) {
            }
        }
    } worker_exit_guard = {this, &task_id};

    try {
        RunTaskImpl(task_id, request, publisher);
    } catch (const std::exception& e) {
        try {
            SDK_OPEN_LOG_ERROR("[image_enhance_task] worker failed task_id={} err={}", task_id, e.what());
        } catch (...) {
        }
        try {
            std::lock_guard<std::mutex> lock(mu_);
            std::map<std::string, SdkImageEnhanceTaskSnapshot>::iterator it = tasks_.find(task_id);
            if (it != tasks_.end()) {
                it->second.status = "failed";
                it->second.phase = "failed";
                it->second.progress = 100;
                it->second.error = e.what();
            }
        } catch (...) {
        }
    } catch (...) {
        try {
            SDK_OPEN_LOG_ERROR("[image_enhance_task] worker failed task_id={} err=<unknown>", task_id);
        } catch (...) {
        }
        try {
            std::lock_guard<std::mutex> lock(mu_);
            std::map<std::string, SdkImageEnhanceTaskSnapshot>::iterator it = tasks_.find(task_id);
            if (it != tasks_.end()) {
                it->second.status = "failed";
                it->second.phase = "failed";
                it->second.progress = 100;
                it->second.error = "image enhance task failed";
            }
        } catch (...) {
        }
    }
}

void ImageEnhanceTaskService::MarkWorkerExited(const std::string& task_id) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        active_worker_task_ids_.erase(task_id);
    }
    worker_cv_.notify_all();
}

void ImageEnhanceTaskService::RunTaskImpl(const std::string& task_id, SdkImageEnhanceTaskRequest request,
                                         const OutputPublisher& publisher) {
    std::vector<SdkImageEnhancePage> pages;
    std::map<std::string, int> online_usage_by_capability;
    SdkImageEnhanceTaskSnapshot event_task;
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        SdkImageEnhanceTaskSnapshot& task = tasks_[task_id];
        if (cancel_requested_.find(task_id) != cancel_requested_.end()) {
            task.status = "cancelled";
            task.phase = "cancelled";
            task.progress = 100;
            task.cancel_requested = true;
            cancelled = true;
        } else {
            task.status = "running";
            task.phase = "enhancing";
            task.progress = 5;
            task.message = "running";
        }
        pages = task.pages;
        event_task = task;
    }
    PublishEvent(event_task);
    if (cancelled) {
        return;
    }

    const int step_count = static_cast<int>(request.pipeline.steps.size());
    for (int i = 0; i < step_count; ++i) {
        const SdkImageEnhanceStep& step = request.pipeline.steps[i];
        if (!step.enabled) {
            continue;
        }
        bool cancelled = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (cancel_requested_.find(task_id) != cancel_requested_.end()) {
                SdkImageEnhanceTaskSnapshot& task = tasks_[task_id];
                task.status = "cancelled";
                task.phase = "cancelled";
                task.progress = 100;
                task.cancel_requested = true;
                event_task = task;
                cancelled = true;
            } else {
                SdkImageEnhanceStepSnapshot snapshot;
                snapshot.id = step.id.empty() ? ("step-" + std::to_string(i + 1)) : step.id;
                snapshot.type = step.type;
                snapshot.status = "running";
                snapshot.provider = providers_.image_enhance_provider ? providers_.image_enhance_provider->ProviderName() : "";
                snapshot.input_page_count = static_cast<int>(pages.size());
                snapshot.message = "running";
                tasks_[task_id].steps.push_back(snapshot);
                tasks_[task_id].progress = 10 + (step_count == 0 ? 0 : (70 * i / step_count));
                event_task = tasks_[task_id];
            }
        }
        PublishEvent(event_task);
        if (cancelled) {
            return;
        }

        SdkImageEnhanceStepRequest step_request;
        step_request.task_id = task_id;
        step_request.step = step;
        step_request.pages = pages;
        step_request.output_dir = JoinLocalPath(request.output_dir, "step-" + std::to_string(i + 1));
        step_request.online_api_key = request.online_api_key;
        step_request.online_base_url = request.online_base_url;
        EnsureDirectoryRecursive(step_request.output_dir);
        const SdkImageEnhanceStepResult step_result = providers_.image_enhance_provider->RunStep(step_request);

        bool should_fail = !IsOkStatusCode(step_result.code);
        const bool skip_on_error = step.on_error == "skip";
        if (should_fail && skip_on_error) {
            should_fail = false;
        }
        const bool successful_online_step = IsOkStatusCode(step_result.code) && IsOnlineEnhanceCapability(step.type);
        const std::string online_capability = NormalizeOnlineEnhanceCapability(step.type);
        const int online_usage_amount = static_cast<int>(step_result.pages.empty() ? pages.size() : step_result.pages.size());
        bool failed = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            SdkImageEnhanceTaskSnapshot& task = tasks_[task_id];
            SdkImageEnhanceStepSnapshot& snapshot = task.steps.back();
            snapshot.status = should_fail ? "failed" : (IsOkStatusCode(step_result.code) ? "completed" : "skipped");
            snapshot.output_page_count = should_fail || !IsOkStatusCode(step_result.code)
                                             ? static_cast<int>(pages.size())
                                             : static_cast<int>(step_result.pages.size());
            snapshot.metadata_json = step_result.metadata_json;
            snapshot.warnings = step_result.warnings;
            snapshot.message = step_result.message;
            if (!step_result.warnings.empty()) {
                task.warnings.insert(task.warnings.end(), step_result.warnings.begin(), step_result.warnings.end());
            }
            if (!IsOkStatusCode(step_result.code) && skip_on_error) {
                task.warnings.push_back(step.type + " skipped: " + step_result.message);
            }
            if (should_fail) {
                task.status = "failed";
                task.phase = "failed";
                task.progress = 100;
                task.error = step_result.message;
                event_task = task;
                failed = true;
            } else {
                if (IsOkStatusCode(step_result.code)) {
                    pages = step_result.pages;
                    task.pages = pages;
                    task.output_page_count = static_cast<int>(pages.size());
                    if (successful_online_step) {
                        online_usage_by_capability[online_capability] += online_usage_amount > 0 ? online_usage_amount : 1;
                    }
                }
                task.progress = 10 + (step_count == 0 ? 70 : (70 * (i + 1) / step_count));
                event_task = task;
            }
        }
        PublishEvent(event_task);
        if (failed) {
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        SdkImageEnhanceTaskSnapshot& task = tasks_[task_id];
        task.phase = "converting";
        task.progress = 90;
        task.pages = pages;
        task.output_page_count = static_cast<int>(pages.size());
        event_task = task;
    }
    PublishEvent(event_task);

    SdkImageEnhanceTaskSnapshot final_task;
    std::vector<std::string> output_paths;
    const std::string target_type = NormalizeFormat(request.pipeline.target.type);
    const std::string output_format = NormalizeFormat(IsDocumentTarget(target_type) ? target_type : request.pipeline.target.format);
    if (pages.empty()) {
        std::lock_guard<std::mutex> lock(mu_);
        SdkImageEnhanceTaskSnapshot& task = tasks_[task_id];
        task.status = "failed";
        task.phase = "failed";
        task.progress = 100;
        task.error = "image enhance produced no output pages";
        final_task = task;
    } else if (IsDocumentTarget(target_type)) {
        SdkFileConvertRequest convert_request;
        for (std::vector<SdkImageEnhancePage>::const_iterator it = pages.begin(); it != pages.end(); ++it) {
            convert_request.input_paths.push_back(it->path);
        }
        convert_request.source_type = "images";
        convert_request.source_format = "image";
        convert_request.target_type = target_type;
        convert_request.output_format = target_type;
        convert_request.export_type = request.pipeline.target.export_type.empty() ? "multi-page" : request.pipeline.target.export_type;
        convert_request.output_dir = request.pipeline.target.output_dir.empty() ? request.output_dir : request.pipeline.target.output_dir;
        convert_request.output_path = request.pipeline.target.output_path;
        convert_request.quality = request.pipeline.target.quality;
        convert_request.tiff_color = request.pipeline.target.tiff_color;
        convert_request.tiff_compression = request.pipeline.target.tiff_compression;
        if (convert_request.output_path.empty() && convert_request.export_type != "single-page") {
            convert_request.output_path = JoinLocalPath(convert_request.output_dir, "enhanced." + ExtensionForFormat(target_type));
        }
        SdkFileConvertResult convert_result;
        if (providers_.ofd_provider) {
            convert_result = providers_.ofd_provider->Convert(convert_request);
        } else {
            convert_result.code = ToCode(SdkStatusCode::ProviderNotReady);
            convert_result.message = "file convert provider is not available";
        }
        std::lock_guard<std::mutex> lock(mu_);
        SdkImageEnhanceTaskSnapshot task = tasks_[task_id];
        if (!IsOkStatusCode(convert_result.code)) {
            task.status = "failed";
            task.phase = "failed";
            task.progress = 100;
            task.error = convert_result.message;
        } else {
            task.output_path = convert_result.output_path;
            task.output_paths = convert_result.output_paths;
            if (task.output_paths.empty() && !task.output_path.empty()) {
                task.output_paths.push_back(task.output_path);
            }
            output_paths = task.output_paths;
            task.status = "completed";
            task.phase = "completed";
            task.progress = 100;
        }
        if (!publisher) {
            tasks_[task_id] = task;
        }
        final_task = task;
    } else {
        const std::string output_dir = request.pipeline.target.output_dir.empty() ? request.output_dir : request.pipeline.target.output_dir;
        EnsureDirectoryRecursive(output_dir);
        int output_index = 1;
        for (std::vector<SdkImageEnhancePage>::iterator it = pages.begin(); it != pages.end(); ++it) {
            const std::string final_path = PageOutputPath(output_dir, output_index, output_format);
            if (NormalizeFormat(ExtensionFromPath(it->path)) == output_format) {
                if (it->path != final_path && !CopyFile(it->path, final_path)) {
                    continue;
                }
            } else if (providers_.graphic_provider) {
                SdkFormatConvertRequest convert_request;
                convert_request.input_path = it->path;
                convert_request.output_path = final_path;
                convert_request.output_format = output_format;
                convert_request.quality = request.pipeline.target.quality;
                const SdkFormatConvertResult convert_result = providers_.graphic_provider->ConvertImageFormat(convert_request);
                if (!IsOkStatusCode(convert_result.code)) {
                    continue;
                }
            } else if (!CopyFile(it->path, final_path)) {
                continue;
            }
            it->path = final_path;
            it->output_index = output_index;
            output_paths.push_back(final_path);
            ++output_index;
        }
        std::lock_guard<std::mutex> lock(mu_);
        SdkImageEnhanceTaskSnapshot task = tasks_[task_id];
        if (output_paths.empty()) {
            task.status = "failed";
            task.phase = "failed";
            task.progress = 100;
            task.error = "failed to write enhanced image outputs";
        } else if (publisher && output_paths.size() != pages.size()) {
            task.status = "failed";
            task.phase = "failed";
            task.progress = 100;
            task.error = "failed to write all enhanced image outputs";
        } else {
            task.pages = pages;
            task.output_path = output_paths.front();
            task.output_paths = output_paths;
            task.status = "completed";
            task.phase = "completed";
            task.progress = 100;
        }
        if (!publisher) {
            tasks_[task_id] = task;
        }
        final_task = task;
    }

    if (final_task.status == "completed") {
        final_task.assets.clear();
        for (std::size_t i = 0; i < final_task.output_paths.size(); ++i) {
            SdkCaptureAsset asset;
            asset.asset_id = "asset-enhance-output-" + std::to_string(static_cast<long long>(i + 1));
            asset.kind = "image_enhance_output";
            asset.path = final_task.output_paths[i];
            asset.content_type = ContentTypeForFormat(output_format);
            asset.size = FileSize(asset.path);
            final_task.assets.push_back(asset);
        }
        AttachAssetUrls(task_id, &final_task.assets);

        OnlineEnhanceQuotaCredentials quota_credentials;
        quota_credentials.online_api_key = request.online_api_key;
        quota_credentials.online_session_token = request.online_session_token;
        quota_credentials.authz_base_url = request.authz_base_url;
        const QuotaConsumeResult quota_result =
            ConfirmOnlineEnhanceQuota(providers_, quota_credentials, task_id, online_usage_by_capability);
        if (!IsOkStatusCode(quota_result.code)) {
            final_task.status = "failed";
            final_task.phase = "failed";
            final_task.progress = 100;
            final_task.code = quota_result.code;
            final_task.message = quota_result.message;
            final_task.error = quota_result.message.empty() ? "online image enhance quota confirm failed" : quota_result.message;
            final_task.assets.clear();
        }

        if (publisher && final_task.status == "completed") {
            PublishOutputs(task_id, publisher, &final_task);
        }
        std::lock_guard<std::mutex> lock(mu_);
        if (publisher) {
            final_task.cancel_requested = cancel_requested_.count(task_id) != 0;
        }
        tasks_[task_id] = final_task;
    } else if (publisher) {
        std::lock_guard<std::mutex> lock(mu_);
        final_task.cancel_requested = cancel_requested_.count(task_id) != 0;
        tasks_[task_id] = final_task;
    }
    PublishEvent(final_task);
}

void ImageEnhanceTaskService::PublishOutputs(const std::string& task_id,
                                            const OutputPublisher& publisher,
                                            SdkImageEnhanceTaskSnapshot* task) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (cancel_requested_.count(task_id) != 0) {
            task->status = "cancelled";
            task->phase = "cancelled";
            task->cancel_requested = true;
            task->output_path.clear();
            task->output_paths.clear();
            task->assets.clear();
            return;
        }
    }
    // This is the publication boundary. Cancellation arriving afterwards is
    // recorded, but cannot interrupt a filesystem commit already in progress.
    const OutputPublication published = publisher(task->output_paths);
    if (!IsOkStatusCode(published.code) || published.output_paths.empty() ||
        published.output_paths.size() != task->output_paths.size() ||
        std::find(published.output_paths.begin(), published.output_paths.end(), std::string()) !=
            published.output_paths.end()) {
        task->status = "failed";
        task->phase = "failed";
        task->code = IsOkStatusCode(published.code) ? ToCode(SdkStatusCode::InternalError) : published.code;
        task->message = IsOkStatusCode(published.code) ? "invalid published output paths" : published.message;
        task->error = task->message;
        task->output_path.clear();
        task->output_paths.clear();
        task->assets.clear();
        return;
    }
    for (auto& page : task->pages) {
        for (std::size_t i = 0; i < task->output_paths.size(); ++i) {
            if (page.path == task->output_paths[i]) {
                page.path = published.output_paths[i];
                break;
            }
        }
    }
    for (std::size_t i = 0; i < task->assets.size(); ++i) {
        task->assets[i].path = published.output_paths[i];
        task->assets[i].url.clear();
        task->assets[i].download_url.clear();
    }
    task->output_paths = published.output_paths;
    task->output_path = task->output_paths.front();
    AttachAssetUrls(task_id, &task->assets);
}

void ImageEnhanceTaskService::PublishEvent(const SdkImageEnhanceTaskSnapshot& task) const {
    try {
        EventSink sink;
        {
            std::lock_guard<std::mutex> lock(mu_);
            sink = event_sink_;
        }
        if (!sink || task.connection_id.empty()) {
            return;
        }
        SdkImageEnhanceTaskSnapshot event_task = task;
        AttachAssetUrls(event_task.task_id, &event_task.assets);
        sink(event_task.connection_id,
             BuildWsEvent("image.enhance_changed",
                          Json{{"task_id", event_task.task_id},
                               {"task", BuildImageEnhanceTaskJson(event_task)}},
                          event_task.code,
                          event_task.message));
    } catch (const std::exception& e) {
        try {
            SDK_OPEN_LOG_ERROR("[image_enhance_task] event sink failed task_id={} err={}", task.task_id, e.what());
        } catch (...) {
        }
    } catch (...) {
        try {
            SDK_OPEN_LOG_ERROR("[image_enhance_task] event sink failed task_id={} err=<unknown>", task.task_id);
        } catch (...) {
        }
    }
}

SdkImageEnhanceTaskSnapshot ImageEnhanceTaskService::GetTaskUnlocked(const std::string& task_id) const {
    std::map<std::string, SdkImageEnhanceTaskSnapshot>::const_iterator it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        SdkImageEnhanceTaskSnapshot task;
        task.code = ToCode(SdkStatusCode::InvalidParams);
        task.message = "image enhance task not found";
        return task;
    }
    return it->second;
}

void ImageEnhanceTaskService::AttachAssetUrls(const std::string& task_id, std::vector<SdkCaptureAsset>* assets) const {
    if (!assets || asset_base_url_.empty()) {
        return;
    }
    for (std::vector<SdkCaptureAsset>::iterator it = assets->begin(); it != assets->end(); ++it) {
        if (it->url.empty()) {
            it->url = asset_base_url_ + "/api/assets/" + task_id + "/" + it->asset_id;
        }
        if (it->download_url.empty()) {
            it->download_url = it->url + "/download";
        }
    }
}

std::string ImageEnhanceTaskService::NextTaskId() {
    const uint64_t seq = next_task_seq_.fetch_add(1);
    return "image-enhance-" + std::to_string(seq);
}

} // namespace sdk
} // namespace editor
