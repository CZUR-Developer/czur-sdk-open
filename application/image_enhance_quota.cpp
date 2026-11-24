// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "image_enhance_quota.h"

#include "sdk_status_code.h"

namespace editor {
namespace sdk {

std::string NormalizeOnlineEnhanceCapability(const std::string& type) {
    if (type == "document_rectify_enhance") {
        return "doc_crop_enhance";
    }
    if (type == "remove_background_texture") {
        return "doc_repair";
    }
    return type;
}

bool IsOnlineEnhanceCapability(const std::string& type) {
    const std::string normalized = NormalizeOnlineEnhanceCapability(type);
    return normalized == "doc_crop_enhance" ||
           normalized == "remove_handwriting" ||
           normalized == "doc_repair" ||
           normalized == "remove_moire";
}

QuotaConsumeResult ConfirmOnlineEnhanceQuota(const ProviderBundle& providers,
                                             const OnlineEnhanceQuotaCredentials& credentials,
                                             const std::string& task_id,
                                             const std::map<std::string, int>& usage_by_capability) {
    QuotaConsumeResult result;
    if (usage_by_capability.empty()) {
        return result;
    }
    if (!providers.auth_provider) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "auth provider is not available";
        return result;
    }
    for (std::map<std::string, int>::const_iterator it = usage_by_capability.begin();
         it != usage_by_capability.end();
         ++it) {
        QuotaConsumeRequest quota_request;
        quota_request.token = credentials.online_api_key;
        quota_request.session_token = credentials.online_session_token;
        quota_request.authz_base_url = credentials.authz_base_url;
        quota_request.capability = it->first;
        quota_request.request_id = "image.enhance:" + task_id + ":" + it->first;
        quota_request.units = it->second > 0 ? it->second : 1;
        const QuotaConsumeResult quota_result = providers.auth_provider->ConsumeQuota(quota_request);
        if (!IsOkStatusCode(quota_result.code)) {
            return quota_result;
        }
        result = quota_result;
    }
    return result;
}

} // namespace sdk
} // namespace editor
