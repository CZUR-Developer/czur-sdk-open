// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <thread>
#include <iostream>
#include <memory>
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

const char kDefaultWindowsServiceName[] = "CZURSdkOpenApp";
const char kDefaultWindowsServiceDisplayName[] = "CZUR SDK Open App";

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
SERVICE_STATUS_HANDLE g_service_status_handle = NULL;
SERVICE_STATUS g_service_status = {};
HANDLE g_service_stop_event = NULL;

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

std::string QuoteWindowsArg(const std::string& value) {
    std::string quoted = "\"";
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (*it == '"') {
            quoted += "\\\"";
        } else {
            quoted += *it;
        }
    }
    quoted += "\"";
    return quoted;
}

std::string GetExecutablePath() {
    char buffer[MAX_PATH] = {0};
    const DWORD length = ::GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return length == 0 ? std::string() : std::string(buffer, length);
}

void ReportServiceStatus(DWORD current_state, DWORD win32_exit_code, DWORD wait_hint) {
    if (g_service_status_handle == NULL) {
        return;
    }

    static DWORD checkpoint = 1;
    g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_service_status.dwCurrentState = current_state;
    g_service_status.dwWin32ExitCode = win32_exit_code;
    g_service_status.dwWaitHint = wait_hint;
    g_service_status.dwControlsAccepted =
        current_state == SERVICE_RUNNING ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    g_service_status.dwCheckPoint =
        (current_state == SERVICE_RUNNING || current_state == SERVICE_STOPPED) ? 0 : checkpoint++;
    ::SetServiceStatus(g_service_status_handle, &g_service_status);
}

void WINAPI WindowsServiceControlHandler(DWORD control) {
    if (control != SERVICE_CONTROL_STOP && control != SERVICE_CONTROL_SHUTDOWN) {
        return;
    }
    ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 30000);
    if (g_service_stop_event != NULL) {
        ::SetEvent(g_service_stop_event);
    }
}

bool InstallWindowsService(const std::string& service_name,
                           const std::string& display_name,
                           const std::string& config_path) {
    const std::string executable_path = GetExecutablePath();
    if (executable_path.empty()) {
        std::cerr << "failed to resolve sdk_open_app executable path" << std::endl;
        return false;
    }

    std::string binary_path = QuoteWindowsArg(executable_path) + " --service --service-name " +
                              QuoteWindowsArg(service_name);
    if (!config_path.empty()) {
        binary_path += " --config " + QuoteWindowsArg(config_path);
    }

    SC_HANDLE scm = ::OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL) {
        std::cerr << "OpenSCManager failed, error=" << ::GetLastError() << std::endl;
        return false;
    }

    SC_HANDLE service = ::CreateServiceA(scm,
                                         service_name.c_str(),
                                         display_name.c_str(),
                                         SERVICE_ALL_ACCESS,
                                         SERVICE_WIN32_OWN_PROCESS,
                                         SERVICE_AUTO_START,
                                         SERVICE_ERROR_NORMAL,
                                         binary_path.c_str(),
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL);
    if (service == NULL) {
        const DWORD error = ::GetLastError();
        ::CloseServiceHandle(scm);
        std::cerr << "CreateService failed, error=" << error << std::endl;
        return false;
    }

    ::CloseServiceHandle(service);
    ::CloseServiceHandle(scm);
    std::cout << "installed Windows service: " << service_name << std::endl;
    return true;
}

bool UninstallWindowsService(const std::string& service_name) {
    SC_HANDLE scm = ::OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        std::cerr << "OpenSCManager failed, error=" << ::GetLastError() << std::endl;
        return false;
    }

    SC_HANDLE service = ::OpenServiceA(scm, service_name.c_str(), DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == NULL) {
        const DWORD error = ::GetLastError();
        ::CloseServiceHandle(scm);
        std::cerr << "OpenService failed, error=" << error << std::endl;
        return false;
    }

    SERVICE_STATUS status = {};
    ::ControlService(service, SERVICE_CONTROL_STOP, &status);
    if (::DeleteService(service) == FALSE) {
        const DWORD error = ::GetLastError();
        ::CloseServiceHandle(service);
        ::CloseServiceHandle(scm);
        std::cerr << "DeleteService failed, error=" << error << std::endl;
        return false;
    }

    ::CloseServiceHandle(service);
    ::CloseServiceHandle(scm);
    std::cout << "uninstalled Windows service: " << service_name << std::endl;
    return true;
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

std::unique_ptr<editor::sdk::SdkApp> CreateSdkOpenApp(const std::string& config_path) {
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
    return std::unique_ptr<editor::sdk::SdkApp>(new editor::sdk::SdkApp(config, std::move(providers)));
}

#if defined(_WIN32)
int RunSdkOpenAppUntilEvent(const std::string& config_path, HANDLE shutdown_event) {
    std::unique_ptr<editor::sdk::SdkApp> app = CreateSdkOpenApp(config_path);
    if (!app->Start()) {
        SDK_OPEN_LOG_ERROR("[sdk_open_app] failed to start");
        return 1;
    }

    SDK_OPEN_LOG_INFO("[sdk_open_app] running as Windows service");
    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
    const DWORD wait_result = WaitForWindowsShutdownSignal(shutdown_event);
    if (wait_result == WAIT_OBJECT_0) {
        SDK_OPEN_LOG_INFO("[sdk_open_app] service stop requested");
    } else {
        SDK_OPEN_LOG_WARN("[sdk_open_app] wait for service stop failed, wait_result={} last_error={}",
                          wait_result,
                          static_cast<unsigned long>(::GetLastError()));
    }
    app->Stop();
    return wait_result == WAIT_OBJECT_0 ? 0 : 1;
}

std::string g_service_config_path;
std::string g_service_name = kDefaultWindowsServiceName;

void WINAPI WindowsServiceMain(DWORD, LPSTR*) {
    g_service_status_handle = ::RegisterServiceCtrlHandlerA(g_service_name.c_str(), WindowsServiceControlHandler);
    if (g_service_status_handle == NULL) {
        return;
    }

    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 30000);
    g_service_stop_event = ::CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_service_stop_event == NULL) {
        ReportServiceStatus(SERVICE_STOPPED, ::GetLastError(), 0);
        return;
    }

    const int result = RunSdkOpenAppUntilEvent(g_service_config_path, g_service_stop_event);
    ::CloseHandle(g_service_stop_event);
    g_service_stop_event = NULL;
    ReportServiceStatus(SERVICE_STOPPED, result == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR, 0);
}

int RunAsWindowsService(const std::string& service_name, const std::string& config_path) {
    g_service_name = service_name.empty() ? kDefaultWindowsServiceName : service_name;
    g_service_config_path = config_path;

    SERVICE_TABLE_ENTRYA service_table[] = {
        {const_cast<char*>(g_service_name.c_str()), WindowsServiceMain},
        {NULL, NULL},
    };

    if (::StartServiceCtrlDispatcherA(service_table) == FALSE) {
        const DWORD error = ::GetLastError();
        std::cerr << "StartServiceCtrlDispatcher failed, error=" << error << std::endl;
        return 1;
    }
    return 0;
}
#endif

#if !defined(SDK_OPEN_MAIN_TESTING)
int main(int argc, char* argv[]) {
    std::string config_path;
#if defined(_WIN32)
    bool run_as_service = false;
    bool install_service = false;
    bool uninstall_service = false;
    std::string service_name = kDefaultWindowsServiceName;
    std::string service_display_name = kDefaultWindowsServiceDisplayName;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        if (arg == "--service") {
            run_as_service = true;
        } else if (arg == "--install-service") {
            install_service = true;
        } else if (arg == "--uninstall-service") {
            uninstall_service = true;
        } else if (arg == "--service-name" && i + 1 < argc) {
            service_name = argv[++i];
        } else if (arg == "--display-name" && i + 1 < argc) {
            service_display_name = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (!arg.empty() && arg[0] != '-' && config_path.empty()) {
            config_path = arg;
        }
    }

    if (install_service) {
        return InstallWindowsService(service_name, service_display_name, config_path) ? 0 : 1;
    }
    if (uninstall_service) {
        return UninstallWindowsService(service_name) ? 0 : 1;
    }
    if (run_as_service) {
        return RunAsWindowsService(service_name, config_path);
    }

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
#if !defined(_WIN32)
    if (argc > 1 && argv[1] != nullptr) {
        config_path = argv[1];
    }
#endif

    std::unique_ptr<editor::sdk::SdkApp> app = CreateSdkOpenApp(config_path);
    if (!app->Start()) {
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
    app->Stop();
    CleanupWindowsShutdownEvent(shutdown_event);
#else
    SDK_OPEN_LOG_INFO("[sdk_open_app] running. waiting for SIGINT/SIGTERM...");
    const int signal_number = WaitForShutdownSignal(shutdown_signals);
    SDK_OPEN_LOG_INFO("[sdk_open_app] shutdown requested, signal={}", signal_number);
    app->Stop();
#endif
    return 0;
}
#endif
