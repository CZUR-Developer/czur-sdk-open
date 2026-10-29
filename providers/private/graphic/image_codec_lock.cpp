#include "graphic/image_codec_lock.h"

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

std::mutex& ImageCodecMutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
