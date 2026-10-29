#!/usr/bin/env python3
"""Static contracts for the Harmony AppService USB lifecycle boundary."""

from pathlib import Path
import re
import unittest


SDK_ROOT = Path(__file__).resolve().parents[1]
HARMONY = SDK_ROOT / "platform" / "harmony"


def read(relative: str) -> str:
    return (HARMONY / relative).read_text(encoding="utf-8")


def ordered(text: str, *needles: str) -> None:
    cursor = -1
    for needle in needles:
        position = text.find(needle, cursor + 1)
        if position < 0:
            raise AssertionError(f"missing lifecycle marker: {needle}")
        if position <= cursor:
            raise AssertionError(f"lifecycle marker is out of order: {needle}")
        cursor = position


class HarmonyLifecycleContractTest(unittest.TestCase):
    def test_napi_start_and_stop_are_async_work(self) -> None:
        napi = read("native/sdk_open_harmony_napi.cpp")
        types = read("sdkopen/src/main/cpp/types/libsdkopen_harmony/Index.d.ts")
        self.assertIn("napi_create_async_work", napi)
        self.assertIn("napi_queue_async_work", napi)
        self.assertIn("return QueueLifecycle(env, true", napi)
        self.assertIn("return QueueLifecycle(env, false", napi)
        self.assertIn("start: (workDir: string, webRoot: string) => Promise<string>", types)
        self.assertIn("stop: () => Promise<string>", types)

    def test_status_is_lock_free_for_the_arkts_dispatcher(self) -> None:
        host = read("native/sdk_open_harmony_host.cpp")
        is_running = host.split("bool IsRunning()", 1)[1].split(
            "std::string Status()", 1
        )[0]
        status = host.split("std::string Status()", 1)[1].split(
            "} // namespace harmony", 1
        )[0]
        self.assertIn("g_running.load()", is_running)
        self.assertIn("g_running.load()", status)
        self.assertNotIn("g_mutex", is_running)
        self.assertNotIn("g_mutex", status)
        stop = host.split("void Stop()", 1)[1].split("bool IsRunning()", 1)[0]
        ordered(stop, "g_running.store(false)", "std::lock_guard<std::mutex> lock(g_mutex)")

    def test_start_never_replaces_a_live_runtime_after_early_stop_state(self) -> None:
        host = read("native/sdk_open_harmony_host.cpp")
        start = host.split("bool Start", 1)[1].split("void Stop()", 1)[0]
        ordered(
            start,
            "if (g_app)",
            "if (g_app->IsRunning())",
            "g_app.reset()",
            "std::unique_ptr<SdkApp> app(new SdkApp",
        )

    def test_usb_bridge_is_installed_before_runtime_start(self) -> None:
        service = read("sdkopen/src/main/ets/services/SdkOpenServiceExtension.ets")
        ordered(
            service,
            "copyRawTree(this.context.resourceManager, RAW_DEVICE_CONFIG",
            "if (!this.usbHost.attach())",
            "await sdkOpen.start(workDir, webRoot)",
        )
        self.assertIn("const sharedUsbHost: UsbHostTransport", service)

    def test_usb_transport_only_calls_declared_native_bridge_methods(self) -> None:
        transport = read("sdkopen/src/main/ets/services/UsbHostTransport.ets")
        calls = set(re.findall(r"sdkOpen\.([A-Za-z0-9_]+)", transport))
        self.assertEqual(
            calls,
            {
                "attachUsbHost",
                "completeUsbHostRequest",
                "failUsbHost",
                "releaseUsbHostFd",
                "startUsbHostNativeIso",
                "stopUsbHostNativeIso",
                "submitUsbHostTransfer",
            },
        )
        self.assertIn("result.devices.push", transport)

    def test_stop_keeps_dispatcher_alive_until_native_release_finishes(self) -> None:
        service = read("sdkopen/src/main/ets/services/SdkOpenServiceExtension.ets")
        host = read("native/sdk_open_harmony_host.cpp")
        transport = read("sdkopen/src/main/ets/services/UsbHostTransport.ets")
        ordered(
            service,
            "await this.startup.catch",
            "await sdkOpen.stop()",
            "await this.usbHost.shutdownAfterNativeStop()",
        )
        ordered(
            host,
            "g_app->Stop()",
            "g_app.reset()",
            "DeviceManager::GetInstance()->release()",
        )
        shutdown = transport.split("async shutdownAfterNativeStop", 1)[1].split(
            "private serializeLifecycle", 1
        )[0]
        self.assertNotIn("sdkOpen.stop", shutdown)
        self.assertIn("await this.stopTransfers()", shutdown)
        self.assertIn("this.closePipe()", shutdown)

    def test_prepare_stop_is_private_and_awaited_before_ability_stop(self) -> None:
        service = read("sdkopen/src/main/ets/services/SdkOpenServiceExtension.ets")
        tray = read("sdkopen_tray/src/main/ets/common/TrayServiceControl.ets")
        request = service.split("if (code === REQUEST_PREPARE_STOP)", 1)[1].split(
            "return false", 1
        )[0]
        ordered(
            request,
            "rpc.IPCSkeleton.getCallingUid() !== process.uid",
            "return this.prepareStop()",
        )
        prepare = tray.split("private async prepareStopIfRunning", 1)[1].split(
            "async stopForExit", 1
        )[0]
        ordered(
            prepare,
            "if (!await this.readStatus())",
            "connectAppServiceExtensionAbility",
            "this.sendPrepareStop(remote)",
        )
        exit_stop = tray.split("async stopForExit", 1)[1].split(
            "async execute", 1
        )[0]
        ordered(
            exit_stop,
            "await this.prepareStopIfRunning()",
            "stopAppServiceExtensionAbility",
        )
        command_stop = tray.split("command === 'stop'", 1)[1]
        ordered(
            command_stop,
            "await this.prepareStopIfRunning()",
            "stopAppServiceExtensionAbility",
        )
        self.assertIn("Service cleanup connection disconnect failed", prepare)
        self.assertIn("resolve();", prepare)

    def test_system_teardown_has_nonblocking_native_cleanup_fallback(self) -> None:
        service = read("sdkopen/src/main/ets/services/SdkOpenServiceExtension.ets")
        napi = read("native/sdk_open_harmony_napi.cpp")
        destroy = service.split("onDestroy(): void", 1)[1].split(
            "onConnect", 1
        )[0]
        self.assertIn("this.prepareStop().catch", destroy)
        self.assertNotIn("await ", destroy)
        self.assertIn("napi_add_async_cleanup_hook", (SDK_ROOT.parent / "module" / "devices" /
            "usb_host_napi_ohos.cpp").read_text(encoding="utf-8"))
        self.assertIn("RegisterUsbHostNapiOhos", napi)


if __name__ == "__main__":
    unittest.main()
