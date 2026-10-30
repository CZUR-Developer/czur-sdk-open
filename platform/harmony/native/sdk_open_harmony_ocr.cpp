#include <dlfcn.h>
#include <mutex>
#include <string>

#include "czcv_ocr_capi.h"
#include "sdk_logger.h"

// The shipped CZOCR API assumes the algorithm library lives beside writable
// model data. Harmony requires native code to load from the installed HAP libs
// directory. Wrap only initialization; the API's loader still initializes its
// normal dispatch table, and all recognition calls use the original API.
extern "C" void* __wrap_czocr_init_in_dev_stage(const char* model_dir,
                                                const char* license_key,
                                                const char* is_develop,
                                                const char* log_dir) {
    typedef bool (*LoadLibrary)(const char*);
    typedef void* (*InitAlgorithm)(const char*, const char*, const char*, const char*);
    static std::mutex mutex;
    static InitAlgorithm initialize = NULL;
    std::lock_guard<std::mutex> lock(mutex);
    if (initialize == NULL) {
        Dl_info info = {};
        if (::dladdr(reinterpret_cast<void*>(&czocr_check_lic), &info) == 0 ||
            info.dli_fname == NULL) {
            SDK_OPEN_LOG_ERROR("[harmony_ocr] CZOCR API location unavailable");
            return NULL;
        }
        const std::string api_path(info.dli_fname);
        const std::size_t slash = api_path.find_last_of('/');
        if (slash == std::string::npos) {
            SDK_OPEN_LOG_ERROR("[harmony_ocr] CZOCR API path has no directory: {}", api_path);
            return NULL;
        }
        const std::string algorithm_path = api_path.substr(0, slash) + "/libczcv_ocr.so";
        // These modules and the API dispatch table live for the whole process.
        static void* api = ::dlopen(api_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        static void* algorithm = ::dlopen(algorithm_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (api == NULL || algorithm == NULL) {
            const char* error = ::dlerror();
            SDK_OPEN_LOG_ERROR("[harmony_ocr] native library load failed: {}: {}",
                               algorithm_path, error ? error : "unknown loader error");
            return NULL;
        }
        LoadLibrary load = reinterpret_cast<LoadLibrary>(::dlsym(api, "czocr_load_library"));
        InitAlgorithm init = reinterpret_cast<InitAlgorithm>(::dlsym(algorithm, "czcv_ocr_init_doc"));
        if (load == NULL || init == NULL) {
            SDK_OPEN_LOG_ERROR("[harmony_ocr] CZOCR library lacks native initialization exports");
            return NULL;
        }
        if (!load(algorithm_path.c_str())) {
            SDK_OPEN_LOG_ERROR("[harmony_ocr] CZOCR dispatch initialization failed: {}", algorithm_path);
            return NULL;
        }
        initialize = init;
        SDK_OPEN_LOG_INFO("[harmony_ocr] initialized CZOCR dispatch from {}", algorithm_path);
    }
    // Same native initializer and arguments as czocr_init_in_dev_stage. License
    // validation in Ocr::initModule remains in effect.
    return initialize(model_dir, license_key, is_develop, log_dir);
}
