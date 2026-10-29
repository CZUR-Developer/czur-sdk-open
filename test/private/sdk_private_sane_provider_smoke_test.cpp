#include "private_sane_provider.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

int main() {
    const std::string work_dir = "/tmp/czur-sdk-private-sane-provider-" + std::to_string(::getpid());
    const std::string sane_config_dir = work_dir + "/sane/config";
    ::setenv("SDK_OPEN_WORK_DIR", work_dir.c_str(), 1);
    ::setenv("CZUR_SANE_CONFIG_DIR", sane_config_dir.c_str(), 1);

    {
        editor::sdk::private_impl::PrivateSaneProvider provider;
        const editor::sdk::SdkSaneStatusResult status = provider.GetStatus();
        if (!status.available) {
            std::cerr << "SANE provider is unavailable: " << status.message << std::endl;
            return 1;
        }
    }
    return 0;
}
