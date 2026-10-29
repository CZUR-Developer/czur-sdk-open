// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "image_write_utils.h"
#include "opencv2/imgcodecs.hpp"
#include "private_image_enhance_provider.h"

namespace {

void SetEnvValue(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value == NULL ? "" : value);
#else
    if (value == NULL) {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

const editor::sdk::SdkImageEnhanceCapability* FindCapability(
    const editor::sdk::SdkImageEnhanceCapabilityResult& result,
    const std::string& type) {
    for (std::vector<editor::sdk::SdkImageEnhanceCapability>::const_iterator it = result.capabilities.begin();
         it != result.capabilities.end();
         ++it) {
        if (it->type == type) {
            return &(*it);
        }
    }
    return NULL;
}

int ExpectOnlineCapability(const editor::sdk::SdkImageEnhanceCapabilityResult& result,
                           const std::string& type,
                           bool available) {
    const editor::sdk::SdkImageEnhanceCapability* capability = FindCapability(result, type);
    if (capability == NULL) {
        std::cerr << "missing online capability: " << type << std::endl;
        return 1;
    }
    if (capability->runtime != "online") {
        std::cerr << "capability should be online: " << type << std::endl;
        return 1;
    }
    if (capability->requires_capability != "image.enhance.online") {
        std::cerr << "capability should require image.enhance.online: " << type << std::endl;
        return 1;
    }
    if (capability->available != available) {
        std::cerr << "unexpected availability for " << type << ": " << capability->available << std::endl;
        return 1;
    }
    return 0;
}

std::string TempPath(const std::string& label, const std::string& extension) {
    return "/tmp/" + label + "-" + std::to_string(::getpid()) + extension;
}

void RemoveFile(const std::string& path) {
    if (!path.empty()) {
        ::remove(path.c_str());
    }
}

int ExpectStepOutputDpi(editor::sdk::private_impl::PrivateImageEnhanceProvider& provider,
                        const editor::sdk::SdkImageEnhanceStep& step,
                        const std::string& input_path,
                        const std::string& output_dir,
                        int expected_dpi) {
    editor::sdk::SdkImageEnhanceStepRequest request;
    request.output_dir = output_dir;
    request.step = step;
    editor::sdk::SdkImageEnhancePage page;
    page.source_index = 1;
    page.output_index = 1;
    page.path = input_path;
    request.pages.push_back(page);

    const editor::sdk::SdkImageEnhanceStepResult result = provider.RunStep(request);
    if (result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        result.pages.size() != 1) {
        std::cerr << "step failed: " << step.type
                  << " code=" << result.code
                  << " message=" << result.message << std::endl;
        return 1;
    }

    const int actual_dpi = editor::sdk::private_impl::ReadImageDpiFromMetadata(result.pages[0].path);
    if (actual_dpi != expected_dpi) {
        std::cerr << "unexpected step dpi for " << step.type
                  << ": got " << actual_dpi
                  << ", expected " << expected_dpi
                  << " output=" << result.pages[0].path << std::endl;
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    const char* original_api_key_env = std::getenv("CZUR_SDK_IMAGE_ENHANCE_API_KEY");
    const char* original_base_url_env = std::getenv("CZUR_SDK_IMAGE_ENHANCE_BASE_URL");
    const std::string original_api_key = original_api_key_env == NULL ? std::string() : std::string(original_api_key_env);
    const std::string original_base_url = original_base_url_env == NULL ? std::string() : std::string(original_base_url_env);

    SetEnvValue("CZUR_SDK_IMAGE_ENHANCE_API_KEY", NULL);
    SetEnvValue("CZUR_SDK_IMAGE_ENHANCE_BASE_URL", NULL);

    editor::sdk::private_impl::PrivateImageEnhanceProvider provider;
    const editor::sdk::SdkImageEnhanceCapabilityResult unconfigured = provider.ListCapabilities();
    if (ExpectOnlineCapability(unconfigured, "doc_crop_enhance", false) != 0 ||
        ExpectOnlineCapability(unconfigured, "remove_handwriting", false) != 0 ||
        ExpectOnlineCapability(unconfigured, "doc_repair", false) != 0 ||
        ExpectOnlineCapability(unconfigured, "remove_moire", false) != 0) {
        return 1;
    }

    SetEnvValue("CZUR_SDK_IMAGE_ENHANCE_API_KEY", "sk-test");
    const editor::sdk::SdkImageEnhanceCapabilityResult configured = provider.ListCapabilities();
    if (ExpectOnlineCapability(configured, "doc_crop_enhance", false) != 0 ||
        ExpectOnlineCapability(configured, "remove_handwriting", false) != 0 ||
        ExpectOnlineCapability(configured, "doc_repair", false) != 0 ||
        ExpectOnlineCapability(configured, "remove_moire", false) != 0) {
        return 1;
    }

    if (FindCapability(configured, "document_rectify_enhance") != NULL ||
        FindCapability(configured, "remove_background_texture") != NULL) {
        std::cerr << "legacy aliases should not be exposed as public capabilities" << std::endl;
        return 1;
    }

    const cv::Mat sample(64, 96, CV_8UC3, cv::Scalar(40, 120, 200));
    const int expected_dpi = 320;
    const std::string rotate_input = TempPath("sdk-private-enhance-rotate-input", ".jpg");
    const std::string crop_input = TempPath("sdk-private-enhance-crop-input", ".png");
    const std::string red_green_input = TempPath("sdk-private-enhance-red-green-input", ".tiff");
    const std::string rotate_output_dir = TempPath("sdk-private-enhance-rotate-out", "");
    const std::string crop_output_dir = TempPath("sdk-private-enhance-crop-out", "");
    const std::string red_green_output_dir = TempPath("sdk-private-enhance-red-green-out", "");
    ::mkdir(rotate_output_dir.c_str(), 0755);
    ::mkdir(crop_output_dir.c_str(), 0755);
    ::mkdir(red_green_output_dir.c_str(), 0755);
    if (!editor::sdk::private_impl::WriteImageWithDpiLocked(rotate_input, sample, expected_dpi) ||
        !editor::sdk::private_impl::WriteImageWithDpiLocked(crop_input, sample, expected_dpi) ||
        !editor::sdk::private_impl::WriteImageWithDpiLocked(red_green_input, sample, expected_dpi)) {
        std::cerr << "failed to prepare dpi-tagged enhance inputs" << std::endl;
        RemoveFile(rotate_input);
        RemoveFile(crop_input);
        RemoveFile(red_green_input);
        return 1;
    }

    editor::sdk::SdkImageEnhanceStep rotate_step;
    rotate_step.type = "rotate";
    rotate_step.params_json = R"({"mode":"manual","angle":90,"deskew":false})";
    if (ExpectStepOutputDpi(provider, rotate_step, rotate_input, rotate_output_dir, expected_dpi) != 0) {
        return 1;
    }

    editor::sdk::SdkImageEnhanceStep crop_step;
    crop_step.type = "crop_enhance";
    crop_step.params_json = R"({"mode":"padding","top_percent":2,"bottom_percent":2,"left_percent":2,"right_percent":2})";
    if (ExpectStepOutputDpi(provider, crop_step, crop_input, crop_output_dir, expected_dpi) != 0) {
        return 1;
    }

    editor::sdk::SdkImageEnhanceStep red_green_step;
    red_green_step.type = "red_green_head";
    red_green_step.params_json =
        R"({"auto_flag":true,"rois":[],"stroke_width":0,"blur_level":0,"color_thresh":50,"texture_degree":1.2,"specify_color":false,"src_color":[0,0,0],"target_color":[67,67,222]})";
    if (ExpectStepOutputDpi(provider, red_green_step, red_green_input, red_green_output_dir, expected_dpi) != 0) {
        return 1;
    }

    SetEnvValue("CZUR_SDK_IMAGE_ENHANCE_API_KEY", "sk-env-should-not-be-used");
    editor::sdk::SdkImageEnhanceStepRequest env_key_ignored;
    env_key_ignored.output_dir = "/tmp/czur-sdk-image-enhance-smoke/env-key-ignored";
    env_key_ignored.step.type = "doc_crop_enhance";
    editor::sdk::SdkImageEnhancePage env_key_page;
    env_key_page.source_index = 1;
    env_key_page.output_index = 1;
    env_key_page.path = "missing.jpg";
    env_key_ignored.pages.push_back(env_key_page);
    const editor::sdk::SdkImageEnhanceStepResult env_key_result = provider.RunStep(env_key_ignored);
    if (env_key_result.message != "online image enhance api key is not configured") {
        std::cerr << "CZUR_SDK_IMAGE_ENHANCE_API_KEY must not be used as the online enhance X-Api-Key source"
                  << std::endl;
        return 1;
    }

    SetEnvValue("CZUR_SDK_IMAGE_ENHANCE_API_KEY", NULL);
    editor::sdk::SdkImageEnhanceStepRequest request_key_override;
    request_key_override.output_dir = "/tmp/czur-sdk-image-enhance-smoke/request-key-override";
    request_key_override.online_api_key = "sk-request-key";
    request_key_override.online_base_url = "invalid-base-url";
    request_key_override.step.type = "doc_crop_enhance";
    editor::sdk::SdkImageEnhancePage request_key_page;
    request_key_page.source_index = 1;
    request_key_page.output_index = 1;
    request_key_page.path = "missing.jpg";
    request_key_override.pages.push_back(request_key_page);
    const editor::sdk::SdkImageEnhanceStepResult request_key_result = provider.RunStep(request_key_override);
    if (request_key_result.message == "online image enhance api key is not configured") {
        std::cerr << "request-scoped online api key should be accepted before env fallback" << std::endl;
        return 1;
    }

    const char* integration_input_env = std::getenv("CZUR_SDK_IMAGE_ENHANCE_INTEGRATION_INPUT");
    if (integration_input_env != NULL && integration_input_env[0] != '\0') {
        if (original_api_key.empty()) {
            std::cerr << "CZUR_SDK_IMAGE_ENHANCE_API_KEY required for integration run" << std::endl;
            return 1;
        }
        SetEnvValue("CZUR_SDK_IMAGE_ENHANCE_API_KEY", original_api_key.c_str());
        if (!original_base_url.empty()) {
            SetEnvValue("CZUR_SDK_IMAGE_ENHANCE_BASE_URL", original_base_url.c_str());
        }

        const char* integration_output_env = std::getenv("CZUR_SDK_IMAGE_ENHANCE_INTEGRATION_OUTPUT_DIR");
        const std::string output_dir = integration_output_env == NULL || integration_output_env[0] == '\0'
                                           ? "/tmp/czur-sdk-image-enhance-smoke"
                                           : std::string(integration_output_env);
        const std::vector<std::string> capabilities = {
            "doc_crop_enhance",
            "remove_handwriting",
            "doc_repair",
            "remove_moire",
        };
        for (std::size_t i = 0; i < capabilities.size(); ++i) {
            editor::sdk::SdkImageEnhanceStepRequest request;
            request.output_dir = output_dir + "/" + capabilities[i];
            request.online_api_key = original_api_key;
            request.step.type = capabilities[i];
            editor::sdk::SdkImageEnhancePage page;
            page.source_index = 1;
            page.output_index = 1;
            page.path = integration_input_env;
            request.pages.push_back(page);

            const editor::sdk::SdkImageEnhanceStepResult result = provider.RunStep(request);
            if (result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
                result.pages.size() != 1) {
                std::cerr << "integration run failed for " << capabilities[i]
                          << ": code=" << result.code
                          << " message=" << result.message << std::endl;
                return 1;
            }
            std::cout << capabilities[i] << " output=" << result.pages[0].path << std::endl;
        }
    }

    return 0;
}
