// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <string>

namespace editor {
namespace sdk {
namespace private_impl {

// The command protocol returns 16 printable, non-space ASCII bytes. Do not
// substitute UVC identity or cached values when the optional read fails.
template <typename Reader>
std::string ReadDeviceCommandSn(bool supported, Reader read_serial) {
    if (!supported) {
        return std::string();
    }
    try {
        const std::string serial = read_serial();
        if (serial.size() != 16) {
            return std::string();
        }
        for (std::string::const_iterator it = serial.begin(); it != serial.end(); ++it) {
            const unsigned char byte = static_cast<unsigned char>(*it);
            if (byte < 0x21 || byte > 0x7e) {
                return std::string();
            }
        }
        return serial;
    } catch (...) {
        // Device metadata is best-effort; it must not fail device.open.
        return std::string();
    }
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
