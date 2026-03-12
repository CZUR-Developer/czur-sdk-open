#pragma once

#include <string>
#include <vector>

namespace editor {
namespace sdk {

class ISdkDeviceProvider {
public:
    virtual ~ISdkDeviceProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual std::vector<std::string> ListDevices() const = 0;
};

} // namespace sdk
} // namespace editor

