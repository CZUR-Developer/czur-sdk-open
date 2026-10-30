#include "sdk_open_harmony_host.h"

#include <memory>
#include <string>

#include "device_manager.h"
#include "napi/native_api.h"
#include "usb_host_napi_ohos.h"

namespace {

napi_value MakeString(napi_env env, const std::string& value) {
    napi_value result = nullptr;
    napi_create_string_utf8(env, value.c_str(), value.size(), &result);
    return result;
}

std::string ReadStringArg(napi_env env, napi_callback_info info, std::size_t index) {
    std::size_t argc = index + 1;
    napi_value args[2] = {nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc <= index || args[index] == nullptr) {
        return std::string();
    }
    std::size_t size = 0;
    napi_get_value_string_utf8(env, args[index], nullptr, 0, &size);
    std::string value(size + 1, '\0');
    if (size > 0) {
        napi_get_value_string_utf8(env, args[index], &value[0], value.size(), &size);
    }
    value.resize(size);
    return value;
}

struct LifecycleWork {
    napi_async_work work;
    napi_deferred deferred;
    bool start;
    bool success;
    std::string work_dir;
    std::string web_root;

    LifecycleWork()
        : work(NULL), deferred(NULL), start(false), success(false) {}
};

void ExecuteLifecycle(napi_env, void* data) {
    LifecycleWork* lifecycle = static_cast<LifecycleWork*>(data);
    try {
        if (lifecycle->start) {
            lifecycle->success = editor::sdk::harmony::Start(
                lifecycle->work_dir, lifecycle->web_root);
        } else {
            editor::sdk::harmony::Stop();
            lifecycle->success = true;
        }
    } catch (...) {
        lifecycle->success = false;
    }
}

void CompleteLifecycle(napi_env env, napi_status status, void* data) {
    std::unique_ptr<LifecycleWork> lifecycle(static_cast<LifecycleWork*>(data));
    const bool success = status == napi_ok && lifecycle->success;
    napi_value result = MakeString(env,
        success ? "{\"success\":true}" : "{\"success\":false}");
    napi_resolve_deferred(env, lifecycle->deferred, result);
    napi_delete_async_work(env, lifecycle->work);
}

napi_value QueueLifecycle(napi_env env, bool start,
    const std::string& work_dir, const std::string& web_root) {
    std::unique_ptr<LifecycleWork> lifecycle(new LifecycleWork());
    lifecycle->start = start;
    lifecycle->work_dir = work_dir;
    lifecycle->web_root = web_root;

    napi_value promise = NULL;
    napi_value resource_name = MakeString(env,
        start ? "sdkOpenHarmonyStart" : "sdkOpenHarmonyStop");
    if (napi_create_promise(env, &lifecycle->deferred, &promise) != napi_ok) {
        napi_throw_error(env, NULL, "Failed to create SDK Open lifecycle promise");
        return NULL;
    }
    if (napi_create_async_work(env, NULL, resource_name, ExecuteLifecycle,
        CompleteLifecycle, lifecycle.get(), &lifecycle->work) != napi_ok) {
        napi_throw_error(env, NULL, "Failed to queue SDK Open lifecycle work");
        return NULL;
    }
    if (napi_queue_async_work(env, lifecycle->work) != napi_ok) {
        napi_delete_async_work(env, lifecycle->work);
        napi_throw_error(env, NULL, "Failed to queue SDK Open lifecycle work");
        return NULL;
    }
    lifecycle.release();
    return promise;
}

napi_value Start(napi_env env, napi_callback_info info) {
    return QueueLifecycle(env, true, ReadStringArg(env, info, 0), ReadStringArg(env, info, 1));
}

napi_value Stop(napi_env env, napi_callback_info) {
    return QueueLifecycle(env, false, std::string(), std::string());
}

napi_value Status(napi_env env, napi_callback_info) {
    return MakeString(env, editor::sdk::harmony::Status());
}

} // namespace

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor descriptors[] = {
        {"start", nullptr, Start, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stop", nullptr, Stop, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"status", nullptr, Status, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    if (!editor::devices::RegisterUsbHostNapiOhos(env, exports,
        [](const std::shared_ptr<editor::devices::IUsbTransportOhos>& transport) {
            return editor::devices::DeviceManager::GetInstance()->setUsbTransportOhos(transport);
        })) {
        napi_throw_error(env, NULL, "Failed to register SDK Open USB Host bridge");
        return NULL;
    }
    return exports;
}
EXTERN_C_END

static napi_module sdkOpenModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "sdkopen_harmony",
    .nm_priv = nullptr,
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterSdkOpenModule(void) {
    napi_module_register(&sdkOpenModule);
}
