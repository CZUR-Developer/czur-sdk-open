// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "capture_pipeline_service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <mutex>

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
        AddAsset(MakeAsset("asset-original", "original", original_path_, result.content_type.empty() ? "image/jpeg" : result.content_type, result.width, result.height, result.size));
        if (!laser_path_.empty()) {
            AddAsset(MakeAsset("asset-laser", "laser", laser_path_, "image/jpeg"));
        }
        FinishStage("capture_raw", "succeeded", ProviderName(providers_device), "ok", {}, {"asset-original"});
        return CStatus();
    }

    CStatus OriginalThumbnail() {
        if (!request_.profile.thumbnail_original) {
            return CStatus();
        }
        return GenerateThumbnail("original_thumbnail", "original_thumbnail", original_path_, "asset-thumbnail-original");
    }

    CStatus PageProcess() {
        StartStage("page_process");
        page_path_ = JoinPath(output_dir_, "page_processed.jpg");
        SdkPageProcessRequest page_request;
        page_request.input_path = original_path_;
        page_request.laser_path = laser_path_;
        page_request.output_path = page_path_;
        page_request.page_processing = request_.profile.page_processing;
        const SdkPageProcessResult result = graphic_facade_.ProcessPage(page_request);
        if (!IsOkStatusCode(result.code) || result.unsupported || !result.processed) {
            page_path_ = original_path_;
        AddWarning("page_process fallback: " + result.message);
            FinishStage("page_process", "succeeded", ProviderName(providers_graphic), "fallback to original", {"asset-original"}, {"asset-page-processed"});
            AddAsset(MakeAsset("asset-page-processed", "page_processed", page_path_, "image/jpeg"));
            return CStatus();
        }
        page_path_ = result.output_path.empty() ? page_path_ : result.output_path;
        AddAsset(MakeAsset("asset-page-processed", "page_processed", page_path_, "image/jpeg"));
        FinishStage("page_process", "succeeded", ProviderName(providers_graphic), "ok", {"asset-original"}, {"asset-page-processed"});
        return CStatus();
    }

    CStatus PageProcessedThumbnail() {
        if (!request_.profile.thumbnail_page_processed) {
            return CStatus();
        }
        return GenerateThumbnail("page_processed_thumbnail", "page_processed_thumbnail", page_path_, "asset-thumbnail-page-processed");
    }

    CStatus ColorMode() {
        StartStage("color_mode");
        color_path_ = JoinPath(output_dir_, "color_processed.jpg");
        SdkColorModeRequest color_request;
        color_request.input_path = page_path_;
        color_request.output_path = color_path_;
        color_request.color_mode = request_.profile.color_mode;
        const SdkColorModeResult result = graphic_facade_.ApplyColorMode(color_request);
        if (!IsOkStatusCode(result.code) || result.unsupported || !result.processed) {
            color_path_ = page_path_;
            AddWarning("color_mode fallback: " + result.message);
            AddAsset(MakeAsset("asset-color-processed", "color_processed", color_path_, "image/jpeg"));
            FinishStage("color_mode", "succeeded", ProviderName(providers_graphic), "fallback to page result", {"asset-page-processed"}, {"asset-color-processed"});
            return CStatus();
        }
        color_path_ = result.output_path.empty() ? color_path_ : result.output_path;
        AddAsset(MakeAsset("asset-color-processed", "color_processed", color_path_, "image/jpeg"));
        FinishStage("color_mode", "succeeded", ProviderName(providers_graphic), "ok", {"asset-page-processed"}, {"asset-color-processed"});
        return CStatus();
    }

    CStatus ColorProcessedThumbnail() {
        if (!request_.profile.thumbnail_color_processed) {
            return CStatus();
        }
        return GenerateThumbnail("color_processed_thumbnail", "color_processed_thumbnail", color_path_, "asset-thumbnail-color-processed");
    }

    CStatus FormatConvert() {
        StartStage("format_convert");
        final_path_ = JoinPath(output_dir_, "capture_final" + ExtensionForFormat(request_.profile.output_format));
        if (HasExtension(color_path_, request_.profile.output_format)) {
            final_path_ = color_path_;
            AddAsset(MakeAsset("asset-final", "final", final_path_, ContentTypeForFormat(request_.profile.output_format)));
            FinishStage("format_convert", "succeeded", ProviderName(providers_graphic), "passthrough", {"asset-color-processed"}, {"asset-final"});
            return CStatus();
        }

        SdkFormatConvertRequest convert_request;
        convert_request.input_path = color_path_;
        convert_request.output_path = final_path_;
        convert_request.output_format = request_.profile.output_format;
        const SdkFormatConvertResult result = graphic_facade_.ConvertImageFormat(convert_request);
        if (!IsOkStatusCode(result.code) || (!result.converted && !result.passthrough)) {
            FinishStage("format_convert", "failed", ProviderName(providers_graphic), result.message, {"asset-color-processed"}, {});
            pipeline_result_.code = result.code;
            pipeline_result_.message = result.message.empty() ? "format convert failed" : result.message;
            pipeline_result_.status = "failed";
            return CStatus(pipeline_result_.message);
        }
        final_path_ = result.output_path.empty() ? final_path_ : result.output_path;
        AddAsset(MakeAsset("asset-final", "final", final_path_, ContentTypeForFormat(request_.profile.output_format)));
        FinishStage("format_convert", "succeeded", ProviderName(providers_graphic), "ok", {"asset-color-processed"}, {"asset-final"});
        return CStatus();
    }

    CStatus FinalizeResult() {
        StartStage("finalize_result");
        FinishStage("finalize_result", "succeeded", "sdk-open", "ok", {"asset-final"}, {});
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

    CStatus GenerateThumbnail(const std::string& stage_name,
                                      const std::string& kind,
                                      const std::string& input_path,
                                      const std::string& asset_id) {
        StartStage(stage_name);
        SdkThumbnailRequest request;
        request.input_path = input_path;
        request.output_path = JoinPath(output_dir_, stage_name + ".jpg");
        request.thumbnail_kind = kind;
        const SdkThumbnailResult result = graphic_facade_.GenerateThumbnail(request);
        if (!IsOkStatusCode(result.code) || !result.generated) {
            AddWarning(stage_name + " failed: " + result.message);
            FinishStage(stage_name, "failed", ProviderName(providers_graphic), result.message, {}, {});
            return CStatus();
        }
        AddAsset(MakeAsset(asset_id, kind, result.output_path.empty() ? request.output_path : result.output_path, "image/jpeg", result.width, result.height, result.size));
        FinishStage(stage_name, "succeeded", ProviderName(providers_graphic), "ok", {}, {asset_id});
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
    std::string page_path_;
    std::string color_path_;
    std::string final_path_;
    mutable std::mutex mu_;
};

class CaptureRawNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit CaptureRawNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->CaptureRaw(); }
private:
    CapturePipelineContext* context_;
};

class OriginalThumbnailNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit OriginalThumbnailNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->OriginalThumbnail(); }
private:
    CapturePipelineContext* context_;
};

class PageProcessNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit PageProcessNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->PageProcess(); }
private:
    CapturePipelineContext* context_;
};

class PageProcessedThumbnailNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit PageProcessedThumbnailNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->PageProcessedThumbnail(); }
private:
    CapturePipelineContext* context_;
};

class ColorModeNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit ColorModeNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->ColorMode(); }
private:
    CapturePipelineContext* context_;
};

class ColorProcessedThumbnailNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit ColorProcessedThumbnailNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->ColorProcessedThumbnail(); }
private:
    CapturePipelineContext* context_;
};

class FormatConvertNode : public CGraph::GTemplateNode<CapturePipelineContext*> {
public:
    explicit FormatConvertNode(CapturePipelineContext* context) : context_(context) {}
    CStatus run() override { return context_->FormatConvert(); }
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
    CGraph::GTemplateNodePtr<CapturePipelineContext*> original_thumb = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*> page = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*> page_thumb = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*> color = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*> color_thumb = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*> convert = nullptr;
    CGraph::GTemplateNodePtr<CapturePipelineContext*> finalize = nullptr;

    pipeline->registerGElement<CaptureRawNode>(&capture, {}, &context);
    pipeline->registerGElement<OriginalThumbnailNode>(&original_thumb, {capture}, &context);
    pipeline->registerGElement<PageProcessNode>(&page, {capture}, &context);
    pipeline->registerGElement<PageProcessedThumbnailNode>(&page_thumb, {page}, &context);
    pipeline->registerGElement<ColorModeNode>(&color, {page}, &context);
    pipeline->registerGElement<ColorProcessedThumbnailNode>(&color_thumb, {color}, &context);
    pipeline->registerGElement<FormatConvertNode>(&convert, {color}, &context);
    pipeline->registerGElement<FinalizeResultNode>(&finalize, {convert, original_thumb, page_thumb, color_thumb}, &context);

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
