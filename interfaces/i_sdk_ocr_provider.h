#pragma once

#include <string>
#include <vector>

namespace editor {
namespace sdk {

class ISdkOcrProvider {
public:
    virtual ~ISdkOcrProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual std::string SubmitTask(const std::vector<std::string>& input_files, const std::string& output_path) = 0;
};

} // namespace sdk
} // namespace editor

