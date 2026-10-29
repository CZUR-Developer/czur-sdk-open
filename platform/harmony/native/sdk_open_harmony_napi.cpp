#include "sdk_open_harmony_host.h"

#include <string>

#include "napi/native_api.h"

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

napi_value Start(napi_env env, napi_callback_info info) {
    const std::string work_dir = ReadStringArg(env, info, 0);
    const std::string web_root = ReadStringArg(env, info, 1);
    const bool ok = editor::sdk::harmony::Start(work_dir, web_root);
    return MakeString(env, ok ? "{\"success\":true}" : "{\"success\":false}");
}

napi_value Stop(napi_env env, napi_callback_info) {
    editor::sdk::harmony::Stop();
    return MakeString(env, "{\"success\":true}");
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
