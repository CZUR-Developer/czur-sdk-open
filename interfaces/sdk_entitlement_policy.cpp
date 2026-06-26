// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_entitlement_policy.h"

#include <sstream>

namespace editor {
namespace sdk {

namespace {

bool IsKnownTier(SdkAccountType account_type) {
    return account_type == SdkAccountType::Trial || account_type == SdkAccountType::Vip ||
           account_type == SdkAccountType::Svip || account_type == SdkAccountType::SvipPlus;
}

void PushAlwaysSupportedCapabilities(std::vector<std::string>* capabilities) {
    capabilities->push_back("system.ping");
    capabilities->push_back("system.info");
    capabilities->push_back("system.capabilities");
    capabilities->push_back("auth.create_session");
    capabilities->push_back("auth.get_context");
    capabilities->push_back("auth.refresh_session");
    capabilities->push_back("auth.destroy_session");
    capabilities->push_back("auth.activate_offline");
    capabilities->push_back("device.list");
    capabilities->push_back("device.get");
    capabilities->push_back("device.open");
    capabilities->push_back("device.close");
    capabilities->push_back("capture.take");
    capabilities->push_back("capture.get");
    capabilities->push_back("capture.set_turn_detect");
    capabilities->push_back("video.start");
    capabilities->push_back("video.stop");
    capabilities->push_back("video.set_format");
    capabilities->push_back("video.set_profile");
    capabilities->push_back("image.process");
    capabilities->push_back("image.process_page");
    capabilities->push_back("image.apply_color_mode");
    capabilities->push_back("image.enhance_capabilities");
    capabilities->push_back("image.enhance");
    capabilities->push_back("image.enhance_get");
    capabilities->push_back("image.enhance_cancel");
    capabilities->push_back("image.enhance_workflow_list");
    capabilities->push_back("image.enhance_workflow_get");
    capabilities->push_back("image.enhance_workflow_save");
    capabilities->push_back("image.enhance_workflow_delete");
    capabilities->push_back("file.convert");
    capabilities->push_back("recognition.barcode_detect");
    capabilities->push_back("sane.status");
    capabilities->push_back("sane.list");
    capabilities->push_back("sane.watch_start");
    capabilities->push_back("sane.watch_stop");
    capabilities->push_back("sane.open");
    capabilities->push_back("sane.close");
    capabilities->push_back("sane.get_options");
    capabilities->push_back("sane.set_options");
    capabilities->push_back("sane.profile_list");
    capabilities->push_back("sane.profile_save");
    capabilities->push_back("sane.profile_apply");
    capabilities->push_back("sane.profile_delete");
    capabilities->push_back("sane.scan");
    capabilities->push_back("sane.scan_get");
    capabilities->push_back("sane.scan_cancel");
}

} // namespace

int EntitlementRank(SdkAccountType account_type) {
    switch (account_type) {
        case SdkAccountType::Trial:
            return 0;
        case SdkAccountType::Vip:
            return 1;
        case SdkAccountType::Svip:
            return 2;
        case SdkAccountType::SvipPlus:
            return 3;
        case SdkAccountType::Custom:
            return 4;
        case SdkAccountType::Unknown:
        default:
            return -1;
    }
}

bool IsEntitlementAtLeast(SdkAccountType current, SdkAccountType required) {
    return EntitlementRank(current) >= EntitlementRank(required);
}

std::vector<std::string> BuildEntitledCapabilities(SdkAccountType account_type) {
    std::vector<std::string> capabilities;
    PushAlwaysSupportedCapabilities(&capabilities);
    if (IsKnownTier(account_type)) {
        capabilities.push_back("ocr.recognize");
        capabilities.push_back("ocr.get");
        capabilities.push_back("ocr.cancel");
        capabilities.push_back("ocr.extract_text");
    }
    return capabilities;
}

bool IsTrialEntitlementState(const AuthContext& auth_context) {
    return auth_context.account_type == SdkAccountType::Trial ||
           auth_context.entitlement_state == "offline_trial" ||
           auth_context.entitlement_state == "online_trial";
}

bool CapabilityRequiresTrialVipQuota(const AuthContext& auth_context, const std::string& capability) {
    if (!IsTrialEntitlementState(auth_context)) {
        return false;
    }
    const std::string bucket = CapabilityToEntitlementQuotaBucket(capability);
    return bucket == "trial_vip";
}

std::string CapabilityToEntitlementQuotaBucket(const std::string& capability) {
    if (capability == "trial_vip" ||
        capability == "ocr.recognize" || capability == "ocr.extract_text" ||
        capability == "image.process.vip" || capability == "image.process.svip" ||
        capability == "image.process.svip_plus" || capability == "image.enhance.vip" ||
        capability == "image.enhance.svip" || capability == "image.enhance.svip_plus" ||
        capability == "file.convert.vip" || capability == "file.convert.svip" ||
        capability == "file.convert.svip_plus") {
        return "trial_vip";
    }
    if (capability == "capture.take") {
        return "capture";
    }
    if (capability == "image.process" || capability == "image.process_page" ||
        capability == "image.apply_color_mode" || capability == "image.enhance" ||
        capability == "image.enhance_get" || capability == "image.enhance_cancel" ||
        capability == "image.enhance_capabilities" || capability == "image.enhance_workflow_list" ||
        capability == "image.enhance_workflow_get" || capability == "image.enhance_workflow_save" ||
        capability == "image.enhance_workflow_delete" || capability == "doc_crop_enhance" ||
        capability == "remove_handwriting" || capability == "doc_repair" ||
        capability == "remove_moire") {
        return "image";
    }
    if (capability == "file.convert") {
        return "file";
    }
    return "";
}

EntitlementCheckResult CheckFeatureEntitlement(const AuthContext& auth_context,
                                               const std::string& feature,
                                               SdkAccountType required_account_type) {
    EntitlementCheckResult result;
    result.feature = feature;
    result.required_account_type = required_account_type;
    if (!IsKnownTier(required_account_type) || required_account_type == SdkAccountType::Trial) {
        return result;
    }
    if (IsEntitlementAtLeast(auth_context.account_type, required_account_type)) {
        return result;
    }
    if (IsTrialEntitlementState(auth_context) &&
        IsEntitlementAtLeast(auth_context.licensed_account_type, required_account_type)) {
        result.requires_trial_quota = true;
        return result;
    }
    result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
    std::ostringstream oss;
    oss << "capability not allowed: " << feature
        << " requires " << ToAccountTypeString(required_account_type)
        << ", current " << ToAccountTypeString(auth_context.account_type);
    result.message = oss.str();
    return result;
}

} // namespace sdk
} // namespace editor
