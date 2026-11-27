// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "image_enhance_workflow_store.h"

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {
namespace {

std::mutex& WorkflowStoreMutex() {
    static std::mutex mu;
    return mu;
}

std::string GetOptionalStringField(const Json& obj, const char* key) {
    Json::const_iterator it = obj.find(key);
    if (it != obj.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return "";
}

std::string ImageEnhanceWorkflowDir() {
    return JoinPath(GetSdkOpenWorkDir(), "profiles");
}

std::string ImageEnhanceWorkflowStorePath() {
    return JoinPath(ImageEnhanceWorkflowDir(), "image_enhance_workflows.json");
}

bool FileExistsOrError(const std::string& path, bool* exists) {
    if (exists == NULL) {
        return false;
    }
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        *exists = true;
        return true;
    }
    if (errno == ENOENT) {
        *exists = false;
        return true;
    }
    *exists = false;
    return false;
}

bool ReadJsonFile(const std::string& path, Json* out_json) {
    if (out_json == NULL) {
        return false;
    }
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        return false;
    }
    try {
        in >> *out_json;
        return true;
    } catch (...) {
        return false;
    }
}

bool ReadJsonFileStrict(const std::string& path, Json* out_json) {
    if (out_json == NULL) {
        return false;
    }
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string payload((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return false;
    }
    Json parsed = Json::parse(payload.begin(), payload.end(), NULL, false);
    if (parsed.is_discarded()) {
        return false;
    }
    *out_json = parsed;
    return true;
}

bool WriteExclusiveTempFile(const std::string& final_path,
                            const std::string& content,
                            std::string* temp_path) {
    if (temp_path == NULL) {
        return false;
    }
#if defined(_WIN32)
    const DWORD pid = ::GetCurrentProcessId();
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string candidate = final_path + ".tmp." + std::to_string(static_cast<unsigned long>(pid)) +
                                "." + std::to_string(static_cast<unsigned long>(::GetTickCount())) +
                                "." + std::to_string(static_cast<long long>(attempt));
        HANDLE file = ::CreateFileA(candidate.c_str(),
                                    GENERIC_WRITE,
                                    0,
                                    NULL,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                                    NULL);
        if (file == INVALID_HANDLE_VALUE) {
            if (::GetLastError() == ERROR_FILE_EXISTS || ::GetLastError() == ERROR_ALREADY_EXISTS) {
                continue;
            }
            return false;
        }
        DWORD written = 0;
        bool ok = true;
        std::size_t offset = 0;
        while (offset < content.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(content.size() - offset, 64U * 1024U));
            written = 0;
            if (::WriteFile(file, content.data() + offset, chunk, &written, NULL) == 0 || written != chunk) {
                ok = false;
                break;
            }
            offset += chunk;
        }
        if (::FlushFileBuffers(file) == 0) {
            ok = false;
        }
        ::CloseHandle(file);
        if (!ok) {
            std::remove(candidate.c_str());
            return false;
        }
        temp_path->swap(candidate);
        return true;
    }
    return false;
#else
    std::string candidate = final_path + ".tmp.XXXXXX";
    const int fd = ::mkstemp(&candidate[0]);
    if (fd < 0) {
        return false;
    }
    bool ok = true;
    std::size_t offset = 0;
    while (offset < content.size()) {
        const ssize_t written = ::write(fd, content.data() + offset, content.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (written == 0) {
            ok = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::close(fd) != 0) {
        ok = false;
    }
    if (!ok) {
        std::remove(candidate.c_str());
        return false;
    }
    temp_path->swap(candidate);
    return true;
#endif
}

bool ReplaceFileWithTemp(const std::string& path, const std::string& temp_path) {
#if defined(_WIN32)
    return ::MoveFileExA(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(temp_path.c_str(), path.c_str()) == 0;
#endif
}

bool WriteJsonFile(const std::string& path, const Json& value) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << value.dump(2);
    return static_cast<bool>(out);
}

bool WriteJsonFileStrict(const std::string& path, const Json& value) {
    std::string payload;
    try {
        payload = value.dump(2);
    } catch (...) {
        return false;
    }
    std::string temp_path;
    if (!WriteExclusiveTempFile(path, payload, &temp_path)) {
        return false;
    }
    if (!ReplaceFileWithTemp(path, temp_path)) {
        std::remove(temp_path.c_str());
        return false;
    }
    return true;
}

struct WorkflowStoreLoadResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    Json store = Json::object();
};

WorkflowStoreLoadResult LoadImageEnhanceWorkflowStoreUnlocked(bool strict) {
    WorkflowStoreLoadResult result;
    const std::string path = ImageEnhanceWorkflowStorePath();
    bool exists = false;
    if (!FileExistsOrError(path, &exists)) {
        if (strict) {
            result.code = ToCode(SdkStatusCode::InternalError);
            result.message = "failed to read image enhance workflow store";
            return result;
        }
    }
    Json store;
    const bool read_ok = strict ? ReadJsonFileStrict(path, &store) : ReadJsonFile(path, &store);
    if (!exists || !read_ok) {
        if (strict && exists) {
            result.code = ToCode(SdkStatusCode::InternalError);
            result.message = "invalid image enhance workflow store";
            return result;
        }
        store = Json::object();
    }
    if (!store.is_object()) {
        if (strict) {
            result.code = ToCode(SdkStatusCode::InternalError);
            result.message = "invalid image enhance workflow store";
            return result;
        }
        store = Json::object();
    }
    if (store.find("workflows") != store.end() && !store["workflows"].is_array()) {
        if (strict) {
            result.code = ToCode(SdkStatusCode::InternalError);
            result.message = "invalid image enhance workflow store";
            return result;
        }
        store["workflows"] = Json::array();
    }
    if (store.find("workflows") == store.end()) {
        store["workflows"] = Json::array();
    }
    result.store = store;
    return result;
}

bool SaveImageEnhanceWorkflowStoreUnlocked(const Json& store, bool strict) {
    if (!EnsureDirectoryRecursive(ImageEnhanceWorkflowDir())) {
        return false;
    }
    const std::string store_path = ImageEnhanceWorkflowStorePath();
    return strict ? WriteJsonFileStrict(store_path, store) : WriteJsonFile(store_path, store);
}

std::string CurrentTimestampString() {
    return std::to_string(static_cast<long long>(std::time(NULL)));
}

std::string NextWorkflowIdUnlocked() {
    static uint64_t seq = 1;
    return "wf-" + CurrentTimestampString() + "-" + std::to_string(static_cast<long long>(seq++));
}

Json NormalizeImageEnhanceWorkflowUnlocked(Json workflow) {
    if (!workflow.is_object()) {
        workflow = Json::object();
    }
    if (!workflow.contains("workflow_id") || !workflow["workflow_id"].is_string() || workflow["workflow_id"].get<std::string>().empty()) {
        workflow["workflow_id"] = NextWorkflowIdUnlocked();
    }
    if (!workflow.contains("name") || !workflow["name"].is_string() || workflow["name"].get<std::string>().empty()) {
        workflow["name"] = "Untitled workflow";
    }
    if (!workflow.contains("description") || !workflow["description"].is_string()) {
        workflow["description"] = "";
    }
    if (!workflow.contains("pipeline") || !workflow["pipeline"].is_object()) {
        workflow["pipeline"] = Json{{"version", "image.enhance.pipeline.v1"},
                                    {"steps", Json::array()},
                                    {"target", Json{{"type", "images"}, {"format", "jpg"}, {"export_type", "single-page"}}}};
    }
    if (!workflow["pipeline"].contains("target") || !workflow["pipeline"]["target"].is_object()) {
        workflow["pipeline"]["target"] = Json{{"type", "images"}, {"format", "jpg"}, {"export_type", "single-page"}};
    }
    const std::string now = CurrentTimestampString();
    if (!workflow.contains("created_at") || !workflow["created_at"].is_string()) {
        workflow["created_at"] = now;
    }
    workflow["updated_at"] = now;
    return workflow;
}

ImageEnhanceWorkflowResult MakeWorkflowResult(SdkStatusCode code,
                                              const std::string& message,
                                              const Json& data = Json::object()) {
    ImageEnhanceWorkflowResult result;
    result.code = ToCode(code);
    result.message = message;
    result.data = data;
    return result;
}

} // namespace

ImageEnhanceWorkflowResult ListImageEnhanceWorkflows(bool strict) {
    std::lock_guard<std::mutex> lock(WorkflowStoreMutex());
    const WorkflowStoreLoadResult loaded = LoadImageEnhanceWorkflowStoreUnlocked(strict);
    if (!IsOkStatusCode(loaded.code)) {
        return MakeWorkflowResult(SdkStatusCode::InternalError, loaded.message);
    }
    const Json workflows = loaded.store.find("workflows") != loaded.store.end() && loaded.store["workflows"].is_array()
                               ? loaded.store["workflows"]
                               : Json::array();
    return MakeWorkflowResult(SdkStatusCode::Ok,
                              "ok",
                              Json{{"workflows", workflows},
                                   {"count", workflows.size()}});
}

ImageEnhanceWorkflowResult GetImageEnhanceWorkflow(const std::string& workflow_id, bool strict) {
    if (workflow_id.empty()) {
        return MakeWorkflowResult(SdkStatusCode::InvalidParams, "workflow_id required");
    }
    std::lock_guard<std::mutex> lock(WorkflowStoreMutex());
    const WorkflowStoreLoadResult loaded = LoadImageEnhanceWorkflowStoreUnlocked(strict);
    if (!IsOkStatusCode(loaded.code)) {
        return MakeWorkflowResult(SdkStatusCode::InternalError, loaded.message);
    }
    const Json workflows = loaded.store.find("workflows") != loaded.store.end() && loaded.store["workflows"].is_array()
                               ? loaded.store["workflows"]
                               : Json::array();
    for (Json::const_iterator it = workflows.begin(); it != workflows.end(); ++it) {
        if (it->is_object() && GetOptionalStringField(*it, "workflow_id") == workflow_id) {
            return MakeWorkflowResult(SdkStatusCode::Ok, "ok", Json{{"workflow", *it}});
        }
    }
    return MakeWorkflowResult(SdkStatusCode::InvalidParams, "image enhance workflow not found");
}

ImageEnhanceWorkflowResult SaveImageEnhanceWorkflow(Json workflow, bool strict) {
    std::lock_guard<std::mutex> lock(WorkflowStoreMutex());
    workflow = NormalizeImageEnhanceWorkflowUnlocked(workflow);
    const std::string workflow_id = GetOptionalStringField(workflow, "workflow_id");
    WorkflowStoreLoadResult loaded = LoadImageEnhanceWorkflowStoreUnlocked(strict);
    if (!IsOkStatusCode(loaded.code)) {
        return MakeWorkflowResult(SdkStatusCode::InternalError, loaded.message);
    }
    Json store = loaded.store;
    Json workflows = store.find("workflows") != store.end() && store["workflows"].is_array()
                         ? store["workflows"]
                         : Json::array();
    bool updated = false;
    for (Json::iterator it = workflows.begin(); it != workflows.end(); ++it) {
        if (it->is_object() && GetOptionalStringField(*it, "workflow_id") == workflow_id) {
            if (it->contains("created_at") && (*it)["created_at"].is_string()) {
                workflow["created_at"] = (*it)["created_at"];
            }
            *it = workflow;
            updated = true;
            break;
        }
    }
    if (!updated) {
        workflows.push_back(workflow);
    }
    store["workflows"] = workflows;
    if (!SaveImageEnhanceWorkflowStoreUnlocked(store, strict)) {
        return MakeWorkflowResult(SdkStatusCode::InternalError, "failed to save image enhance workflow");
    }
    return MakeWorkflowResult(SdkStatusCode::Ok,
                              "ok",
                              Json{{"saved", true},
                                   {"updated", updated},
                                   {"workflow", workflow}});
}

ImageEnhanceWorkflowResult DeleteImageEnhanceWorkflow(const std::string& workflow_id, bool strict) {
    if (workflow_id.empty()) {
        return MakeWorkflowResult(SdkStatusCode::InvalidParams, "workflow_id required");
    }
    std::lock_guard<std::mutex> lock(WorkflowStoreMutex());
    WorkflowStoreLoadResult loaded = LoadImageEnhanceWorkflowStoreUnlocked(strict);
    if (!IsOkStatusCode(loaded.code)) {
        return MakeWorkflowResult(SdkStatusCode::InternalError, loaded.message);
    }
    Json store = loaded.store;
    Json workflows = store.find("workflows") != store.end() && store["workflows"].is_array()
                         ? store["workflows"]
                         : Json::array();
    Json kept = Json::array();
    bool deleted = false;
    for (Json::const_iterator it = workflows.begin(); it != workflows.end(); ++it) {
        if (it->is_object() && GetOptionalStringField(*it, "workflow_id") == workflow_id) {
            deleted = true;
            continue;
        }
        kept.push_back(*it);
    }
    if (!deleted) {
        return MakeWorkflowResult(SdkStatusCode::InvalidParams, "image enhance workflow not found");
    }
    store["workflows"] = kept;
    if (!SaveImageEnhanceWorkflowStoreUnlocked(store, strict)) {
        return MakeWorkflowResult(SdkStatusCode::InternalError, "failed to delete image enhance workflow");
    }
    return MakeWorkflowResult(SdkStatusCode::Ok,
                              "ok",
                              Json{{"deleted", true},
                                   {"workflow_id", workflow_id},
                                   {"count", kept.size()}});
}

} // namespace sdk
} // namespace editor
