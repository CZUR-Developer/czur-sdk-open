// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "capture_pipeline_service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <sys/stat.h>

#include "CGraph.h"
#include "sdk_logger.h"
#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {

namespace {

std::string ExtensionForFormat(const std::string& format) {
    if (format == "png") {
        return ".png";
    }
    if (format == "tiff" || format == "tif") {
        return ".tiff";
    }
    return ".jpg";
}

std::string ContentTypeForFormat(const std::string& format) {
    if (format == "png") {
        return "image/png";
    }
    if (format == "tiff" || format == "tif") {
        return "image/tiff";
    }
    return "image/jpeg";
}

bool HasExtension(const std::string& path, const std::string& format) {
    const std::string ext = ExtensionForFormat(format);
    if (path.size() < ext.size()) {
        return false;
    }
    std::string suffix = path.substr(path.size() - ext.size());
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
    return suffix == ext;
}

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

uint64_t FileSize(const std::string& path) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

bool CopyFileBinary(const std::string& input_path, const std::string& output_path) {
    std::ifstream input(input_path.c_str(), std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::ofstream output(output_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << input.rdbuf();
    return output.good();
}

std::string SanitizeAssetToken(const std::string& value, const std::string& fallback) {
    std::string token;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char ch = static_cast<unsigned char>(*it);
        if (std::isalnum(ch)) {
            token.push_back(static_cast<char>(std::tolower(ch)));
        } else if (*it == '-' || *it == '_') {
            token.push_back('-');
        }
    }
    return token.empty() ? fallback : token;
}

SdkCaptureAsset MakeAsset(const std::string& id,
                          const std::string& kind,
                          const std::string& path,
                          const std::string& content_type,
                          int width = 0,
                          int height = 0,
                          uint64_t size = 0) {
    SdkCaptureAsset asset;
    asset.asset_id = id;
    asset.kind = kind;
    asset.path = path;
    asset.content_type = content_type;
    asset.width = width;
    asset.height = height;
    asset.size = size;
    return asset;
}

enum class ThumbnailTarget {
    Original,
    PageProcessed,
    ColorProcessed,
    Final
};

struct ThumbnailSpec {
    bool enabled = false;
    std::string stage_name;
    std::string kind;
    std::string input_asset_id;
    std::string output_asset_id;
    std::string input_path;
};

struct ProcessedOutput {
    std::string output_id;
    std::string role;
    int index = 0;
    std::string page_asset_id;
    std::string page_asset_kind;
    std::string page_path;
    std::string color_asset_id;
    std::string color_asset_kind;
    std::string color_path;
    std::string final_asset_id;
    std::string final_path;
    int width = 0;
    int height = 0;
    uint64_t size = 0;
};

class CapturePipelineContext {
public:
    CapturePipelineContext(const CapturePipelineRequest& request,
                           const DeviceFacade& device_facade,
                           const GraphicFacade& graphic_facade,
                           const ProviderBundle& providers,
                           CaptureStageCallback stage_callback)
        : request_(request),
          device_facade_(device_facade),
          graphic_facade_(graphic_facade),
          providers_(providers),
          stage_callback_(stage_callback) {
        output_dir_ = request_.output_dir.empty() ? GetSdkOpenCaptureTaskDir(request_.task_id) : request_.output_dir;
        if (!EnsureDirectoryRecursive(output_dir_)) {
            SDK_OPEN_LOG_WARN("[capture_pipeline] failed to create output_dir={}", output_dir_);
        }
    }

    CStatus CaptureRaw() {
        StartStage("capture_raw");
        SdkCaptureRequest capture_request;
        capture_request.device_id = request_.device_id;
        capture_request.output_dir = output_dir_;
        capture_request.include_base64 = request_.include_base64;
        capture_request.timeout_ms = request_.timeout_ms;

        std::mutex capture_mu;
        std::condition_variable capture_cv;
        bool completed = false;
        SdkCaptureResult result;
        device_facade_.CaptureStill(request_.auth_context, capture_request, [&](const SdkCaptureResult& capture_result) {
            {
                std::lock_guard<std::mutex> lock(capture_mu);
                if (completed) {
                    return;
                }
                result = capture_result;
                completed = true;
            }
            capture_cv.notify_one();
        });

        {
            std::unique_lock<std::mutex> lock(capture_mu);
            if (!completed) {
                const int timeout_ms = request_.timeout_ms > 0 ? request_.timeout_ms : 15000;
                if (!capture_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&completed]() { return completed; })) {
                    completed = true;
                    result.code = ToCode(SdkStatusCode::CaptureTimeout);
                    result.message = "capture timeout";
                }
            }
        }

        if (!IsOkStatusCode(result.code) || !result.captured) {
            FinishStage("capture_raw", "failed", ProviderName(providers_device), result.message, {}, {});
            pipeline_result_.code = result.code;
            pipeline_result_.message = result.message.empty() ? "capture failed" : result.message;
            pipeline_result_.status = "failed";
            return CStatus(pipeline_result_.message);
        }

        original_path_ = !result.original_path.empty() ? result.original_path : result.output_path;
        laser_path_ = result.laser_path;
        original_width_ = result.width;
        original_height_ = result.height;
        detected_rects_ = result.detected_rects;
        detected_rects_source_width_ = result.detected_rects_source_width;
        detected_rects_source_height_ = result.detected_rects_source_height;
        scan_device_type_ = result.scan_device_type;
        AddAsset(MakeAsset("asset-original", "original", original_path_, result.content_type.empty() ? "image/jpeg" : result.content_type, result.width, result.height, result.size));
        if (!laser_path_.empty()) {
            AddAsset(MakeAsset("asset-laser", "laser", laser_path_, "image/jpeg"));
        }
        FinishStage("capture_raw", "succeeded", ProviderName(providers_device), "ok", {}, {"asset-original"});
        return CStatus();
    }

    CStatus PageProcess() {
        StartStage("page_process");
        SdkPageProcessRequest page_request;
        page_request.device_id = request_.device_id;
        page_request.input_path = original_path_;
        page_request.laser_path = laser_path_;
        page_request.output_dir = output_dir_;
        page_request.output_path = JoinPath(output_dir_, "page_processed.jpg");
        page_request.page_processing = request_.profile.page_processing;
        page_request.width = original_width_;
        page_request.height = original_height_;
        page_request.single_page_realtime_detect_rects = request_.profile.single_page_realtime_detect_rects;
        page_request.single_page = request_.profile.single_page;
        page_request.curved_book = request_.profile.curved_book;
        page_request.detected_rects = detected_rects_;
        page_request.detected_rects_source_width = detected_rects_source_width_;
        page_request.detected_rects_source_height = detected_rects_source_height_;
        page_request.selected_area_rect = request_.profile.selected_area_rect;
        page_request.selected_area_source_width = request_.profile.selected_area_source_width;
        page_request.selected_area_source_height = request_.profile.selected_area_source_height;
        page_request.scan_device_type = scan_device_type_;
        const SdkPageProcessResult result = graphic_facade_.ProcessPage(page_request);
        if (!IsOkStatusCode(result.code) || result.unsupported || !result.processed) {
            page_outputs_.clear();
            ProcessedOutput output;
            output.output_id = "page-001";
            output.role = "page";
            output.index = 0;
            output.page_asset_id = "asset-page-processed";
            output.page_asset_kind = "page_processed";
            output.page_path = original_path_;
            output.width = original_width_;
            output.height = original_height_;
            output.size = FileSize(original_path_);
            page_outputs_.push_back(output);
            AddWarning("page_process fallback: " + result.message);
            FinishStage("page_process", "succeeded", ProviderName(providers_graphic), "fallback to original", {"asset-original"}, {"asset-page-processed"});
            AddAsset(MakeAsset(output.page_asset_id, output.page_asset_kind, output.page_path, "image/jpeg", output.width, output.height, output.size));
            return CStatus();
        }

        page_outputs_.clear();
        if (!result.outputs.empty()) {
            for (std::vector<SdkPageOutput>::const_iterator it = result.outputs.begin(); it != result.outputs.end(); ++it) {
                if (it->path.empty()) {
                    continue;
                }
                AddPageOutputFromProvider(*it, result.outputs.size() == 1);
            }
        } else {
            SdkPageOutput output;
            output.output_id = "page-001";
            output.role = "page";
            output.index = 0;
            output.path = result.output_path.empty() ? page_request.output_path : result.output_path;
            output.content_type = "image/jpeg";
            output.width = original_width_;
            output.height = original_height_;
            output.size = FileSize(output.path);
            AddPageOutputFromProvider(output, true);
        }

        if (page_outputs_.empty()) {
            pipeline_result_.code = ToCode(SdkStatusCode::ProviderCallFailed);
            pipeline_result_.message = "page process produced no output";
            pipeline_result_.status = "failed";
            FinishStage("page_process", "failed", ProviderName(providers_graphic), pipeline_result_.message, {"asset-original"}, {});
            return CStatus(pipeline_result_.message);
        }

        FinishStage("page_process", "succeeded", ProviderName(providers_graphic), "ok", {"asset-original"}, PageAssetIds());
        return CStatus();
    }

    CStatus ProcessOutputWorkflow() {
        const bool single = page_outputs_.size() <= 1;
        for (std::vector<ProcessedOutput>::iterator it = page_outputs_.begin(); it != page_outputs_.end(); ++it) {
            CStatus status = GenerateThumbnail(BuildThumbnailSpecForOutput(ThumbnailTarget::PageProcessed, *it, single));
            if (status.isErr()) {
                return status;
            }
            status = ApplyColorModeForOutput(*it, single);
            if (status.isErr()) {
                return status;
            }
            status = GenerateThumbnail(BuildThumbnailSpecForOutput(ThumbnailTarget::ColorProcessed, *it, single));
            if (status.isErr()) {
                return status;
            }
            status = FormatConvertForOutput(*it, single);
            if (status.isErr()) {
                return status;
            }
            status = GenerateThumbnail(BuildThumbnailSpecForOutput(ThumbnailTarget::Final, *it, single));
            if (status.isErr()) {
                return status;
            }
        }
        return CStatus();
    }

    CStatus RunThumbnail(ThumbnailTarget target) {
        const std::vector<ThumbnailSpec> specs = BuildThumbnailSpecs(target);
        for (std::vector<ThumbnailSpec>::const_iterator it = specs.begin(); it != specs.end(); ++it) {
            if (!it->enabled) {
                continue;
            }
            const CStatus status = GenerateThumbnail(*it);
            if (status.isErr()) {
                return status;
            }
        }
        return CStatus();
    }

    CStatus FinalizeResult() {
        StartStage("finalize_result");
        FinishStage("finalize_result", "succeeded", "sdk-open", "ok", FinalAssetIds(), {});
        if (pipeline_result_.status != "failed") {
            pipeline_result_.code = ToCode(SdkStatusCode::Ok);
            pipeline_result_.message = "ok";
            pipeline_result_.status = "succeeded";
        }
        return CStatus();
    }

    CapturePipelineResult Result() const {
        std::lock_guard<std::mutex> lock(mu_);
        CapturePipelineResult result = pipeline_result_;
        result.assets = assets_;
        result.stages = stages_;
        result.warnings = warnings_;
        return result;
    }

private:
    enum ProviderKind {
        providers_device,
        providers_graphic
    };

    std::string ProviderName(ProviderKind kind) const {
        if (kind == providers_device) {
            return providers_.device_provider ? providers_.device_provider->ProviderName() : "";
        }
        return providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : "";
    }

    void StartStage(const std::string& name) {
        SdkCaptureStageResult stage;
        stage.name = name;
        stage.status = "running";
        stage.message = "running";
        if (stage_callback_) {
            stage_callback_(stage);
        }
    }

    void FinishStage(const std::string& name,
                     const std::string& status,
                     const std::string& provider,
                     const std::string& message,
                     const std::vector<std::string>& inputs,
                     const std::vector<std::string>& outputs) {
        SdkCaptureStageResult stage;
        stage.name = name;
        stage.status = status;
        stage.provider = provider;
        stage.message = message;
        stage.input_assets = inputs;
        stage.output_assets = outputs;
        {
            std::lock_guard<std::mutex> lock(mu_);
            stages_.push_back(stage);
        }
        if (stage_callback_) {
            stage_callback_(stage);
        }
    }

    void AddAsset(const SdkCaptureAsset& asset) {
        std::lock_guard<std::mutex> lock(mu_);
        for (std::vector<SdkCaptureAsset>::iterator it = assets_.begin(); it != assets_.end(); ++it) {
            if (it->asset_id == asset.asset_id) {
                *it = asset;
                return;
            }
        }
        assets_.push_back(asset);
    }

    void AddWarning(const std::string& warning) {
        std::lock_guard<std::mutex> lock(mu_);
        warnings_.push_back(warning);
    }

    void AddPageOutputFromProvider(const SdkPageOutput& provider_output, bool single) {
        ProcessedOutput output;
        output.output_id = SanitizeAssetToken(provider_output.output_id, "page-" + std::to_string(page_outputs_.size() + 1));
        output.role = provider_output.role.empty() ? "page" : provider_output.role;
        output.index = provider_output.index;
        output.page_path = provider_output.path;
        output.width = provider_output.width;
        output.height = provider_output.height;
        output.size = provider_output.size > 0 ? provider_output.size : FileSize(provider_output.path);
        output.page_asset_id = single ? "asset-page-processed" : ("asset-page-processed-" + output.output_id);
        output.page_asset_kind = single ? "page_processed" : ("page_processed_" + output.output_id);
        AddAsset(MakeAsset(output.page_asset_id, output.page_asset_kind, output.page_path, provider_output.content_type.empty() ? "image/jpeg" : provider_output.content_type, output.width, output.height, output.size));
        page_outputs_.push_back(output);
    }

    std::vector<std::string> PageAssetIds() const {
        std::vector<std::string> ids;
        for (std::vector<ProcessedOutput>::const_iterator it = page_outputs_.begin(); it != page_outputs_.end(); ++it) {
            ids.push_back(it->page_asset_id);
        }
        return ids;
    }

    std::vector<std::string> ColorAssetIds() const {
        std::vector<std::string> ids;
        for (std::vector<ProcessedOutput>::const_iterator it = page_outputs_.begin(); it != page_outputs_.end(); ++it) {
            if (!it->color_asset_id.empty()) {
                ids.push_back(it->color_asset_id);
            }
        }
        return ids;
    }

    std::vector<std::string> FinalAssetIds() const {
        std::vector<std::string> ids;
        for (std::vector<ProcessedOutput>::const_iterator it = page_outputs_.begin(); it != page_outputs_.end(); ++it) {
            if (!it->final_asset_id.empty()) {
                ids.push_back(it->final_asset_id);
            }
        }
        return ids;
    }

    CStatus ApplyColorModeForOutput(ProcessedOutput& output, bool single) {
        const std::string stage_name = single ? "color_mode" : ("color_mode_" + output.output_id);
        StartStage(stage_name);

        SdkColorModeRequest color_request;
        color_request.input_path = output.page_path;
        color_request.output_path = JoinPath(output_dir_, single ? "color_processed.jpg" : ("color_processed_" + output.output_id + ".jpg"));
        color_request.color_mode = request_.profile.color_mode;
        const SdkColorModeResult result = graphic_facade_.ApplyColorMode(color_request);
        if (!IsOkStatusCode(result.code) || result.unsupported || !result.processed) {
            output.color_path = output.page_path;
            AddWarning(stage_name + " fallback: " + result.message);
        } else {
            output.color_path = result.output_path.empty() ? color_request.output_path : result.output_path;
        }
        output.color_asset_id = single ? "asset-color-processed" : ("asset-color-processed-" + output.output_id);
        output.color_asset_kind = single ? "color_processed" : ("color_processed_" + output.output_id);
        AddAsset(MakeAsset(output.color_asset_id, output.color_asset_kind, output.color_path, "image/jpeg", output.width, output.height, FileSize(output.color_path)));
        FinishStage(stage_name, "succeeded", ProviderName(providers_graphic), output.color_path == output.page_path ? "fallback to page result" : "ok", {output.page_asset_id}, {output.color_asset_id});
        return CStatus();
    }

    CStatus FormatConvertForOutput(ProcessedOutput& output, bool single) {
        const std::string stage_name = single ? "format_convert" : ("format_convert_" + output.output_id);
        StartStage(stage_name);

        const std::string final_path = JoinPath(output_dir_, single ? ("capture_final" + ExtensionForFormat(request_.profile.output_format))
                                                                    : ("capture_final_" + output.output_id + ExtensionForFormat(request_.profile.output_format)));
        if (HasExtension(output.color_path, request_.profile.output_format)) {
            output.final_path = output.color_path;
        } else {
            SdkFormatConvertRequest convert_request;
            convert_request.input_path = output.color_path;
            convert_request.output_path = final_path;
            convert_request.output_format = request_.profile.output_format;
            const SdkFormatConvertResult result = graphic_facade_.ConvertImageFormat(convert_request);
            if (!IsOkStatusCode(result.code) || (!result.converted && !result.passthrough)) {
                FinishStage(stage_name, "failed", ProviderName(providers_graphic), result.message, {output.color_asset_id}, {});
                pipeline_result_.code = result.code;
                pipeline_result_.message = result.message.empty() ? "format convert failed" : result.message;
                pipeline_result_.status = "failed";
                return CStatus(pipeline_result_.message);
            }
            output.final_path = result.output_path.empty() ? final_path : result.output_path;
        }
        output.final_asset_id = single ? "asset-final" : ("asset-final-" + output.output_id);
        AddAsset(MakeAsset(output.final_asset_id, single ? "final" : ("final_" + output.output_id), output.final_path, ContentTypeForFormat(request_.profile.output_format), output.width, output.height, FileSize(output.final_path)));
        FinishStage(stage_name, "succeeded", ProviderName(providers_graphic), output.final_path == output.color_path ? "passthrough" : "ok", {output.color_asset_id}, {output.final_asset_id});
        return CStatus();
    }

    ThumbnailSpec BuildThumbnailSpecForOutput(ThumbnailTarget target, const ProcessedOutput& output, bool single) const {
        ThumbnailSpec spec;
        if (target == ThumbnailTarget::PageProcessed) {
            spec.enabled = request_.profile.thumbnail_page_processed;
            spec.stage_name = single ? "page_processed_thumbnail" : ("page_processed_thumbnail_" + output.output_id);
            spec.kind = single ? "page_processed_thumbnail" : ("page_processed_thumbnail_" + output.output_id);
            spec.input_asset_id = output.page_asset_id;
            spec.output_asset_id = single ? "asset-thumbnail-page-processed" : ("asset-thumbnail-page-processed-" + output.output_id);
            spec.input_path = output.page_path;
            return spec;
        }
        if (target == ThumbnailTarget::ColorProcessed) {
            spec.enabled = request_.profile.thumbnail_color_processed;
            spec.stage_name = single ? "color_processed_thumbnail" : ("color_processed_thumbnail_" + output.output_id);
            spec.kind = single ? "color_processed_thumbnail" : ("color_processed_thumbnail_" + output.output_id);
            spec.input_asset_id = output.color_asset_id;
            spec.output_asset_id = single ? "asset-thumbnail-color-processed" : ("asset-thumbnail-color-processed-" + output.output_id);
            spec.input_path = output.color_path;
            return spec;
        }
        if (target == ThumbnailTarget::Final) {
            spec.enabled = request_.profile.thumbnail_final;
            spec.stage_name = single ? "final_thumbnail" : ("final_thumbnail_" + output.output_id);
            spec.kind = single ? "final_thumbnail" : ("final_thumbnail_" + output.output_id);
            spec.input_asset_id = output.final_asset_id;
            spec.output_asset_id = single ? "asset-thumbnail-final" : ("asset-thumbnail-final-" + output.output_id);
            spec.input_path = output.final_path;
            return spec;
        }
        return spec;
    }

    std::vector<ThumbnailSpec> BuildThumbnailSpecs(ThumbnailTarget target) const {
        std::vector<ThumbnailSpec> specs;
        if (target == ThumbnailTarget::Original) {
            ThumbnailSpec spec;
            spec.enabled = request_.profile.thumbnail_original;
            spec.stage_name = "original_thumbnail";
            spec.kind = "original_thumbnail";
            spec.input_asset_id = "asset-original";
            spec.output_asset_id = "asset-thumbnail-original";
            spec.input_path = original_path_;
            specs.push_back(spec);
            return specs;
        }
        return specs;
    }

    CStatus GenerateThumbnail(const ThumbnailSpec& spec) {
        if (!spec.enabled) {
            return CStatus();
        }
        StartStage(spec.stage_name);
        SdkThumbnailRequest request;
        request.input_path = spec.input_path;
        request.output_path = JoinPath(output_dir_, spec.stage_name + ".jpg");
        request.thumbnail_kind = spec.kind;
        const SdkThumbnailResult result = graphic_facade_.GenerateThumbnail(request);
        const std::string output_path = result.output_path.empty() ? request.output_path : result.output_path;
        if (IsOkStatusCode(result.code) && result.generated && FileExists(output_path)) {
            AddAsset(MakeAsset(spec.output_asset_id, spec.kind, output_path, "image/jpeg", result.width, result.height, result.size));
            FinishStage(spec.stage_name, "succeeded", ProviderName(providers_graphic), "ok", {spec.input_asset_id}, {spec.output_asset_id});
            return CStatus();
        }

        const std::string message = result.message.empty() ? "thumbnail generation failed" : result.message;
        AddWarning(spec.stage_name + " fallback: " + message);
        if (!CopyFileBinary(spec.input_path, request.output_path)) {
            AddWarning(spec.stage_name + " fallback copy failed");
            FinishStage(spec.stage_name, "failed", ProviderName(providers_graphic), "fallback copy failed", {spec.input_asset_id}, {});
            return CStatus();
        }

        AddAsset(MakeAsset(spec.output_asset_id, spec.kind, request.output_path, "image/jpeg", 0, 0, FileSize(request.output_path)));
        FinishStage(spec.stage_name, "succeeded", ProviderName(providers_graphic), "fallback copy original", {spec.input_asset_id}, {spec.output_asset_id});
        return CStatus();
    }

    CapturePipelineRequest request_;
    const DeviceFacade& device_facade_;
    const GraphicFacade& graphic_facade_;
    ProviderBundle providers_;
    CaptureStageCallback stage_callback_;
    CapturePipelineResult pipeline_result_;
    std::vector<SdkCaptureAsset> assets_;
    std::vector<SdkCaptureStageResult> stages_;
    std::vector<std::string> warnings_;
    std::string output_dir_;
    std::string original_path_;
    std::string laser_path_;
    int original_width_ = 0;
    int original_height_ = 0;
    std::vector<SdkRect4P> detected_rects_;
    int detected_rects_source_width_ = 0;
    int detected_rects_source_height_ = 0;
    int scan_device_type_ = 0;
    std::vector<ProcessedOutput> page_outputs_;
    mutable std::mutex mu_;
};

class CaptureRawNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit CaptureRawNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->CaptureRaw(); }
private:
    CapturePipelineContext* context_;
};

class ThumbnailNode : public CGraph::GTemplateNode<CapturePipelineContext*, ThumbnailTarget> {
public:
    ThumbnailNode(CapturePipelineContext* context, ThumbnailTarget target) : context_(context), target_(target) {}
    CStatus run() override { return context_->RunThumbnail(target_); }
private:
    CapturePipelineContext* context_;
    ThumbnailTarget target_;
};

class PageProcessNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit PageProcessNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->PageProcess(); }
private:
    CapturePipelineContext* context_;
};

class OutputWorkflowNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit OutputWorkflowNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->ProcessOutputWorkflow(); }
private:
    CapturePipelineContext* context_;
};

class FinalizeResultNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit FinalizeResultNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->FinalizeResult(); }
private:
    CapturePipelineContext* context_;
};

} // namespace

CapturePipelineService::CapturePipelineService(const ProviderBundle& providers)
    : device_facade_(providers),
      graphic_facade_(providers),
      providers_(providers) {}

CapturePipelineResult CapturePipelineService::Run(const CapturePipelineRequest& request,
                                                  CaptureStageCallback stage_callback) const {
    CapturePipelineContext context(request, device_facade_, graphic_facade_, providers_, stage_callback);
    CGraph::GPipelinePtr pipeline = CGraph::GPipelineFactory::create();
    CGraph::GTemplateNodePtr<CapturePipelineContext*> capture = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*, ThumbnailTarget> original_thumb = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*> page = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*> output_workflow = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*> finalize = nullptr;

    pipeline->registerGElement<CaptureRawNode>(&capture, {}, &context);
    pipeline->registerGElement<ThumbnailNode>(&original_thumb, {capture}, &context, ThumbnailTarget::Original);
    pipeline->registerGElement<PageProcessNode>(&page, {original_thumb}, &context);
    pipeline->registerGElement<OutputWorkflowNode>(&output_workflow, {page}, &context);
    pipeline->registerGElement<FinalizeResultNode>(&finalize, {output_workflow}, &context);

    const CStatus status = pipeline->process();
    CGraph::GPipelineFactory::remove(pipeline);

    CapturePipelineResult result = context.Result();
    if (status.isErr() && result.status != "failed") {
        result.status = "failed";
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = status.getInfo();
    }
    return result;
}

} // namespace sdk
} // namespace editor
