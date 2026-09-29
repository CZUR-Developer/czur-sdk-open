// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "sdk_auth_types.h"
#include "sdk_status_code.h"

namespace editor {
namespace sdk {

struct SdkSinglePageOptions;
struct SdkCurvedBookOptions;
struct SdkImageEnhancePipeline;

struct EntitlementCheckResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string feature;
    SdkAccountType required_account_type = SdkAccountType::Trial;
    bool requires_trial_quota = false;
};

int EntitlementRank(SdkAccountType account_type);
bool IsEntitlementAtLeast(SdkAccountType current, SdkAccountType required);
std::vector<std::string> BuildEntitledCapabilities(SdkAccountType account_type);
bool IsTrialEntitlementState(const AuthContext& auth_context);
bool CapabilityRequiresTrialVipQuota(const AuthContext& auth_context, const std::string& capability);
std::string CapabilityToEntitlementQuotaBucket(const std::string& capability);
SdkAccountType RequiredTierForColorMode(const std::string& color_mode);
SdkAccountType RequiredTierForImageProcess(const std::string& page_processing,
                                           const std::string& color_mode,
                                           const SdkSinglePageOptions& single_page,
                                           const SdkCurvedBookOptions& curved_book);
SdkAccountType RequiredTierForEnhanceStep(const std::string& step_type);
SdkAccountType RequiredTierForEnhancePipeline(const SdkImageEnhancePipeline& pipeline);
std::string TrialQuotaCapabilityFor(const std::string& method, SdkAccountType required);
EntitlementCheckResult CheckFeatureEntitlement(const AuthContext& auth_context,
                                               const std::string& feature,
                                               SdkAccountType required_account_type);

} // namespace sdk
} // namespace editor
