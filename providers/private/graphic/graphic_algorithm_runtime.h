#pragma once

#include <mutex>
#include <string>

#include "sdk_status_code.h"

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

class GraphicAlgorithmRuntime {
public:
    static GraphicAlgorithmRuntime& Instance();

    bool EnsureInitialized(SdkStatusCode& code, std::string& message);
    std::mutex& AlgorithmMutex();

private:
    GraphicAlgorithmRuntime() = default;

    std::string ResolveRawModelDir() const;
    std::string ResolveModelDir() const;
    std::string ResolveCzcvLogPath() const;

    std::mutex init_mu_;
    std::mutex algorithm_mu_;
    bool initialized_ = false;
};

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
