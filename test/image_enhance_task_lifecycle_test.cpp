// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <dlfcn.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
thread_local bool g_fail_next_thread = false;
std::atomic<int> g_injected_thread_failures(0);
}

// Test-only pthread_create interposition.  It is intentionally defined in the
// test executable, never in the SDK library.
extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                              void* (*entry)(void*), void* argument) noexcept {
    if (g_fail_next_thread) {
        g_fail_next_thread = false;
        ++g_injected_thread_failures;
        return EAGAIN;
    }
    typedef int (*CreateThread)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
    static CreateThread create_thread =
        reinterpret_cast<CreateThread>(dlsym(RTLD_NEXT, "pthread_create"));
    return create_thread ? create_thread(thread, attr, entry, argument) : EAGAIN;
}

#include "image_enhance_task_service.h"
#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {
namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
void RequireEventually(Predicate predicate, int timeout_ms, const std::string& message) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(predicate(), message);
}

class BlockingImageEnhanceProvider : public ISdkImageEnhanceProvider {
public:
    enum Mode {
        kComplete,
        kThrow,
        kFail
    };

    std::string ProviderName() const override { return "image-enhance-lifecycle-test"; }

    SdkImageEnhanceCapabilityResult ListCapabilities() override {
        SdkImageEnhanceCapabilityResult result;
        result.provider = ProviderName();
        return result;
    }

    SdkImageEnhanceStepResult RunStep(const SdkImageEnhanceStepRequest& request) override {
        Mode mode = kComplete;
        {
            std::unique_lock<std::mutex> lock(mu_);
            started_.insert(request.task_id);
            cv_.notify_all();
            cv_.wait(lock, [this, &request]() {
                return released_.find(request.task_id) != released_.end();
            });
            std::map<std::string, Mode>::const_iterator it = modes_.find(request.task_id);
            if (it != modes_.end()) {
                mode = it->second;
            }
        }
        if (mode == kThrow) {
            throw std::runtime_error("test provider failure");
        }
        SdkImageEnhanceStepResult result;
        if (mode == kFail) {
            result.code = ToCode(SdkStatusCode::InternalError);
            result.message = "test provider returned failure";
            return result;
        }
        result.processed = true;
        result.pages = request.pages;
        return result;
    }

    bool WaitForStarted(const std::string& task_id, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this, &task_id]() {
            return started_.find(task_id) != started_.end();
        });
    }

    void Release(const std::string& task_id) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            released_.insert(task_id);
        }
        cv_.notify_all();
    }

    void SetMode(const std::string& task_id, Mode mode) {
        std::lock_guard<std::mutex> lock(mu_);
        modes_[task_id] = mode;
    }

private:
    std::condition_variable cv_;
    std::mutex mu_;
    std::set<std::string> started_;
    std::set<std::string> released_;
    std::map<std::string, Mode> modes_;
};

SdkImageEnhanceTaskRequest MakeRequest(const std::string& connection_id,
                                       const std::string& input_path,
                                       const std::string& output_dir) {
    SdkImageEnhanceTaskRequest request;
    request.connection_id = connection_id;
    request.input_paths.push_back(input_path);
    request.output_dir = output_dir;
    SdkImageEnhanceStep step;
    step.id = "blocking";
    step.type = "blocking";
    request.pipeline.steps.push_back(step);
    request.pipeline.target.type = "images";
    request.pipeline.target.format = "jpg";
    request.pipeline.target.output_dir = output_dir;
    return request;
}

class TempFixture {
public:
    TempFixture() {
        char directory_template[] = "/tmp/sdk-open-image-enhance-lifecycle-XXXXXX";
        char* directory = mkdtemp(directory_template);
        Require(directory != NULL, "failed to create unique lifecycle test directory");
        root = directory;
        input_path = root + "/input.jpg";
    }

    std::string root;
    std::string input_path;
};

void CreateInputFile(const TempFixture& fixture) {
    const std::string& path = fixture.input_path;
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
    output << "test image";
    Require(output.good(), "failed to create lifecycle input");
}

void TestCancelWaitsForWorkerExit(const TempFixture& fixture) {
    std::shared_ptr<BlockingImageEnhanceProvider> provider(new BlockingImageEnhanceProvider());
    ProviderBundle providers;
    providers.image_enhance_provider = provider;
    ImageEnhanceTaskService service(providers);
    service.SetEventSink([](const std::string&, const Json&) {
        throw std::runtime_error("event sink must not affect task submission");
    });

    const SdkImageEnhanceTaskResult start =
        service.StartTask(MakeRequest("owner-a", fixture.input_path, fixture.root + "/a"));
    Require(start.accepted, "task should be accepted even when event sink throws");
    Require(provider->WaitForStarted(start.task_id, 2000), "worker did not enter provider");

    const SdkImageEnhanceCancelRequest cancel_request = {start.task_id};
    const SdkImageEnhanceTaskResult cancel = service.CancelTask("owner-a", cancel_request);
    Require(cancel.accepted && cancel.task.cancel_requested, "cancel request should be accepted");
    Require(service.ActiveTaskCount() == 1, "cancel must not imply worker exit");

    std::atomic<bool> drain_returned(false);
    std::atomic<bool> drain_entered(false);
    std::thread drain([&service, &drain_returned, &drain_entered]() {
        drain_entered.store(true);
        service.CancelAndWait("owner-a");
        drain_returned.store(true);
    });
    RequireEventually([&drain_entered]() { return drain_entered.load(); }, 1000,
                      "drain thread did not start");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Require(!drain_returned.load(), "drain returned before provider exited");
    provider->Release(start.task_id);
    drain.join();
    Require(drain_returned.load() && service.ActiveTaskCount() == 0, "drain must wait for worker exit");
    service.CancelAndWait("owner-a");
}

void TestOwnerDrainDoesNotWaitForOtherOwner(const TempFixture& fixture) {
    std::shared_ptr<BlockingImageEnhanceProvider> provider(new BlockingImageEnhanceProvider());
    ProviderBundle providers;
    providers.image_enhance_provider = provider;
    ImageEnhanceTaskService service(providers);

    const SdkImageEnhanceTaskResult task_a =
        service.StartTask(MakeRequest("owner-a", fixture.input_path, fixture.root + "/a2"));
    const SdkImageEnhanceTaskResult task_b =
        service.StartTask(MakeRequest("owner-b", fixture.input_path, fixture.root + "/b2"));
    Require(task_a.accepted && task_b.accepted, "both tasks should be accepted");
    Require(provider->WaitForStarted(task_a.task_id, 2000), "owner A worker did not start");
    Require(provider->WaitForStarted(task_b.task_id, 2000), "owner B worker did not start");

    std::atomic<bool> drain_entered(false);
    std::thread drain([&service, &drain_entered]() {
        drain_entered.store(true);
        service.CancelAndWait("owner-a");
    });
    RequireEventually([&drain_entered]() { return drain_entered.load(); }, 1000,
                      "owner A drain thread did not start");
    provider->Release(task_a.task_id);
    drain.join();
    Require(service.ActiveTaskCount() == 1, "owner A drain must not wait for owner B");

    provider->Release(task_b.task_id);
    service.CancelAndWait("owner-b");
    service.CancelAndWait();
    Require(service.ActiveTaskCount() == 0, "all workers should be drained");
}

void TestFailureNotifiesDrain(const TempFixture& fixture) {
    std::shared_ptr<BlockingImageEnhanceProvider> provider(new BlockingImageEnhanceProvider());
    ProviderBundle providers;
    providers.image_enhance_provider = provider;
    ImageEnhanceTaskService service(providers);

    const SdkImageEnhanceTaskResult start =
        service.StartTask(MakeRequest("owner-failure", fixture.input_path, fixture.root + "/f"));
    Require(start.accepted, "failure task should be accepted");
    provider->SetMode(start.task_id, BlockingImageEnhanceProvider::kThrow);
    Require(provider->WaitForStarted(start.task_id, 2000), "failure worker did not start");

    std::atomic<bool> drain_entered(false);
    std::thread drain([&service, &drain_entered]() {
        drain_entered.store(true);
        service.CancelAndWait("owner-failure");
    });
    RequireEventually([&drain_entered]() { return drain_entered.load(); }, 1000,
                      "failure drain thread did not start");
    provider->Release(start.task_id);
    drain.join();
    const SdkImageEnhanceTaskSnapshot task = service.GetTask("owner-failure", start.task_id);
    Require(task.status == "failed", "provider exception should produce failed task");
    Require(service.ActiveTaskCount() == 0, "failed worker should leave active set");
    service.CancelAndWait("owner-failure");
}

void TestThreadStartFailureRollsBack(const TempFixture& fixture) {
    std::shared_ptr<BlockingImageEnhanceProvider> provider(new BlockingImageEnhanceProvider());
    ProviderBundle providers;
    providers.image_enhance_provider = provider;
    ImageEnhanceTaskService service(providers);

    g_fail_next_thread = true;
    bool threw = false;
    try {
        service.StartTask(MakeRequest("owner-start-failure", fixture.input_path, fixture.root + "/start-failure"));
    } catch (const std::exception&) {
        threw = true;
    }
    g_fail_next_thread = false;
    Require(threw, "injected pthread_create failure should be reported");
    Require(service.ActiveTaskCount() == 0, "failed thread start must restore active worker state");
    const SdkImageEnhanceTaskSnapshot missing = service.GetTask("owner-start-failure", "image-enhance-1");
    Require(missing.code == ToCode(SdkStatusCode::InvalidParams),
            "failed thread start must remove the task record");

    const SdkImageEnhanceTaskResult retry =
        service.StartTask(MakeRequest("owner-start-failure", fixture.input_path, fixture.root + "/retry"));
    Require(retry.accepted, "next task submission should succeed after thread start failure");
    Require(provider->WaitForStarted(retry.task_id, 1000), "retry worker did not start");
    provider->Release(retry.task_id);
    service.CancelAndWait("owner-start-failure");
    Require(service.ActiveTaskCount() == 0, "retry worker should drain");
}

void TestOutputPublisherLifecycle(const TempFixture& fixture) {
    std::shared_ptr<BlockingImageEnhanceProvider> provider(new BlockingImageEnhanceProvider());
    ProviderBundle providers;
    providers.image_enhance_provider = provider;
    ImageEnhanceTaskService service(providers);
    std::atomic<bool> entered(false), release(false), drained(false);
    struct ReleaseOnExit {
        std::atomic<bool>& flag;
        ~ReleaseOnExit() { flag.store(true); }
    } release_on_exit = {release};
    auto request = MakeRequest("publisher-owner", fixture.input_path, fixture.root + "/publisher");
    request.pipeline.steps.clear();
    const auto start = service.StartTask(request, [&entered, &release](const std::vector<std::string>& paths) {
        entered.store(true);
        RequireEventually([&release]() { return release.load(); }, 3000, "publisher not released");
        ImageEnhanceTaskService::OutputPublication result;
        result.output_paths = paths;
        return result;
    });
    Require(start.accepted, "publisher task accepted");
    RequireEventually([&entered]() { return entered.load(); }, 2000, "publisher did not start");
    const auto running = service.GetTask("publisher-owner", start.task_id);
    Require(running.status == "running" && running.phase == "converting" && running.output_paths.empty(),
            "publisher must not expose premature completed outputs");
    Require(service.ActiveTaskCount() == 1 && service.ClearFinishedTasks() == 0,
            "publisher must remain an active owned worker");
    const SdkImageEnhanceCancelRequest cancel = {start.task_id};
    Require(service.CancelTask("publisher-owner", cancel).task.cancel_requested,
            "publisher cancellation request must be accepted");
    std::thread drain([&service, &drained]() { service.CancelAndWait(); drained.store(true); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool waited = !drained.load();
    release.store(true);
    drain.join();
    Require(waited && service.ActiveTaskCount() == 0, "drain must wait for publisher exit");
    const auto done = service.GetTask("publisher-owner", start.task_id);
    Require(done.status == "completed" && done.cancel_requested && !done.output_paths.empty(),
            "publication already begun can complete while preserving cancellation request");
}

void TestOutputPublisherFailures(const TempFixture& fixture) {
    std::shared_ptr<BlockingImageEnhanceProvider> provider(new BlockingImageEnhanceProvider());
    ProviderBundle providers;
    providers.image_enhance_provider = provider;
    ImageEnhanceTaskService service(providers);
    for (int mode = 0; mode < 4; ++mode) {
        auto request = MakeRequest("publisher-failure", fixture.input_path,
                                   fixture.root + "/publication-failure-" + std::to_string(mode));
        request.pipeline.steps.clear();
        const auto start = service.StartTask(request, [mode](const std::vector<std::string>&) {
            if (mode == 1) throw std::runtime_error("publisher exception");
            ImageEnhanceTaskService::OutputPublication result;
            if (mode == 0) {
                result.code = ToCode(SdkStatusCode::InvalidParams);
                result.message = "output collision";
            }
            if (mode == 3) result.output_paths.push_back("");
            // modes 2/3: empty success vector or empty path is invalid.
            return result;
        });
        Require(start.accepted, "failure publisher task accepted");
        RequireEventually([&service]() { return service.ActiveTaskCount() == 0; }, 2000,
                          "failure publisher did not exit");
        const auto task = service.GetTask("publisher-failure", start.task_id);
        Require(task.status == "failed" && task.assets.empty() && task.output_paths.empty() &&
                task.output_path.empty() && !task.error.empty(), "publisher failure must not expose successful outputs");
        if (mode == 0) Require(task.code == ToCode(SdkStatusCode::InvalidParams), "publication error code preserved");
    }
}

void TestOutputPublisherAdmissionAndLegacy(const TempFixture& fixture) {
    std::shared_ptr<BlockingImageEnhanceProvider> provider(new BlockingImageEnhanceProvider());
    ProviderBundle providers;
    providers.image_enhance_provider = provider;
    ImageEnhanceTaskService service(providers);
    std::atomic<int> publisher_calls(0);
    auto request = MakeRequest("before-publisher", fixture.input_path, fixture.root + "/before-publisher");
    const auto start = service.StartTask(request, [&publisher_calls](const std::vector<std::string>& paths) {
        ++publisher_calls;
        ImageEnhanceTaskService::OutputPublication result;
        result.output_paths = paths;
        return result;
    });
    struct ReleaseProvider {
        std::shared_ptr<BlockingImageEnhanceProvider> provider;
        std::string id;
        ~ReleaseProvider() { provider->Release(id); }
    } release_provider = {provider, start.task_id};
    Require(provider->WaitForStarted(start.task_id, 2000), "pre-publication provider not entered");
    const SdkImageEnhanceCancelRequest cancel = {start.task_id};
    service.CancelTask("before-publisher", cancel);
    provider->Release(start.task_id);
    service.CancelAndWait();
    const auto cancelled = service.GetTask("before-publisher", start.task_id);
    Require(publisher_calls.load() == 0 && cancelled.status == "cancelled" && cancelled.cancel_requested,
            "cancel before publication admission must not call publisher");

    request.connection_id = "legacy-owner";
    request.output_dir = fixture.root + "/legacy";
    request.pipeline.target.output_dir = request.output_dir;
    request.pipeline.target.output_path = fixture.root + "/legacy-ignored.jpg";
    request.pipeline.steps.clear();
    const auto legacy = service.StartTask(request);
    RequireEventually([&service]() { return service.ActiveTaskCount() == 0; }, 2000, "legacy worker not exited");
    const auto done = service.GetTask("legacy-owner", legacy.task_id);
    Require(done.status == "completed" && done.output_path.find(request.output_dir + "/") == 0 &&
            done.output_path != request.pipeline.target.output_path,
            "Open one-argument entry must preserve legacy image output naming");
}

} // namespace
} // namespace sdk
} // namespace editor

int main() {
    try {
        const editor::sdk::TempFixture fixture;
        editor::sdk::CreateInputFile(fixture);
        editor::sdk::TestCancelWaitsForWorkerExit(fixture);
        editor::sdk::TestOwnerDrainDoesNotWaitForOtherOwner(fixture);
        editor::sdk::TestFailureNotifiesDrain(fixture);
        editor::sdk::TestThreadStartFailureRollsBack(fixture);
        editor::sdk::TestOutputPublisherLifecycle(fixture);
        editor::sdk::TestOutputPublisherFailures(fixture);
        editor::sdk::TestOutputPublisherAdmissionAndLegacy(fixture);
        std::cout << "image enhance task lifecycle tests passed" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "image enhance task lifecycle tests failed: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "image enhance task lifecycle tests failed: unknown exception" << std::endl;
        return 1;
    }
}
