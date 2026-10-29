// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "private_sdk_master_secret.h"

#include <cstdlib>

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

const char kMasterSecretEnvName[] = "CZUR_SDK_MASTER_SECRET";
const char kDefaultMasterSecret[] = "J2pVl1wJskljtAbG6rnV9RE4uEETjqEaWlcg9XnWgGI=";
const char kMasterSecretSourceEnv[] = "env";
const char kMasterSecretSourceDefault[] = "default";

const char* MasterSecretEnvValue() {
    const char* env_value = std::getenv(kMasterSecretEnvName);
    return env_value != NULL && env_value[0] != '\0' ? env_value : NULL;
}

} // namespace

std::string GetPrivateSdkMasterSecret() {
    const char* env_value = MasterSecretEnvValue();
    return env_value != NULL ? env_value : kDefaultMasterSecret;
}

std::string GetPrivateSdkMasterSecretSource() {
    return MasterSecretEnvValue() != NULL ? kMasterSecretSourceEnv : kMasterSecretSourceDefault;
}

bool IsPrivateSdkMasterSecretEnvConfigured() {
    return MasterSecretEnvValue() != NULL;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
