#pragma once

#include <mutex>

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

std::mutex& ImageCodecMutex();

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
