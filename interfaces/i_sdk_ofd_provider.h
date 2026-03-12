#pragma once

#include <string>

namespace editor {
namespace sdk {

class ISdkOfdProvider {
public:
    virtual ~ISdkOfdProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual bool OpenDocument(const std::string& path) = 0;
};

} // namespace sdk
} // namespace editor

