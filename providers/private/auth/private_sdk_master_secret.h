// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <string>

namespace editor {
namespace sdk {
namespace private_impl {

std::string GetPrivateSdkMasterSecret();
std::string GetPrivateSdkMasterSecretSource();
bool IsPrivateSdkMasterSecretEnvConfigured();

} // namespace private_impl
} // namespace sdk
} // namespace editor
