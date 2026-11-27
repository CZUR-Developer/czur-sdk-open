// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <string>

#include "sdk_auth_types.h"
#include "sdk_provider_bundle.h"

namespace editor {
namespace sdk {

struct OnlineEnhanceQuotaCredentials {
    std::string online_api_key;
    std::string online_session_token;
    std::string authz_base_url;
};

std::string NormalizeOnlineEnhanceCapability(const std::string& type);
bool IsOnlineEnhanceCapability(const std::string& type);
QuotaConsumeResult ConfirmOnlineEnhanceQuota(const ProviderBundle& providers,
                                             const OnlineEnhanceQuotaCredentials& credentials,
                                             const std::string& task_id,
                                             const std::map<std::string, int>& usage_by_capability);

} // namespace sdk
} // namespace editor
