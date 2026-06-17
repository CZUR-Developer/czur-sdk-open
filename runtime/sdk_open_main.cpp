// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <thread>
#include <iostream>
#include <utility>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

#if defined(SDK_USE_PRIVATE_PROVIDER) && !defined(_WIN32)
#include "private_provider_factory.h"
#elif defined(SDK_USE_PRIVATE_PROVIDER) && defined(_WIN32)
#include "private_windows_provider_factory.h"
#elif !defined(SDK_USE_PRIVATE_PROVIDER)
#include "mock_provider_factory.h"
#endif
#include "sdk_app.h"
#include "sdk_config.h"
#include "sdk_logger.h"
#include "sdk_runtime_paths.h"

namespace {

std::string GetExecutableDir() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    const DWORD length = ::GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (length == 0) {
        return ".";
    }
    std::string exe_path(buffer, length);
    const size_t pos = exe_path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    return exe_path.substr(0, pos);
#else
    char buffer[PATH_MAX];
    const ssize_t len = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        return ".";
    }
    buffer[len] = '\0';
    std::string exe_path(buffer);
    const size_t pos = exe_path.find_last_of('/');
    if (pos == std::string::npos) {
        return ".";
    }
    return exe_path.substr(0, pos);
#endif
}

#if !defined(_WIN32)
bool BlockShutdownSignals(sigset_t* signal_set) {
    if (signal_set == nullptr) {
        return false;
    }
    ::sigemptyset(signal_set);
    ::sigaddset(signal_set, SIGINT);
    ::sigaddset(signal_set, SIGTERM);
    return ::pthread_sigmask(SIG_BLOCK, signal_set, nullptr) == 0;
}

int WaitForShutdownSignal(const sigset_t& signal_set) {
    int signal_number = 0;
    const int result = ::sigwait(&signal_set, &signal_number);
    if (result != 0) {
        return 0;
    }
    return signal_number;
}
#else
HANDLE g_windows_shutdown_event = NULL;
LONG g_windows_shutdown_signal = 0;

bool IsWindowsShutdownSignal(const DWORD signal_number) {
    return signal_number == CTRL_C_EVENT ||
           signal_number == CTRL_BREAK_EVENT ||
           signal_number == CTRL_CLOSE_EVENT ||
           signal_number == CTRL_SHUTDOWN_EVENT;
}

BOOL WINAPI WindowsShutdownHandler(DWORD signal_number) {
    if (!IsWindowsShutdownSignal(signal_number)) {
        return FALSE;
    }
    ::InterlockedExchange(&g_windows_shutdown_signal, static_cast<LONG>(signal_number));
    if (g_windows_shutdown_event != NULL) {
        ::SetEvent(g_windows_shutdown_event);
    }
    return TRUE;
}

bool InitializeWindowsShutdownEvent(HANDLE* stop_event) {
    if (stop_event == NULL) {
        return false;
    }
    *stop_event = ::CreateEventA(NULL, TRUE, FALSE, NULL);
    if (*stop_event == NULL) {
        return false;
    }
    g_windows_shutdown_event = *stop_event;
    ::InterlockedExchange(&g_windows_shutdown_signal, 0);
    if (::SetConsoleCtrlHandler(WindowsShutdownHandler, TRUE) == FALSE) {
        g_windows_shutdown_event = NULL;
        ::CloseHandle(*stop_event);
        *stop_event = NULL;
        return false;
    }
    return true;
}

void CleanupWindowsShutdownEvent(HANDLE stop_event) {
    ::SetConsoleCtrlHandler(WindowsShutdownHandler, FALSE);
    if (g_windows_shutdown_event == stop_event) {
        g_windows_shutdown_event = NULL;
    }
    if (stop_event != NULL) {
        ::CloseHandle(stop_event);
    }
}

DWORD WaitForWindowsShutdownSignal(HANDLE stop_event) {
    if (stop_event == NULL) {
        return WAIT_FAILED;
    }
    return ::WaitForSingleObject(stop_event, INFINITE);
}

DWORD LastWindowsShutdownSignal() {
    return static_cast<DWORD>(::InterlockedCompareExchange(&g_windows_shutdown_signal, 0, 0));
}
#endif

} // namespace

void ApplyEnvPortOverride(const char* env_key, int& target_port) {
    const char* val = std::getenv(env_key);
    if (val == nullptr) {
        return;
    }
    const int parsed = std::atoi(val);
    if (parsed > 0 && parsed <= 65535) {
        target_port = parsed;
    }
}

void ApplyEnvStringOverride(const char* env_key, std::string& target_value) {
    const char* val = std::getenv(env_key);
    if (val == nullptr) {
        return;
    }
    target_value = val;
}

void ApplyDefaultSaneConfigDir() {
    const char* sane_config_dir = std::getenv("CZUR_SANE_CONFIG_DIR");
    if (sane_config_dir != nullptr && sane_config_dir[0] != '\0') {
        return;
    }
    const std::string default_dir = editor::sdk::JoinPath(
        editor::sdk::JoinPath(editor::sdk::GetSdkOpenWorkDir(), "sane"), "config");
#if defined(_WIN32)
    if (_putenv_s("CZUR_SANE_CONFIG_DIR", default_dir.c_str()) != 0) {
#else
    if (::setenv("CZUR_SANE_CONFIG_DIR", default_dir.c_str(), 0) != 0) {
#endif
        SDK_OPEN_LOG_WARN("[sdk_open_app] failed to set default CZUR_SANE_CONFIG_DIR: {}", std::strerror(errno));
    }
}

#if !defined(SDK_OPEN_MAIN_TESTING)
int main(int argc, char* argv[]) {
#if defined(_WIN32)
    HANDLE shutdown_event = NULL;
    if (!InitializeWindowsShutdownEvent(&shutdown_event)) {
        std::cerr << "failed to initialize Windows shutdown handler" << std::endl;
        return 1;
    }
#else
    sigset_t shutdown_signals;
    if (!BlockShutdownSignals(&shutdown_signals)) {
        std::cerr << "failed to initialize shutdown signal handling" << std::endl;
        return 1;
    }
#endif

    std::string config_path;
    if (argc > 1 && argv[1] != nullptr) {
        config_path = argv[1];
    }

    editor::sdk::InitializeSdkOpenLogger();
    editor::sdk::SdkConfig config = editor::sdk::SdkConfig::FromFile(config_path);
    config.web_root = GetExecutableDir() + "/web";
    ApplyEnvPortOverride("SDK_ADMIN_HTTP_PORT", config.admin_http_port);
    ApplyEnvPortOverride("SDK_DEMO_HTTP_PORT", config.demo_http_port);
    ApplyEnvPortOverride("SDK_ASSET_HTTP_PORT", config.asset_http_port);
    ApplyEnvPortOverride("SDK_COMMAND_WS_PORT", config.command_ws_port);
    ApplyEnvPortOverride("SDK_VIDEO_WS_PORT", config.video_ws_port);
    ApplyEnvStringOverride("SDK_ASSET_BASE_URL", config.asset_base_url);
    ApplyEnvStringOverride("SDK_AUTH_TOKEN", config.auth_token);

#if defined(SDK_USE_PRIVATE_PROVIDER) && defined(_WIN32)
    ApplyDefaultSaneConfigDir();
    editor::sdk::ProviderBundle providers = editor::sdk::private_windows::CreateProviderBundle();
    SDK_OPEN_LOG_INFO("[sdk_open_app] provider mode: private");
#elif defined(SDK_USE_PRIVATE_PROVIDER)
    ApplyDefaultSaneConfigDir();
    editor::sdk::ProviderBundle providers = editor::sdk::private_impl::CreateProviderBundle();
    SDK_OPEN_LOG_INFO("[sdk_open_app] provider mode: private");
#else
    editor::sdk::ProviderBundle providers = editor::sdk::mock::CreateProviderBundle();
    SDK_OPEN_LOG_INFO("[sdk_open_app] provider mode: mock");
#endif
    editor::sdk::SdkApp app(config, std::move(providers));
    if (!app.Start()) {
        SDK_OPEN_LOG_ERROR("[sdk_open_app] failed to start");
#if defined(_WIN32)
        CleanupWindowsShutdownEvent(shutdown_event);
#endif
        return 1;
    }

#if defined(_WIN32)
    SDK_OPEN_LOG_INFO("[sdk_open_app] running. waiting for CTRL_C/CTRL_BREAK/CTRL_CLOSE/CTRL_SHUTDOWN...");
    const DWORD wait_result = WaitForWindowsShutdownSignal(shutdown_event);
    const DWORD signal_number = LastWindowsShutdownSignal();
    if (wait_result == WAIT_OBJECT_0) {
        SDK_OPEN_LOG_INFO("[sdk_open_app] shutdown requested, signal={}", signal_number);
    } else {
        SDK_OPEN_LOG_WARN("[sdk_open_app] wait for shutdown failed, wait_result={} last_error={}",
                          wait_result,
                          static_cast<unsigned long>(::GetLastError()));
    }
    app.Stop();
    CleanupWindowsShutdownEvent(shutdown_event);
#else
    SDK_OPEN_LOG_INFO("[sdk_open_app] running. waiting for SIGINT/SIGTERM...");
    const int signal_number = WaitForShutdownSignal(shutdown_signals);
    SDK_OPEN_LOG_INFO("[sdk_open_app] shutdown requested, signal={}", signal_number);
    app.Stop();
#endif
    return 0;
}
#endif
