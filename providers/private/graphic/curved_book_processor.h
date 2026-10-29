#pragma once

#include <mutex>
#include <string>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

class CurvedBookProcessor {
public:
    CurvedBookProcessor();
    ~CurvedBookProcessor();

    SdkPageProcessResult Process(const SdkPageProcessRequest& request);

private:
    bool LoadLaserConfigIfNeeded(const SdkPageProcessRequest& request, int width, int height);

    std::string ResolveOutputDir(const SdkPageProcessRequest& request) const;
    std::string ResolveLaserConfigPath(const std::string& device_id) const;

    mutable std::mutex mu_;
    std::string laser_config_device_id_;
    int laser_config_width_ = 0;
    int laser_config_height_ = 0;
};

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
