#include "sdk_open_harmony_host.h"

#include <cstdlib>
#include <memory>
#include <mutex>

#include "mock_provider_factory.h"
#include "private_graphic_provider.h"
#include "private_provider_factory.h"
#include "private_recognition_provider.h"
#include "sdk_app.h"
#include "sdk_config.h"
#include "sdk_json_utils.h"
#include "sdk_logger.h"
#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {
namespace harmony {

namespace {
std::mutex g_mutex;
std::unique_ptr<SdkApp> g_app;

void SetEnvironment(const char* name, const std::string& value) {
    if (!value.empty()) {
        ::setenv(name, value.c_str(), 1);
    }
}
}

bool Start(const std::string& work_dir, const std::string& web_root) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_app && g_app->IsRunning()) {
        return true;
    }

    SetEnvironment("SDK_OPEN_WORK_DIR", work_dir);
    SetEnvironment("SDK_OPEN_LOG_DIR", JoinPath(work_dir, "logs"));
    SetEnvironment("SDK_GRAPHIC_MODEL_DIR", JoinPath(work_dir, "czcv_model"));
    // CZOCR appends license.lic directly to its model directory argument.
    const std::string ocr_model_dir = JoinPath(work_dir, "models");
    SetEnvironment("SDK_OCR_MODEL_DIR", ocr_model_dir + "/");
    SetEnvironment("SDK_OCR_FONT_DIR", JoinPath(work_dir, "ocr_fonts"));
    SetEnvironment("SDK_GRAPHIC_LOG_PATH", JoinPath(JoinPath(work_dir, "logs"), "czcv_sdk.log"));
    InitializeSdkOpenLogger();

    SdkConfig config;
    config.bind_host = "127.0.0.1";
    config.admin_http_port = 17080;
    config.demo_http_port = 17081;
    config.asset_http_port = 17082;
    config.command_ws_port = 17090;
    config.video_ws_port = 17091;
    config.web_root = web_root;
    config.auth_token.clear();
    config.tls.enabled = false;

    ProviderBundle providers = mock::CreateProviderBundle();
    const ProviderBundle real_providers = private_impl::CreateProviderBundle(false);
    providers.auth_provider = real_providers.auth_provider;
    providers.image_enhance_provider = real_providers.image_enhance_provider;
    providers.ocr_provider = real_providers.ocr_provider;
    providers.ofd_provider = real_providers.ofd_provider;
    providers.graphic_provider = std::make_shared<private_impl::PrivateGraphicProvider>();
    providers.recognition_provider = std::make_shared<private_impl::PrivateRecognitionProvider>();
    // HarmonyOS does not provide the desktop SANE/TWAIN backends.  A null
    // provider makes the protocol return ProviderUnavailable instead of
    // exposing mock scanners as if hardware were present.
    providers.sane_provider.reset();
    providers.twain_provider.reset();
    std::unique_ptr<SdkApp> app(new SdkApp(config, std::move(providers)));
    if (!app->Start()) {
        SDK_OPEN_LOG_ERROR("[sdk_open_harmony] failed to start embedded runtime");
        return false;
    }
    g_app = std::move(app);
    SDK_OPEN_LOG_INFO("[sdk_open_harmony] embedded runtime started, demo=http://127.0.0.1:{}", config.demo_http_port);
    return true;
}

void Stop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_app) {
        g_app->Stop();
        g_app.reset();
    }
}

bool IsRunning() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_app && g_app->IsRunning();
}

std::string Status() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return DumpJson(Json{{"running", g_app && g_app->IsRunning()}, {"demoUrl", "http://127.0.0.1:17081/"}});
}

} // namespace harmony
} // namespace sdk
} // namespace editor
