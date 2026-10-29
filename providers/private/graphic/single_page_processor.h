#pragma once

#include <mutex>
#include <string>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

class SinglePageProcessor {
public:
    SinglePageProcessor();
    ~SinglePageProcessor();

    SdkPageProcessResult Process(const SdkPageProcessRequest& request);

private:
    mutable std::mutex mu_;
};

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
