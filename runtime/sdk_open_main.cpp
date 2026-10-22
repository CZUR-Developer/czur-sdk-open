// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <thread>
#include <iostream>
#include <memory>
#include <atomic>
#include <chrono>
#include <utility>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#include <limits.h>
#if defined(__APPLE__)
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <CoreFoundation/CoreFoundation.h>
#endif
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
#include "sdk_tls_certificate_manager.h"

namespace {

const char kDefaultWindowsServiceName[] = "CZURSdkOpenApp";
const char kDefaultWindowsServiceDisplayName[] = "CZUR SDK Open App";

#if defined(_WIN32)
std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return std::string();
    }

    const int required = ::WideCharToMultiByte(CP_UTF8,
                                                WC_ERR_INVALID_CHARS,
                                                value.data(),
                                                static_cast<int>(value.size()),
                                                NULL,
                                                0,
                                                NULL,
                                                NULL);
    if (required <= 0) {
        return std::string();
    }

    std::string utf8(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(CP_UTF8,
                              WC_ERR_INVALID_CHARS,
                              value.data(),
                              static_cast<int>(value.size()),
                              &utf8[0],
                              required,
                              NULL,
                              NULL) != required) {
        return std::string();
    }
    return utf8;
}
#endif

std::string GetExecutableDir() {
#if defined(_WIN32)
    // cpp-httplib treats file paths as UTF-8 on Windows. GetModuleFileNameA
    // returns bytes in the system ANSI code page, which makes non-ASCII
    // installation directories (for example, Chinese paths) unreadable by
    // the UTF-8 path conversion used by cpp-httplib. Keep the path in UTF-16
    // until it is converted explicitly to UTF-8.
    std::vector<wchar_t> buffer(MAX_PATH);
    std::wstring exe_path;
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return ".";
        }
        if (length < buffer.size()) {
            exe_path.assign(buffer.data(), length);
            break;
        }
        if (buffer.size() >= 32768) {
            return ".";
        }
        buffer.resize(buffer.size() * 2);
    }

    const std::wstring::size_type pos = exe_path.find_last_of(L"/\\");
    if (pos == std::wstring::npos) {
        return ".";
    }
    const std::string executable_dir = WideToUtf8(exe_path.substr(0, pos));
    return executable_dir.empty() ? "." : executable_dir;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return ".";
    std::unique_ptr<char[]> buffer(new char[size + 1]);
    if (_NSGetExecutablePath(buffer.get(), &size) != 0) return ".";
    buffer[size] = '\0';
    std::string exe_path(buffer.get());
    const size_t pos = exe_path.find_last_of('/');
    return pos == std::string::npos ? "." : exe_path.substr(0, pos);
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

#if defined(__APPLE__)
bool SetDefaultEnvironmentValue(const char* name, const std::string& value) {
    const char* current = std::getenv(name);
    if (current != nullptr && current[0] != '\0') {
        return true;
    }
    return ::setenv(name, value.c_str(), 1) == 0;
}

bool CopyFileIfMissing(const std::string& source,
                       const std::string& destination,
                       mode_t mode) {
    struct stat destination_stat;
    if (::lstat(destination.c_str(), &destination_stat) == 0) {
        if (!S_ISREG(destination_stat.st_mode)) {
            errno = EINVAL;
            return false;
        }
        return ::chmod(destination.c_str(), mode) == 0;
    }
    if (errno != ENOENT) {
        return false;
    }

    const int source_fd = ::open(source.c_str(), O_RDONLY);
    if (source_fd < 0) {
        return false;
    }

    const int destination_fd = ::open(
        destination.c_str(), O_WRONLY | O_CREAT | O_EXCL, mode);
    if (destination_fd < 0) {
        const int open_error = errno;
        ::close(source_fd);
        if (open_error == EEXIST) {
            if (::lstat(destination.c_str(), &destination_stat) != 0 ||
                !S_ISREG(destination_stat.st_mode)) {
                errno = EINVAL;
                return false;
            }
            return ::chmod(destination.c_str(), mode) == 0;
        }
        errno = open_error;
        return false;
    }

    bool copied = true;
    char buffer[16 * 1024];
    while (copied) {
        const ssize_t bytes_read = ::read(source_fd, buffer, sizeof(buffer));
        if (bytes_read == 0) {
            break;
        }
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            copied = false;
            break;
        }

        ssize_t written = 0;
        while (written < bytes_read) {
            const ssize_t result = ::write(
                destination_fd, buffer + written,
                static_cast<size_t>(bytes_read - written));
            if (result < 0 && errno == EINTR) {
                continue;
            }
            if (result <= 0) {
                copied = false;
                break;
            }
            written += result;
        }
    }

    const int copy_error = errno;
    if (::close(source_fd) != 0) {
        copied = false;
    }
    if (::close(destination_fd) != 0) {
        copied = false;
    }
    if (!copied) {
        ::unlink(destination.c_str());
        errno = copy_error;
        return false;
    }
    return ::chmod(destination.c_str(), mode) == 0;
}

bool IsRegularFile(const std::string& path) {
    struct stat path_stat;
    if (::lstat(path.c_str(), &path_stat) != 0) {
        return false;
    }
    if (!S_ISREG(path_stat.st_mode)) {
        errno = EINVAL;
        return false;
    }
    return true;
}

bool ReadRegularFile(const std::string& path, std::string& contents) {
    const int file_descriptor = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (file_descriptor < 0) return false;
    struct stat opened_stat;
    if (::fstat(file_descriptor, &opened_stat) != 0) {
        const int read_error = errno;
        ::close(file_descriptor);
        errno = read_error;
        return false;
    }
    if (!S_ISREG(opened_stat.st_mode)) {
        ::close(file_descriptor);
        errno = EINVAL;
        return false;
    }

    contents.clear();
    char buffer[16 * 1024];
    for (;;) {
        const ssize_t bytes_read = ::read(file_descriptor, buffer, sizeof(buffer));
        if (bytes_read == 0) break;
        if (bytes_read < 0 && errno == EINTR) continue;
        if (bytes_read < 0) {
            const int read_error = errno;
            ::close(file_descriptor);
            errno = read_error;
            return false;
        }
        contents.append(buffer, static_cast<size_t>(bytes_read));
    }
    return ::close(file_descriptor) == 0;
}

bool WriteAll(int file_descriptor, const std::string& contents) {
    size_t written = 0;
    while (written < contents.size()) {
        const ssize_t result = ::write(
            file_descriptor, contents.data() + written,
            contents.size() - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        written += static_cast<size_t>(result);
    }
    return true;
}

bool ReplaceRegularFileAtomically(const std::string& source,
                                  const std::string& destination,
                                  mode_t mode) {
    std::string source_contents;
    if (!ReadRegularFile(source, source_contents)) {
        return false;
    }

    if (!IsRegularFile(destination)) {
        return false;
    }

    const std::string temporary_template = destination + ".tmp.XXXXXX";
    std::vector<char> temporary_path(temporary_template.begin(),
                                     temporary_template.end());
    temporary_path.push_back('\0');
    const int temporary_fd = ::mkstemp(temporary_path.data());
    if (temporary_fd < 0) return false;

    bool replaced = ::fchmod(temporary_fd, mode) == 0 &&
        WriteAll(temporary_fd, source_contents) && ::fsync(temporary_fd) == 0;
    int replace_error = replaced ? 0 : errno;
    if (::close(temporary_fd) != 0 && replaced) {
        replace_error = errno;
        replaced = false;
    }

    if (replaced && ::rename(temporary_path.data(), destination.c_str()) != 0) {
        replace_error = errno;
        replaced = false;
    }
    if (!replaced) {
        ::unlink(temporary_path.data());
        errno = replace_error;
        return false;
    }
    return true;
}

bool MigrateLegacyDefaultCertificate(const std::string& legacy_certificate,
                                     const std::string& current_certificate,
                                     const std::string& current_private_key,
                                     const std::string& user_certificate,
                                     const std::string& user_private_key) {
    std::string user_certificate_contents, legacy_certificate_contents;
    if (!ReadRegularFile(user_certificate, user_certificate_contents) ||
        !ReadRegularFile(legacy_certificate, legacy_certificate_contents)) {
        return false;
    }
    // The PKG carries only this public legacy marker.  It is sufficient to
    // identify the old bundled certificate; the matching old private key is
    // intentionally not shipped and is therefore never copied forward.
    if (user_certificate_contents != legacy_certificate_contents) {
        return true;
    }
    return ReplaceRegularFileAtomically(current_certificate, user_certificate, 0644) &&
           ReplaceRegularFileAtomically(current_private_key, user_private_key, 0600);
}

bool PrepareMacRuntimeEnvironment() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        std::cerr << "HOME is unavailable for the SDK Open LaunchAgent" << std::endl;
        return false;
    }

    const char* runtime_override = std::getenv("CZUR_SDK_RUNTIME_DIR");
    const std::string state_root =
        runtime_override != nullptr && runtime_override[0] != '\0'
            ? std::string(runtime_override)
            : editor::sdk::JoinPath(
                  editor::sdk::JoinPath(
                      editor::sdk::JoinPath(
                          editor::sdk::JoinPath(home, "Library"),
                          "Application Support"),
                      "CZUR"),
                  "sdk-open");

    const char* log_override = std::getenv("SDK_OPEN_LOG_DIR");
    const std::string log_root =
        log_override != nullptr && log_override[0] != '\0'
            ? std::string(log_override)
            : editor::sdk::JoinPath(
                  editor::sdk::JoinPath(
                      editor::sdk::JoinPath(
                          editor::sdk::JoinPath(home, "Library"), "Logs"),
                      "CZUR"),
                  "sdk-open");
    const std::string tls_root = editor::sdk::JoinPath(state_root, "tls");

    if (!editor::sdk::EnsureDirectoryRecursive(state_root) ||
        !editor::sdk::EnsureDirectoryRecursive(log_root) ||
        !editor::sdk::EnsureDirectoryRecursive(tls_root)) {
        std::cerr << "failed to create SDK Open user runtime directories: "
                  << std::strerror(errno) << std::endl;
        return false;
    }

    const std::string executable_dir = GetExecutableDir();
    const std::string certificate_name = "sdk-runtime.localhost.fullchain.pem";
    const std::string legacy_certificate_name =
        "legacy-sdk-runtime.localhost.pem";
    const std::string private_key_name = "sdk-runtime.localhost.key.pem";
    const std::string packaged_tls_root =
        editor::sdk::JoinPath(executable_dir, "tls");
    const std::string certificate_path =
        editor::sdk::JoinPath(tls_root, certificate_name);
    const std::string private_key_path =
        editor::sdk::JoinPath(tls_root, private_key_name);
    const std::string root_certificate_path =
        editor::sdk::JoinPath(tls_root, "local-runtime-root-ca.crt");
    const std::string root_private_key_path =
        editor::sdk::JoinPath(tls_root, "local-runtime-root-ca.key.pem");
    const std::string state_path = editor::sdk::JoinPath(tls_root, "tls-state.json");
    const std::string legacy_certificate_path = editor::sdk::JoinPath(
        packaged_tls_root, legacy_certificate_name);
    const std::string system_tls_root = packaged_tls_root;
    const std::string system_certificate_path =
        editor::sdk::JoinPath(system_tls_root, certificate_name);
    const std::string system_private_key_path =
        editor::sdk::JoinPath(system_tls_root, private_key_name);
    const std::string system_root_certificate_path =
        editor::sdk::JoinPath(system_tls_root, "local-runtime-root-ca.crt");
    const std::string system_root_private_key_path =
        editor::sdk::JoinPath(system_tls_root, "local-runtime-root-ca.key.pem");
    const std::string system_state_path =
        editor::sdk::JoinPath(system_tls_root, "tls-state.json");

    if (!IsRegularFile(system_certificate_path) || !IsRegularFile(system_private_key_path) ||
        !IsRegularFile(system_root_certificate_path) || !IsRegularFile(system_root_private_key_path) ||
        !IsRegularFile(system_state_path)) {
        std::cerr << "system SDK Open TLS provisioning is incomplete" << std::endl;
        return false;
    }

    bool has_user_certificate = IsRegularFile(certificate_path);
    bool has_user_private_key = IsRegularFile(private_key_path);
    if (has_user_certificate != has_user_private_key) {
        std::string existing_certificate;
        std::string legacy_marker;
        const bool legacy_missing_key = has_user_certificate &&
            ReadRegularFile(certificate_path, existing_certificate) &&
            ReadRegularFile(legacy_certificate_path, legacy_marker) &&
            existing_certificate == legacy_marker;
        if (!legacy_missing_key) {
            std::cerr << "user SDK Open TLS certificate and private key must be replaced as a pair" << std::endl;
            return false;
        }
        ::unlink(certificate_path.c_str());
        has_user_certificate = false;
    }
    if (!has_user_certificate &&
        (!CopyFileIfMissing(system_certificate_path, certificate_path, 0644) ||
         !CopyFileIfMissing(system_private_key_path, private_key_path, 0600))) {
        std::cerr << "failed to copy system SDK Open TLS leaf files: "
                  << std::strerror(errno) << std::endl;
        return false;
    }

    if (!CopyFileIfMissing(system_root_certificate_path, root_certificate_path, 0644) ||
        !CopyFileIfMissing(system_root_private_key_path, root_private_key_path, 0600) ||
        !CopyFileIfMissing(system_state_path, state_path, 0600)) {
        std::cerr << "failed to copy system SDK Open managed TLS metadata: "
                  << std::strerror(errno) << std::endl;
        return false;
    }

    struct stat legacy_certificate_stat;
    const int legacy_status =
        ::lstat(legacy_certificate_path.c_str(), &legacy_certificate_stat);
    if ((legacy_status != 0 && errno != ENOENT) ||
        (legacy_status == 0 && has_user_certificate && !MigrateLegacyDefaultCertificate(
            legacy_certificate_path, system_certificate_path, system_private_key_path,
            certificate_path, private_key_path))) {
        std::cerr << "failed to migrate SDK Open TLS certificate: "
                  << std::strerror(errno) << std::endl;
        return false;
    }

    if (!SetDefaultEnvironmentValue("CZUR_SDK_RUNTIME_DIR", state_root) ||
        !SetDefaultEnvironmentValue("SDK_OPEN_LOG_DIR", log_root) ||
        !SetDefaultEnvironmentValue("SDK_TLS_ENABLED", "1") ||
        !SetDefaultEnvironmentValue("SDK_TLS_BIND_HOST", "127.0.0.1") ||
        !SetDefaultEnvironmentValue("SDK_TLS_CERT_FILE", certificate_path) ||
        !SetDefaultEnvironmentValue("SDK_TLS_KEY_FILE", private_key_path)) {
        std::cerr << "failed to initialize SDK Open environment: "
                  << std::strerror(errno) << std::endl;
        return false;
    }

    if (::chdir(executable_dir.c_str()) != 0) {
        std::cerr << "failed to set SDK Open working directory: "
                  << std::strerror(errno) << std::endl;
        return false;
    }
    return true;
}
#endif

#if !defined(_WIN32)
bool BlockShutdownSignals(sigset_t* signal_set) {
    if (signal_set == nullptr) {
        return false;
    }
    sigemptyset(signal_set);
    sigaddset(signal_set, SIGINT);
    sigaddset(signal_set, SIGTERM);
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
    // Service installation uses the ANSI CreateServiceA API below, so keep
    // this path in the matching system code page. Runtime file paths use
    // GetExecutableDir(), which is converted to UTF-8 separately.
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

editor::sdk::SdkConfig LoadSdkOpenConfig(const std::string& config_path,
                                      bool use_local_tls_asset_url) {
    editor::sdk::SdkConfig config = editor::sdk::SdkConfig::FromFile(config_path);
    config.web_root = GetExecutableDir() + "/web";
    editor::sdk::ApplySdkEnvironmentOverrides(&config);
    // The macOS package certificate names sdk-runtime.localhost, not the bind IP.
    // Apply the default after explicit configuration and environment overrides.
    if (use_local_tls_asset_url && config.tls.enabled && config.asset_base_url.empty()) {
        config.asset_base_url = "https://sdk-runtime.localhost:" +
                                std::to_string(config.tls.asset_https_port);
    }
    return config;
}

std::unique_ptr<editor::sdk::SdkApp> CreateSdkOpenApp(
    const std::string& config_path, bool use_local_tls_asset_url = false) {
    editor::sdk::InitializeSdkOpenLogger();
    editor::sdk::SdkConfig config = LoadSdkOpenConfig(config_path, use_local_tls_asset_url);

    if (config.tls.enabled) {
        const editor::sdk::SdkTlsRotationPolicy policy;
        const editor::sdk::SdkTlsCertificatePaths paths =
            editor::sdk::SdkTlsCertificateManager::PathsFromConfig(config.tls);
        bool rotated = false;
        std::string tls_error;
        if (!editor::sdk::SdkTlsCertificateManager::RotateIfNeeded(
                paths, policy, &rotated, &tls_error)) {
            SDK_OPEN_LOG_ERROR("[sdk_open_app] managed TLS certificate check failed: {}", tls_error);
            return std::unique_ptr<editor::sdk::SdkApp>();
        }
    }

#if defined(_WIN32)
    if (!config.twain_work_dir.empty() && _putenv_s("CZUR_TWAIN_WORK_DIR", config.twain_work_dir.c_str()) != 0) {
        SDK_OPEN_LOG_WARN("[sdk_open_app] failed to set CZUR_TWAIN_WORK_DIR");
    }
#endif

#if defined(SDK_USE_PRIVATE_PROVIDER) && defined(_WIN32)
    ApplyDefaultSaneConfigDir();
    editor::sdk::ProviderBundle providers = editor::sdk::private_windows::CreateProviderBundle();
    SDK_OPEN_LOG_INFO("[sdk_open_app] provider mode: private");
#elif defined(SDK_USE_PRIVATE_PROVIDER)
    ApplyDefaultSaneConfigDir();
    // Linux SDK Open 显式挂载 storage provider；其他 private bundle 调用保持原有能力集合。
    editor::sdk::ProviderBundle providers = editor::sdk::private_impl::CreateProviderBundle(true);
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
    if (!app) {
        return 1;
    }
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
    bool run_as_launch_agent = false;
#if defined(__APPLE__)
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        if (arg == "--launch-agent") {
            run_as_launch_agent = true;
            break;
        }
    }
    if (run_as_launch_agent && !PrepareMacRuntimeEnvironment()) {
        return 1;
    }
#endif
    std::string config_path;
    bool provision_tls = false;
    bool renew_tls = false;
    std::string provision_tls_dir;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        if (arg == "--provision-tls") {
            provision_tls = true;
        } else if (arg == "--renew-tls") {
            renew_tls = true;
        } else if (arg == "--provision-tls-dir" && i + 1 < argc) {
            provision_tls_dir = argv[++i];
        }
    }
    if (provision_tls || renew_tls) {
        if (provision_tls_dir.empty()) {
            std::cerr << "--provision-tls-dir is required with --provision-tls" << std::endl;
            return 2;
        }
        editor::sdk::SdkTlsCertificatePaths paths;
        paths.tls_dir = provision_tls_dir;
        paths.root_certificate = editor::sdk::JoinPath(provision_tls_dir, "local-runtime-root-ca.crt");
        paths.root_private_key = editor::sdk::JoinPath(provision_tls_dir, "local-runtime-root-ca.key.pem");
        paths.certificate_chain = editor::sdk::JoinPath(provision_tls_dir, "sdk-runtime.localhost.fullchain.pem");
        paths.private_key = editor::sdk::JoinPath(provision_tls_dir, "sdk-runtime.localhost.key.pem");
        paths.state = editor::sdk::JoinPath(provision_tls_dir, "tls-state.json");
        std::string error;
        const bool success = renew_tls
            ? editor::sdk::SdkTlsCertificateManager::RenewLeaf(
                  paths, editor::sdk::SdkTlsRotationPolicy(), &error)
            : editor::sdk::SdkTlsCertificateManager::Provision(
                  paths, editor::sdk::SdkTlsRotationPolicy(), false, &error);
        if (!success) {
            std::cerr << "failed to provision SDK Open TLS: " << error << std::endl;
            return 1;
        }
        return 0;
    }
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
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (!arg.empty() && arg[0] != '-' && config_path.empty()) {
            config_path = arg;
        }
    }
#endif

    std::unique_ptr<editor::sdk::SdkApp> app = CreateSdkOpenApp(config_path, run_as_launch_agent);
    if (!app) {
        return 1;
    }
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
#if defined(__APPLE__)
    // AVFoundation publishes device changes through Cocoa's main run loop.
    // A LaunchAgent has no NSApplication event loop and previously blocked
    // this thread in sigwait(), so AVFoundation never advanced its device
    // inventory after a USB unplug/replug.  Keep signal handling on a helper
    // thread while the main thread pumps Cocoa.  Stop() still runs on the
    // original main thread after the loop exits.
    std::atomic<bool> shutdown_requested(false);
    std::atomic<int> shutdown_signal(0);
    std::thread signal_waiter([&shutdown_signals, &shutdown_requested, &shutdown_signal]() {
        const int signal_number = WaitForShutdownSignal(shutdown_signals);
        shutdown_signal.store(signal_number, std::memory_order_release);
        shutdown_requested.store(true, std::memory_order_release);
    });

    SDK_OPEN_LOG_INFO("[sdk_open_app] pumping Cocoa main run loop for AVFoundation notifications");
    while (!shutdown_requested.load(std::memory_order_acquire)) {
        const SInt32 run_result =
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
        // A run loop with no registered sources can return immediately. Keep
        // the LaunchAgent from spinning while still checking shutdown often.
        if (run_result == kCFRunLoopRunFinished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    signal_waiter.join();
    const int signal_number = shutdown_signal.load(std::memory_order_acquire);
#else
    const int signal_number = WaitForShutdownSignal(shutdown_signals);
#endif
    SDK_OPEN_LOG_INFO("[sdk_open_app] shutdown requested, signal={}", signal_number);
    app->Stop();
#endif

#if defined(SDK_USE_PRIVATE_PROVIDER) && !defined(_WIN32)
    // Cached CZOCR handlers must be destroyed before the dynamically loaded
    // algorithm library tears down its process-global logging state.
    editor::sdk::private_impl::ShutdownOcrRuntime();
#endif
    return 0;
}
#endif
