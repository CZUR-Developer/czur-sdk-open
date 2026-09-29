// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <string>

#include "sdk_entitlement_policy.h"
#include "sdk_provider_types.h"

namespace editor {
namespace sdk {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool CheckTier(SdkAccountType actual, SdkAccountType expected, const std::string& case_name) {
    return Check(actual == expected,
                 case_name + " expected " + ToAccountTypeString(expected) +
                 " got " + ToAccountTypeString(actual));
}

bool CheckString(const std::string& actual, const std::string& expected, const std::string& case_name) {
    return Check(actual == expected,
                 case_name + " expected " + expected + " got " + actual);
}

bool TestColorModeRequiredTiers() {
    return CheckTier(RequiredTierForColorMode("no_optimize"), SdkAccountType::Trial, "default color mode") &&
           CheckTier(RequiredTierForColorMode("white_paper_seal"), SdkAccountType::Svip, "white_paper_seal") &&
           CheckTier(RequiredTierForColorMode("white_paper_stamp"), SdkAccountType::Svip, "white_paper_stamp") &&
           CheckTier(RequiredTierForColorMode("ancient"), SdkAccountType::SvipPlus, "ancient") &&
           CheckTier(RequiredTierForColorMode("ancient_book"), SdkAccountType::SvipPlus, "ancient_book") &&
           CheckTier(RequiredTierForColorMode("WHITE_PAPER_SEAL"), SdkAccountType::Svip, "color mode case fold") &&
           CheckTier(RequiredTierForColorMode("ANCIENT_BOOK"), SdkAccountType::SvipPlus, "ancient case fold");
}

bool TestImageProcessRequiredTiers() {
    SdkSinglePageOptions single_page;
    SdkCurvedBookOptions curved_book;
    bool ok = true;
    ok = CheckTier(RequiredTierForImageProcess("keep_original", "no_optimize", single_page, curved_book),
                   SdkAccountType::Trial,
                   "default image process") && ok;
    single_page.auto_rotate = true;
    ok = CheckTier(RequiredTierForImageProcess("keep_original", "no_optimize", single_page, curved_book),
                   SdkAccountType::Vip,
                   "auto_rotate requires vip") && ok;
    ok = CheckTier(RequiredTierForImageProcess("KEEP_ORIGINAL", "NO_OPTIMIZE", single_page, curved_book),
                   SdkAccountType::Vip,
                   "auto_rotate with uppercase strings") && ok;
    ok = CheckTier(RequiredTierForImageProcess("curved_book", "no_optimize", single_page, curved_book),
                   SdkAccountType::Svip,
                   "curved_book outranks auto_rotate") && ok;
    ok = CheckTier(RequiredTierForImageProcess("book", "no_optimize", single_page, curved_book),
                   SdkAccountType::Svip,
                   "book alias") && ok;
    ok = CheckTier(RequiredTierForImageProcess("curve_flatten", "no_optimize", single_page, curved_book),
                   SdkAccountType::Svip,
                   "curve_flatten alias") && ok;
    ok = CheckTier(RequiredTierForImageProcess("CURVED_BOOK", "no_optimize", single_page, curved_book),
                   SdkAccountType::Svip,
                   "curved_book case fold") && ok;
    ok = CheckTier(RequiredTierForImageProcess("curved_book", "ancient", single_page, curved_book),
                   SdkAccountType::SvipPlus,
                   "ancient outranks curved_book") && ok;
    single_page.auto_rotate = false;
    ok = CheckTier(RequiredTierForImageProcess("selected_area", "white_paper_stamp", single_page, curved_book),
                   SdkAccountType::Svip,
                   "color mode can require svip") && ok;
    return ok;
}

bool TestEnhanceRequiredTiers() {
    const char* vip[] = {"rotate", "ROTATE"};
    const char* svip[] = {"blank_page_detect", "normalize_spec", "red_green_head",
                          "white_paper_seal", "white_paper_stamp", "curved_book"};
    const char* plus[] = {"ancient", "ancient_book", "hole_fill", "punch_hole_fill",
                          "doc_crop_enhance", "document_rectify_enhance", "remove_handwriting",
                          "doc_repair", "remove_background_texture", "remove_moire"};
    bool ok = true;
    for (const char* type : vip) {
        ok = CheckTier(RequiredTierForEnhanceStep(type), SdkAccountType::Vip, type) && ok;
    }
    for (const char* type : svip) {
        ok = CheckTier(RequiredTierForEnhanceStep(type), SdkAccountType::Svip, type) && ok;
    }
    for (const char* type : plus) {
        ok = CheckTier(RequiredTierForEnhanceStep(type), SdkAccountType::SvipPlus, type) && ok;
    }
    ok = CheckTier(RequiredTierForEnhanceStep("unknown"), SdkAccountType::Trial, "fallback tier") && ok;
    SdkImageEnhancePipeline pipeline;
    ok = CheckTier(RequiredTierForEnhancePipeline(pipeline), SdkAccountType::Trial, "empty pipeline") && ok;
    SdkImageEnhanceStep step;
    step.type = "rotate";
    pipeline.steps.push_back(step);
    step.type = "ancient";
    step.enabled = false;
    pipeline.steps.push_back(step);
    ok = CheckTier(RequiredTierForEnhancePipeline(pipeline), SdkAccountType::Vip, "ignore disabled steps") && ok;
    pipeline.steps.back().enabled = true;
    ok = CheckTier(RequiredTierForEnhancePipeline(pipeline), SdkAccountType::SvipPlus, "maximum pipeline tier") && ok;
    return ok;
}

bool TestTrialQuotaCapabilityFor() {
    return CheckString(TrialQuotaCapabilityFor("image.process", SdkAccountType::Vip),
                       "image.process.vip", "image process vip bucket") &&
           CheckString(TrialQuotaCapabilityFor("image.process_page", SdkAccountType::Svip),
                       "image.process.svip", "image process_page svip bucket") &&
           CheckString(TrialQuotaCapabilityFor("image.apply_color_mode", SdkAccountType::SvipPlus),
                       "image.process.svip_plus", "image color svip plus bucket") &&
           CheckString(TrialQuotaCapabilityFor("image.enhance", SdkAccountType::Vip),
                       "image.enhance.vip", "image enhance vip bucket") &&
           CheckString(TrialQuotaCapabilityFor("image.enhance_workflow_save", SdkAccountType::SvipPlus),
                       "image.enhance.svip_plus", "image enhance workflow svip plus bucket") &&
           CheckString(TrialQuotaCapabilityFor("file.convert", SdkAccountType::Svip),
                       "file.convert.svip", "file convert svip bucket") &&
           CheckString(TrialQuotaCapabilityFor("ocr.recognize", SdkAccountType::Vip),
                       "ocr.recognize", "non image file method unchanged") &&
           CheckString(TrialQuotaCapabilityFor("image.process", SdkAccountType::Trial),
                       "image.process", "trial method unchanged");
}

AuthContext MakeAuthContext(SdkAccountType account_type,
                            SdkAccountType licensed_account_type,
                            const std::string& entitlement_state,
                            bool commercial_authorized) {
    AuthContext context;
    context.is_valid = true;
    context.account_type = account_type;
    context.licensed_account_type = licensed_account_type;
    context.entitlement_state = entitlement_state;
    context.commercial_authorized = commercial_authorized;
    return context;
}

bool TestFeatureEntitlement() {
    bool ok = true;
    AuthContext trial_vip = MakeAuthContext(SdkAccountType::Trial,
                                            SdkAccountType::Vip,
                                            "online_trial",
                                            false);
    EntitlementCheckResult result = CheckFeatureEntitlement(trial_vip,
                                                            "image.process",
                                                            SdkAccountType::Vip);
    ok = Check(result.code == ToCode(SdkStatusCode::Ok), "trial licensed vip should pass") && ok;
    ok = Check(result.requires_trial_quota, "trial licensed vip should require trial quota") && ok;

    AuthContext commercial_svip = MakeAuthContext(SdkAccountType::Trial,
                                                  SdkAccountType::SvipPlus,
                                                  "online_trial",
                                                  false);
    result = CheckFeatureEntitlement(commercial_svip,
                                     "image.process",
                                     SdkAccountType::SvipPlus);
    ok = Check(result.code == ToCode(SdkStatusCode::CapabilityNotAllowed),
               "svip_plus trial without commercial authorization should be denied") && ok;
    ok = Check(!result.requires_trial_quota,
               "denied svip_plus commercial feature must not consume trial quota") && ok;

    commercial_svip.commercial_authorized = true;
    result = CheckFeatureEntitlement(commercial_svip,
                                     "image.process",
                                     SdkAccountType::SvipPlus);
    ok = Check(result.code == ToCode(SdkStatusCode::Ok),
               "svip_plus trial with commercial authorization should pass") && ok;
    ok = Check(result.requires_trial_quota,
               "svip_plus trial with commercial authorization should consume trial quota") && ok;

    AuthContext paid_svip = MakeAuthContext(SdkAccountType::Svip,
                                            SdkAccountType::Svip,
                                            "online_paid",
                                            false);
    result = CheckFeatureEntitlement(paid_svip,
                                     "image.process",
                                     SdkAccountType::Svip);
    ok = Check(result.code == ToCode(SdkStatusCode::Ok), "paid svip should pass") && ok;
    ok = Check(!result.requires_trial_quota, "paid svip should not require trial quota") && ok;

    AuthContext paid_vip = MakeAuthContext(SdkAccountType::Vip,
                                           SdkAccountType::Vip,
                                           "online_paid",
                                           true);
    result = CheckFeatureEntitlement(paid_vip,
                                     "image.process",
                                     SdkAccountType::Svip);
    ok = Check(result.code == ToCode(SdkStatusCode::CapabilityNotAllowed),
               "paid vip should not pass svip feature") && ok;
    return ok;
}

} // namespace
} // namespace sdk
} // namespace editor

int main() {
    using namespace editor::sdk;
    const bool ok = TestColorModeRequiredTiers() &&
                    TestImageProcessRequiredTiers() &&
                    TestEnhanceRequiredTiers() &&
                    TestTrialQuotaCapabilityFor() &&
                    TestFeatureEntitlement();
    return ok ? 0 : 1;
}
