#pragma once

#include <string>

namespace editor {
namespace sdk {
namespace harmony {

bool Start(const std::string& work_dir, const std::string& web_root);
void Stop();
bool IsRunning();
std::string Status();

} // namespace harmony
} // namespace sdk
} // namespace editor
