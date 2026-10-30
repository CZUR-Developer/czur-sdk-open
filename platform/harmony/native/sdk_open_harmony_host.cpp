#include "sdk_open_harmony_host.h"

#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>

#include "device_manager.h"
#include "mock_provider_factory.h"
#include "private_graphic_provider.h"
#include "private_provider_factory.h"
#include "private_recognition_provider.h"
#include "sdk_app.h"
#include "sdk_config.h"
#include "sdk_json_utils.h"
#include "sdk_logger.h"
#include "sdk_runtime_paths.h"
#include "sdk_tls_certificate_manager.h"

namespace editor {
namespace sdk {
namespace harmony {

namespace {
std::mutex g_mutex;
std::unique_ptr<SdkApp> g_app;
std::atomic<bool> g_running(false);

void SetEnvironment(const char* name, const std::string& value) {
    if (!value.empty()) {
        ::setenv(name, value.c_str(), 1);
    }
}
}

bool Start(const std::string& work_dir, const std::string& web_root) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_app) {
        // Stop publishes running=false before taking this mutex so the ArkTS
        // status call remains nonblocking. Do not interpret that early state
        // transition as permission to construct a second runtime.
        if (g_app->IsRunning()) {
            g_running.store(true);
            return true;
        }
        g_app->Stop();
        g_app.reset();
        editor::devices::DeviceManager::GetInstance()->release();
    }
    g_running.store(false);

    SetEnvironment("SDK_OPEN_WORK_DIR", work_dir);
    SetEnvironment("SDK_OPEN_LOG_DIR", JoinPath(work_dir, "logs"));
    SetEnvironment("SDK_GRAPHIC_MODEL_DIR", JoinPath(work_dir, "czcv_model"));
    // CZOCR appends license.lic directly to its model directory argument.
    SetEnvironment("SDK_OCR_MODEL_DIR", JoinPath(work_dir, "models") + "/");
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
#if SDK_OPEN_ENABLE_TLS
    config.tls.enabled = true;
    config.tls.bind_host = config.bind_host;
    config.tls.demo_https_port = 18081;
    config.tls.certificate_chain_file = JoinPath(work_dir, "tls/sdk-runtime.localhost.fullchain.pem");
    config.tls.private_key_file = JoinPath(work_dir, "tls/sdk-runtime.localhost.key.pem");
    std::string tls_error;
    if (!SdkTlsCertificateManager::Provision(SdkTlsCertificateManager::PathsFromConfig(config.tls),
                                            SdkTlsRotationPolicy(), false, &tls_error)) {
        SDK_OPEN_LOG_ERROR("[sdk_open_harmony] TLS provisioning failed: {}", tls_error);
        return false;
    }
#endif

    ProviderBundle providers = mock::CreateProviderBundle();
    const ProviderBundle real_providers = private_impl::CreateProviderBundle(false);
    providers.auth_provider = real_providers.auth_provider;
    providers.device_provider = real_providers.device_provider;
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
    g_running.store(true);
    SDK_OPEN_LOG_INFO("[sdk_open_harmony] embedded runtime started, demo=http://127.0.0.1:{}", config.demo_http_port);
    return true;
}

void Stop() {
    // Status is queried synchronously on the ArkTS dispatcher. Publish the
    // transition before waiting for the lifecycle mutex so status never waits
    // behind USB cleanup that itself needs the dispatcher.
    g_running.store(false);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_running.store(false);
    if (g_app) {
        g_app->Stop();
        g_app.reset();
    }
    // The USB Host dispatcher is owned by the service's ArkTS event loop.
    // Stop is executed by N-API async work so release can wait for the
    // dispatcher to drain UVC requests without blocking that event loop.
    editor::devices::DeviceManager::GetInstance()->release();
}

bool IsRunning() {
    return g_running.load();
}

std::string Status() {
    return DumpJson(Json{{"running", g_running.load()}, {"demoUrl", "http://127.0.0.1:17081/"}});
}

} // namespace harmony
} // namespace sdk
} // namespace editor
