#include "private_provider_factory.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool EnsureDirectory(const std::string& path) {
    return ::mkdir(path.c_str(), 0700) == 0 || errno == EEXIST;
}

} // namespace

int main() {
    const std::string work_dir = "/tmp/czur-sdk-private-provider-bundle-" + std::to_string(::getpid());
    const std::string log_dir = work_dir + "/logs";
    const std::string sane_config_dir = work_dir + "/sane/config";
    ::setenv("SDK_OPEN_WORK_DIR", work_dir.c_str(), 1);
    ::setenv("CZUR_SANE_CONFIG_DIR", sane_config_dir.c_str(), 1);

    editor::sdk::ProviderBundle bundle = editor::sdk::private_impl::CreateProviderBundle(true);
    if (!bundle.sane_provider) {
        std::cerr << "sane provider was not created" << std::endl;
        return 1;
    }
    if (!bundle.storage_provider) {
        std::cerr << "storage provider was not created" << std::endl;
        return 1;
    }

    const std::string capture_dir = work_dir + "/capture";
    const std::string tasks_dir = work_dir + "/tasks";
    if (!EnsureDirectory(work_dir) ||
        !EnsureDirectory(capture_dir) ||
        !EnsureDirectory(tasks_dir)) {
        std::cerr << "failed to create storage cleanup fixture" << std::endl;
        return 1;
    }
    std::ofstream(capture_dir + "/capture.tmp") << "capture";
    std::ofstream(tasks_dir + "/task.tmp") << "task";

    editor::sdk::SdkStorageCleanupRequest request;
    request.work_dir = work_dir;
    const editor::sdk::SdkStorageCleanupResult result = bundle.storage_provider->CleanupTemp(request);
    if (!editor::sdk::IsOkStatusCode(result.code) ||
        !result.capture_removed || !result.tasks_removed ||
        ::access(capture_dir.c_str(), F_OK) == 0 ||
        ::access(tasks_dir.c_str(), F_OK) == 0) {
        std::cerr << "storage cleanup failed: " << result.code << " " << result.message << std::endl;
        return 1;
    }
    const editor::sdk::SdkStorageCleanupResult repeated = bundle.storage_provider->CleanupTemp(request);
    if (!editor::sdk::IsOkStatusCode(repeated.code) ||
        repeated.capture_removed || repeated.tasks_removed) {
        std::cerr << "repeated storage cleanup was not idempotent: "
                  << repeated.code << " " << repeated.message << std::endl;
        return 1;
    }
    bundle = editor::sdk::ProviderBundle();
    ::unlink((log_dir + "/sdk_private_main.log").c_str());
    ::unlink((log_dir + "/sdk_private_devices.log").c_str());
    ::rmdir(log_dir.c_str());
    if (::rmdir(work_dir.c_str()) != 0) {
        std::cerr << "failed to remove storage cleanup fixture" << std::endl;
        return 1;
    }
    return 0;
}
