#!/usr/bin/env python3
"""Executable contracts for the Harmony resident-tray lifecycle boundary."""

from pathlib import Path
import json
import re
import subprocess
import unittest


SDK_ROOT = Path(__file__).resolve().parents[1]
HARMONY = SDK_ROOT / "platform" / "harmony"
TYPESCRIPT_JS = Path(
    "/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor/"
    "node_modules/typescript/lib/typescript.js"
)


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


NODE_DRIVER = r"""
const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const input = JSON.parse(fs.readFileSync(0, 'utf8'));
const ts = require(input.typescript);

function load(file, stubs) {
  const result = ts.transpileModule(fs.readFileSync(file, 'utf8'), {
    compilerOptions: {
      module: ts.ModuleKind.CommonJS,
      target: ts.ScriptTarget.ES2020,
      esModuleInterop: false,
      importsNotUsedAsValues: ts.ImportsNotUsedAsValues.Remove
    },
    fileName: file,
    reportDiagnostics: true
  });
  const errors = (result.diagnostics || []).filter(
    (item) => item.category === ts.DiagnosticCategory.Error);
  assert.deepStrictEqual(errors.map((item) => item.messageText), []);
  const module = { exports: {} };
  const localRequire = (name) => {
    if (Object.prototype.hasOwnProperty.call(stubs, name)) return stubs[name];
    throw new Error(`Unexpected import from ${file}: ${name}`);
  };
  const wrapper = vm.runInNewContext(
    `(function(require, module, exports) {${result.outputText}\n})`,
    { console, setTimeout, clearTimeout, Promise, Uint8Array, ArrayBuffer, Error, String });
  wrapper(localRequire, module, module.exports);
  return module.exports;
}

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((ok, fail) => { resolve = ok; reject = fail; });
  return { promise, resolve, reject };
}
const tick = () => new Promise((resolve) => setTimeout(resolve, 0));
const success = '{"success":true}';

async function runtimeScenario(name) {
  const events = [];
  const instances = [];
  const controls = {
    starts: [], stops: [], shutdowns: [], writes: [], reads: [],
    existingPaths: new Set(), openedPaths: [], readAttempts: new Map(),
    readFailures: new Map()
  };
  class FakeUsbHostTransport {
    constructor() { instances.push(this); }
    attach() { events.push('usb.attach'); return true; }
    async shutdownAfterNativeStop() {
      events.push('usb.shutdown');
      const action = controls.shutdowns.shift();
      if (action) await action();
    }
  }
  const native = {
    async start() {
      events.push('native.start');
      const action = controls.starts.shift();
      return action ? await action() : success;
    },
    async stop() {
      events.push('native.stop');
      const action = controls.stops.shift();
      return action ? await action() : success;
    }
  };
  let nextFd = 1;
  const fileIo = {
    OpenMode: { READ_WRITE: 1, CREATE: 2, TRUNC: 4 },
    async mkdir() {},
    async open(path) {
      events.push('file.open');
      controls.openedPaths.push(path);
      controls.existingPaths.add(path);
      return { fd: nextFd++ };
    },
    async write(fd, buffer, options) {
      events.push(`file.write:${options.offset}:${buffer.byteLength}`);
      const action = controls.writes.shift();
      return action ? await action(fd, buffer, options) : buffer.byteLength;
    },
    async close() { events.push('file.close'); },
    async unlink() {},
    async stat(path) {
      if (controls.existingPaths.has(path)) return {};
      throw new Error('missing');
    }
  };
  const resourceManager = {
    async getRawFileList() { throw new Error('leaf'); },
    async getRawFileContent(path) {
      if (path === 'sdkopen_czocr_model_version.txt') {
        return new Uint8Array(Array(64).fill(97));
      }
      events.push(`resource.read:${path}`);
      controls.readAttempts.set(path, (controls.readAttempts.get(path) || 0) + 1);
      const failures = controls.readFailures.get(path) || 0;
      if (failures > 0) {
        controls.readFailures.set(path, failures - 1);
        throw new Error(`resource read failed: ${path}`);
      }
      const action = controls.reads.shift();
      return action ? await action(path) : new Uint8Array([1, 2, 3]);
    }
  };
  const exports = load(input.runtime, {
    '@kit.AbilityKit': {},
    '@kit.CoreFileKit': { fileIo },
    '@kit.PerformanceAnalysisKit': { hilog: { info() {}, warn() {}, error() {} } },
    '@ohos.resourceManager': { __esModule: true, default: {} },
    'libsdkopen_harmony.so': { __esModule: true, default: native },
    './UsbHostTransport': { UsbHostTransport: FakeUsbHostTransport }
  });
  const runtime = new exports.SdkOpenRuntime();
  runtime.initialize({ filesDir: '/tmp/sdkopen-test', resourceManager });
  const lifecycleEvents = () => events.filter((event) =>
    event.startsWith('usb.') || event.startsWith('native.'));

  if (name === 'restart') {
    await runtime.start();
    await runtime.start();
    await runtime.stop();
    await runtime.stop();
    await runtime.start();
    await runtime.stop();
    assert.deepStrictEqual(lifecycleEvents(), [
      'usb.attach', 'native.start', 'native.stop', 'usb.shutdown',
      'usb.attach', 'native.start', 'native.stop', 'usb.shutdown'
    ]);
    assert.strictEqual(instances.length, 2);
    return;
  }
  if (name === 'exit-during-start') {
    const gate = deferred();
    controls.starts.push(() => gate.promise);
    const starting = runtime.start();
    await tick();
    const exiting = runtime.stopForExit();
    await tick();
    assert.deepStrictEqual(lifecycleEvents(), ['usb.attach', 'native.start']);
    gate.resolve(success);
    await starting;
    await exiting;
    assert.deepStrictEqual(lifecycleEvents(), [
      'usb.attach', 'native.start', 'native.stop', 'usb.shutdown'
    ]);
    await assert.rejects(runtime.start(), /exiting/);
    return;
  }
  if (name === 'exit-rejects-queued-start') {
    const gate = deferred();
    controls.starts.push(() => gate.promise);
    const starting = runtime.start();
    await tick();
    const queuedStart = runtime.start();
    const exiting = runtime.stopForExit();
    gate.resolve(success);
    await starting;
    await assert.rejects(queuedStart, /exiting/);
    await exiting;
    assert.deepStrictEqual(lifecycleEvents(), [
      'usb.attach', 'native.start', 'native.stop', 'usb.shutdown'
    ]);
    return;
  }
  if (name === 'start-failure') {
    controls.starts.push(async () => { throw new Error('start exploded'); });
    await assert.rejects(runtime.start(), /start exploded/);
    assert.deepStrictEqual(lifecycleEvents(), [
      'usb.attach', 'native.start', 'native.stop', 'usb.shutdown'
    ]);
    await runtime.start();
    await runtime.stop();
    assert.deepStrictEqual(lifecycleEvents().slice(4), [
      'usb.attach', 'native.start', 'native.stop', 'usb.shutdown'
    ]);
    return;
  }
  if (name === 'stop-failure') {
    await runtime.start();
    controls.stops.push(async () => { throw new Error('stop exploded'); });
    await assert.rejects(runtime.stop(), /stop exploded/);
    assert.deepStrictEqual(lifecycleEvents(), ['usb.attach', 'native.start', 'native.stop']);
    await runtime.stop();
    assert.deepStrictEqual(lifecycleEvents(), [
      'usb.attach', 'native.start', 'native.stop', 'native.stop', 'usb.shutdown'
    ]);
    await runtime.start();
    await runtime.stop();
    return;
  }
  if (name === 'exit-retry') {
    await runtime.start();
    controls.stops.push(async () => { throw new Error('exit stop exploded'); });
    await assert.rejects(runtime.stopForExit(), /exit stop exploded/);
    await runtime.stopForExit();
    assert.deepStrictEqual(lifecycleEvents(), [
      'usb.attach', 'native.start', 'native.stop', 'native.stop', 'usb.shutdown'
    ]);
    await assert.rejects(runtime.start(), /exiting/);
    return;
  }
  if (name === 'native-stop-false') {
    await runtime.start();
    controls.stops.push(async () => '{"success":false,"error":"busy"}');
    await assert.rejects(runtime.stop(), /stop failed/);
    await runtime.start();
    assert.deepStrictEqual(lifecycleEvents(), [
      'usb.attach', 'native.start', 'native.stop',
      'native.stop', 'usb.shutdown', 'usb.attach', 'native.start'
    ]);
    await runtime.stop();
    return;
  }
  if (name === 'usb-shutdown-failure') {
    await runtime.start();
    controls.shutdowns.push(async () => { throw new Error('usb shutdown exploded'); });
    await assert.rejects(runtime.stop(), /usb shutdown exploded/);
    await runtime.start();
    assert.deepStrictEqual(lifecycleEvents(), [
      'usb.attach', 'native.start', 'native.stop', 'usb.shutdown',
      'native.stop', 'usb.shutdown', 'usb.attach', 'native.start'
    ]);
    await runtime.stop();
    return;
  }
  if (name === 'partial-write') {
    controls.reads.push(async () => new Uint8Array([1, 2, 3]));
    controls.writes.push(async () => 1);
    await runtime.start();
    const writes = events.filter((event) => event.startsWith('file.write:'));
    assert.deepStrictEqual(writes.slice(0, 2), ['file.write:0:3', 'file.write:1:2']);
    await runtime.stop();
    return;
  }
  if (name === 'write-failure') {
    controls.writes.push(async () => { throw new Error('disk full'); });
    await assert.rejects(runtime.start(), /disk full/);
    assert.deepStrictEqual(lifecycleEvents(), []);
    assert.deepStrictEqual(events.filter((event) => event.startsWith('file.')), [
      'file.open', 'file.write:0:3', 'file.close'
    ]);
    return;
  }
  if (name === 'czcv-model-v1-upgrade') {
    const modelDir = '/tmp/sdkopen-test/sdk-open/runtime/czcv_model';
    controls.existingPaths.add(`${modelDir}/.sdkopen-czcv-model-v1`);
    await runtime.start();
    assert.strictEqual(controls.readAttempts.get('sdkopen_czcv_model'), 1);
    assert(controls.openedPaths.includes(`${modelDir}/.sdkopen-czcv-model-v2`));
    await runtime.stop();
    return;
  }
  if (name === 'czcv-model-copy-retry') {
    const stampPath =
      '/tmp/sdkopen-test/sdk-open/runtime/czcv_model/.sdkopen-czcv-model-v2';
    controls.readFailures.set('sdkopen_czcv_model', 1);
    await assert.rejects(runtime.start(), /resource read failed: sdkopen_czcv_model/);
    assert.strictEqual(controls.readAttempts.get('sdkopen_czcv_model'), 1);
    assert(!controls.openedPaths.includes(stampPath));
    await runtime.start();
    assert.strictEqual(controls.readAttempts.get('sdkopen_czcv_model'), 2);
    assert(controls.openedPaths.includes(stampPath));
    await runtime.stop();
    return;
  }
  if (name === 'exit-during-staging') {
    const writeGate = deferred();
    controls.reads.push(async () => new Uint8Array(2 * 1024 * 1024 + 1));
    controls.writes.push(() => writeGate.promise);
    const starting = runtime.start();
    for (let attempt = 0; attempt < 20 &&
        !events.some((event) => event.startsWith('file.write:')); ++attempt) {
      await tick();
    }
    assert(events.some((event) => event.startsWith('file.write:')),
      'resource staging did not reach the gated write');
    const exiting = runtime.stopForExit();
    writeGate.resolve(1024 * 1024);
    await assert.rejects(starting, /exiting/);
    await exiting;
    assert.deepStrictEqual(lifecycleEvents(), []);
    assert.strictEqual(events.filter((event) => event.startsWith('resource.read:')).length, 1);
    assert.strictEqual(events.filter((event) => event.startsWith('file.write:')).length, 1);
    assert(events.includes('file.close'));
    return;
  }
  throw new Error(`Unknown runtime scenario: ${name}`);
}

function controllerStubs(status) {
  return {
    '@kit.AbilityKit': { contextConstant: {
      ProcessMode: { ATTACH_TO_STATUS_BAR_ITEM: 'attach' },
      StartupVisibility: { STARTUP_HIDE: 'hide' }
    } },
    '@kit.NetworkKit': { http: {
      RequestMethod: { GET: 'GET' },
      createHttp() { return {
        async request() {
          return { responseCode: 200, result: `{"running":${status.running}}` };
        },
        destroy() {}
      }; }
    } },
    '@kit.CoreFileKit': { fileIo: {
      OpenMode: { READ_ONLY: 1 },
      async stat(path) {
        status.certificatePath = path;
        return { size: 3, isFile() { return true; } };
      },
      async open() { return { fd: 1 }; },
      async read(fd, buffer) {
        new Uint8Array(buffer).set([1, 2, 3]);
        return 3;
      },
      async close() { status.certificateClosed = true; }
    } },
    '@kit.DeviceCertificateKit': { certificateManagerDialog: {
      CertificateType: { CA_CERT: 1 },
      CertificateScope: { CURRENT_USER: 1 },
      CertificateDialogErrorCode: { ERROR_OPERATION_CANCELED: 29700002 },
      async openInstallCertificateDialog(context, type, scope, certificate) {
        status.certificateDialog = { context, type, scope, certificate };
        return 'certificate://current-user/root';
      }
    } },
    '@kit.PerformanceAnalysisKit': { hilog: { info() {}, warn() {}, error() {} } }
  };
}

async function controllerScenario() {
  const status = { running: false };
  const ownerEvents = [];
  const runtime = {
    async start() { ownerEvents.push('start'); status.running = true; },
    async stop() { ownerEvents.push('stop'); status.running = false; },
    async stopForExit() { ownerEvents.push('exit'); status.running = false; }
  };
  const controllerExports = load(input.controller, controllerStubs(status));
  const owner = controllerExports.trayServiceControl;
  const ownerContext = { owner: true, filesDir: '/tmp/sdkopen-owner' };
  owner.initialize(ownerContext, runtime);
  await owner.execute('start');
  const certificateAbilityContext = { filesDir: '/tmp/sdkopen-certificate-ui' };
  await controllerExports.installHttpsRootCertificate(certificateAbilityContext);
  assert.strictEqual(status.certificatePath,
    '/tmp/sdkopen-certificate-ui/sdk-open/runtime/tls/local-runtime-root-ca.crt');
  assert.strictEqual(status.certificateDialog.context, certificateAbilityContext);
  assert.strictEqual(status.certificateDialog.type, 1);
  assert.strictEqual(status.certificateDialog.scope, 1);
  assert.deepStrictEqual(Array.from(status.certificateDialog.certificate), [1, 2, 3]);
  assert.strictEqual(status.certificateClosed, true);
  await owner.execute('stop');
  await owner.stopForExit();
  assert.deepStrictEqual(ownerEvents, ['start', 'stop', 'exit']);

  const popupEvents = [];
  const popup = load(input.controller, controllerStubs(status)).trayServiceControl;
  const popupContext = { filesDir: '/tmp/sdkopen-test', async startAbility(want, options) {
    popupEvents.push({ want, options });
    const command = want.parameters && want.parameters.serviceCommand;
    if (command) status.running = command === 'start';
  } };
  popup.initializePopup(popupContext);
  await popup.execute('start');
  await popup.installHttpsCertificate();
  await popup.execute('stop');
  await popup.openDemo();
  await popup.openAdmin();
  assert.strictEqual(popupEvents.length, 5);
  assert.strictEqual(popupEvents[0].want.bundleName, 'com.czur.opensdk');
  assert.strictEqual(popupEvents[0].want.parameters.serviceCommand, 'start');
  assert.strictEqual(popupEvents[0].options.processMode, 'attach');
  assert.strictEqual(popupEvents[0].options.startupVisibility, 'hide');
  assert.strictEqual(popupEvents[1].want.abilityName, 'HttpsCertificateInstallAbility');
  assert.strictEqual(popupEvents[1].want.parameters, undefined);
  assert.strictEqual(popupEvents[1].options, undefined);
  assert.strictEqual(popupEvents[2].want.parameters.serviceCommand, 'stop');
  assert.strictEqual(popupEvents[2].options.processMode, 'attach');
  assert.strictEqual(popupEvents[2].options.startupVisibility, 'hide');
  assert.strictEqual(popupEvents[3].want.uri, 'http://127.0.0.1:17081/');
  assert.strictEqual(popupEvents[3].options, undefined);
  assert.strictEqual(popupEvents[4].want.action, 'ohos.want.action.viewData');
  assert.strictEqual(popupEvents[4].want.uri, 'http://127.0.0.1:17080/');
  assert.deepStrictEqual(Array.from(popupEvents[4].want.entities), ['entity.system.browsable']);
  assert.strictEqual(popupEvents[4].options, undefined);
}

async function abilityScenario() {
  const popupStatus = { running: false };
  let coldWant;
  const popup = load(input.controller, controllerStubs(popupStatus)).trayServiceControl;
  popup.initializePopup({ async startAbility(want) {
    coldWant = want;
    popupStatus.running = true;
  } });
  await popup.execute('start');
  assert(coldWant, 'popup did not route a cold-start command to the resident ability');

  const events = [];
  const context = {
    async hideAbility() { events.push('hide'); },
    resourceManager: { getRawFileContentSync() { return new Uint8Array(); } }
  };
  class UIAbility { constructor() { this.context = context; } }
  const runtime = {
    initialize(value) { assert.strictEqual(value, context); events.push('runtime.initialize'); },
    async stop() { events.push('runtime.stop'); }
  };
  const control = {
    initialize(value, injected) {
      assert.strictEqual(value, context);
      assert.strictEqual(injected, runtime);
      events.push('control.initialize');
    },
    execute(command) { events.push(`execute:${command}`); return Promise.resolve(); },
    stopForExit() { events.push('stopForExit'); return Promise.resolve(); }
  };
  const exports = load(input.ability, {
    '@kit.AbilityKit': {
      UIAbility,
      contextConstant: {
        ProcessMode: { ATTACH_TO_STATUS_BAR_ITEM: 1 },
        StartupVisibility: { STARTUP_HIDE: 1 }
      }
    },
    '@kit.BasicServicesKit': { BusinessError: class extends Error {}, emitter: {} },
    '@kit.DeskTopExtensionKit': {
      statusBarManager: {
        on() { events.push('tray.on'); },
        off() { events.push('tray.off'); },
        addToStatusBar() {}
      }
    },
    '@kit.ImageKit': { image: {} },
    '@kit.PerformanceAnalysisKit': { hilog: { info() {}, warn() {}, error() {} } },
    '@kit.ArkUI': { window: {} },
    '../common/TrayServiceControl': { trayServiceControl: control },
    '../services/SdkOpenRuntime': { sdkOpenRuntime: runtime }
  });
  const ability = new exports.default();
  ability.onCreate(coldWant);
  ability.onWindowStageCreate({});
  await tick();
  assert.deepStrictEqual(events, [
    'runtime.initialize', 'control.initialize', 'tray.on', 'execute:start', 'hide'
  ]);
  ability.onDestroy();
  await tick();
  assert.deepStrictEqual(events.slice(-2), ['tray.off', 'runtime.stop']);
  assert(!events.includes('stopForExit'),
    'ordinary ability destruction must not make same-process restart final');

  const rebuilt = new exports.default();
  rebuilt.onCreate(coldWant);
  await tick();
  assert.deepStrictEqual(events.slice(-4), [
    'runtime.initialize', 'control.initialize', 'tray.on', 'execute:start'
  ]);
}

function certificateAbilityScenario() {
  const events = [];
  class UIAbility {}
  const exports = load(input.certificateAbility, {
    '@kit.AbilityKit': { UIAbility },
    '@kit.ArkUI': { window: {} },
    '@kit.PerformanceAnalysisKit': { hilog: { info() {}, error() {} } }
  });
  const ability = new exports.default();
  ability.onWindowStageCreate({
    loadContent(page, callback) {
      events.push(page);
      callback({ code: 0 });
    }
  });
  assert.deepStrictEqual(events, ['pages/HttpsCertificateInstall']);
}

(async () => {
  if (input.kind === 'runtime') await runtimeScenario(input.scenario);
  else if (input.kind === 'controller') await controllerScenario();
  else if (input.kind === 'ability') await abilityScenario();
  else if (input.kind === 'certificate-ability') certificateAbilityScenario();
  else throw new Error(`Unknown driver kind: ${input.kind}`);
})().catch((error) => {
  console.error(error && error.stack ? error.stack : error);
  process.exitCode = 1;
});
"""


class HarmonyLifecycleContractTest(unittest.TestCase):
    def run_node(self, kind: str, scenario: str = "") -> None:
        if not TYPESCRIPT_JS.is_file():
            self.skipTest("DevEco TypeScript runtime is unavailable")
        payload = {
            "kind": kind,
            "scenario": scenario,
            "typescript": str(TYPESCRIPT_JS),
            "runtime": str(HARMONY / "sdkopen_tray/src/main/ets/services/SdkOpenRuntime.ets"),
            "controller": str(HARMONY / "sdkopen_tray/src/main/ets/common/TrayServiceControl.ets"),
            "ability": str(HARMONY / "sdkopen_tray/src/main/ets/entryability/SdkOpenTrayAbility.ets"),
            "certificateAbility": str(
                HARMONY / "sdkopen_tray/src/main/ets/entryability/HttpsCertificateInstallAbility.ets"
            ),
        }
        result = subprocess.run(
            ["node", "-e", NODE_DRIVER],
            input=json.dumps(payload), text=True, capture_output=True,
            timeout=20, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)

    def test_napi_start_and_stop_are_async_work(self) -> None:
        napi = read("native/sdk_open_harmony_napi.cpp")
        types = read("sdkopen_tray/src/main/cpp/types/libsdkopen_harmony/Index.d.ts")
        self.assertIn("napi_create_async_work", napi)
        self.assertIn("napi_queue_async_work", napi)
        self.assertIn("return QueueLifecycle(env, true", napi)
        self.assertIn("return QueueLifecycle(env, false", napi)
        self.assertIn("start: (workDir: string, webRoot: string) => Promise<string>", types)
        self.assertIn("stop: () => Promise<string>", types)

    def test_native_status_remains_lock_free_for_arkts_dispatcher(self) -> None:
        host = read("native/sdk_open_harmony_host.cpp")
        is_running = host.split("bool IsRunning()", 1)[1].split("std::string Status()", 1)[0]
        status = host.split("std::string Status()", 1)[1].split("} // namespace harmony", 1)[0]
        self.assertIn("g_running.load()", is_running)
        self.assertIn("g_running.load()", status)
        self.assertNotIn("g_mutex", is_running)
        self.assertNotIn("g_mutex", status)
        stop = host.split("void Stop()", 1)[1].split("bool IsRunning()", 1)[0]
        ordered(stop, "g_running.store(false)", "std::lock_guard<std::mutex> lock(g_mutex)")

    def test_usb_transport_only_calls_declared_native_bridge_methods(self) -> None:
        transport = read("sdkopen_tray/src/main/ets/services/UsbHostTransport.ets")
        calls = set(re.findall(r"sdkOpen\.([A-Za-z0-9_]+)", transport))
        self.assertEqual(calls, {
            "attachUsbHost", "completeUsbHostRequest", "failUsbHost",
            "releaseUsbHostFd", "startUsbHostNativeIso", "stopUsbHostNativeIso",
            "submitUsbHostTransfer",
        })
        self.assertIn("result.devices.push", transport)

    def test_runtime_can_stop_and_restart_without_duplicate_work(self) -> None:
        self.run_node("runtime", "restart")

    def test_exit_during_start_waits_for_start_then_cleans_up(self) -> None:
        self.run_node("runtime", "exit-during-start")

    def test_exit_rejects_a_start_that_was_only_queued(self) -> None:
        self.run_node("runtime", "exit-rejects-queued-start")

    def test_start_failure_cleans_up_and_allows_retry(self) -> None:
        self.run_node("runtime", "start-failure")

    def test_stop_failure_retains_usb_and_allows_retry(self) -> None:
        self.run_node("runtime", "stop-failure")

    def test_native_stop_error_blocks_new_session_until_cleanup_retries(self) -> None:
        self.run_node("runtime", "native-stop-false")

    def test_usb_shutdown_error_blocks_new_session_until_cleanup_retries(self) -> None:
        self.run_node("runtime", "usb-shutdown-failure")

    def test_failed_exit_can_retry_but_successful_exit_is_final(self) -> None:
        self.run_node("runtime", "exit-retry")

    def test_partial_resource_writes_are_completed(self) -> None:
        self.run_node("runtime", "partial-write")

    def test_resource_write_failure_closes_file_without_starting_native(self) -> None:
        self.run_node("runtime", "write-failure")

    def test_czcv_model_v1_stamp_is_upgraded_to_complete_model_set(self) -> None:
        self.run_node("runtime", "czcv-model-v1-upgrade")

    def test_czcv_model_copy_failure_retries_without_completion_stamp(self) -> None:
        self.run_node("runtime", "czcv-model-copy-retry")

    def test_czcv_packaging_uses_complete_architecture_independent_models(self) -> None:
        hvigor = read("sdkopen_tray/hvigorfile.ts")
        runtime = read("sdkopen_tray/src/main/ets/services/SdkOpenRuntime.ets")
        self.assertIn(
            "const modelSourceRoot = path.join(thirdPartyRoot, 'czcv/models/x86-64');",
            hvigor,
        )
        self.assertNotIn("czcv/models/arm64", hvigor)
        self.assertIn(".sdkopen-czcv-model-v2", runtime)
        model_root = SDK_ROOT.parents[1] / "third_party_repo" / "czcv" / "models" / "x86-64"
        expected = {
            "czcv0005_01.bin", "czcv0005_02.bin", "czcv0005_03.bin", "czcv0005_04.bin",
            "czcv0008_01.bin", "czcv0008_02.bin", "czcv0019_01.bin", "czcv0019_02.bin",
        }
        self.assertEqual({path.name for path in model_root.iterdir()} & expected, expected)

    def test_exit_during_resource_staging_cancels_before_usb_attach(self) -> None:
        self.run_node("runtime", "exit-during-staging")

    def test_popup_routes_commands_without_owning_runtime(self) -> None:
        controller = read("sdkopen_tray/src/main/ets/common/TrayServiceControl.ets")
        self.assertNotIn("libsdkopen_harmony.so", controller)
        self.assertNotIn("SdkOpenRuntime", controller)
        self.run_node("controller")

    def test_cold_popup_command_and_same_process_ability_rebuild(self) -> None:
        self.run_node("ability")

    def test_certificate_installer_uses_a_visible_dedicated_ability(self) -> None:
        self.run_node("certificate-ability")
        module = json.loads(read("sdkopen_tray/src/main/module.json5"))["module"]
        ability = next(
            item for item in module["abilities"]
            if item["name"] == "HttpsCertificateInstallAbility"
        )
        self.assertFalse(ability["exported"])
        self.assertNotIn("excludeFromMissions", ability)

    def test_single_tray_hap_manifest_has_no_appservice_acl(self) -> None:
        build_profile = json.loads(read("build-profile.json5"))
        app_scope = json.loads(read("AppScope/app.json5"))
        module = json.loads(read("sdkopen_tray/src/main/module.json5"))["module"]

        self.assertEqual(
            [item["name"] for item in build_profile["modules"]],
            ["sdkopen_tray"],
        )
        self.assertEqual(app_scope["app"]["bundleName"], "com.czur.opensdk")
        self.assertEqual(module["name"], "sdkopen_tray")
        self.assertEqual(module["type"], "entry")
        self.assertEqual(module["deviceTypes"], ["2in1"])
        self.assertEqual(
            {item["name"] for item in module["requestPermissions"]},
            {
                "ohos.permission.ACCESS_CERT_MANAGER",
                "ohos.permission.INTERNET",
                "ohos.permission.PREPARE_APP_TERMINATE",
            },
        )
        manifest_text = read("sdkopen_tray/src/main/module.json5")
        self.assertNotIn("AppService", manifest_text)
        self.assertNotIn("SUPPORT_APP_SERVICE_EXTENSION", manifest_text)
        self.assertNotIn("START_INVISIBLE_ABILITY", manifest_text)

    def test_https_certificate_install_is_user_initiated_and_root_only(self) -> None:
        controller = read("sdkopen_tray/src/main/ets/common/TrayServiceControl.ets")
        tray_page = read("sdkopen_tray/src/main/ets/pages/Index.ets")
        install_page = read("sdkopen_tray/src/main/ets/pages/HttpsCertificateInstall.ets")
        install_ability = read(
            "sdkopen_tray/src/main/ets/entryability/HttpsCertificateInstallAbility.ets"
        )
        self.assertIn("openInstallCertificateDialog", controller)
        self.assertIn("CertificateType.CA_CERT", controller)
        self.assertIn("CertificateScope.CURRENT_USER", controller)
        self.assertIn("sdk-open/runtime/tls/local-runtime-root-ca.crt", controller)
        self.assertNotIn("local-runtime-root-ca.key.pem", controller)
        self.assertNotIn("openHttpsDemo", controller)
        self.assertNotIn("https://sdk-runtime.localhost:18081/", controller)
        self.assertNotIn("打开 HTTPS Demo", tray_page)
        self.assertIn("installHttpsCertificate()", tray_page)
        self.assertIn("getContext(this) as common.UIAbilityContext", install_page)
        self.assertIn("installHttpsRootCertificate(context)", install_page)
        self.assertNotIn("libsdkopen_harmony.so", install_page)
        self.assertNotIn("libsdkopen_harmony.so", install_ability)

    def test_harmony_tls_publishes_certificate_hostname_for_assets(self) -> None:
        host = read("native/sdk_open_harmony_host.cpp")
        self.assertIn('config.asset_base_url = "http://127.0.0.1:"', host)
        self.assertNotIn('config.asset_base_url = "https://sdk-runtime.localhost:"', host)
        self.assertIn("config.tls.bind_host = config.bind_host", host)

    def test_native_runtime_stays_in_owner_hap_not_popup_controller(self) -> None:
        required = [
            "native/sdk_open_harmony_napi.cpp",
            "native/sdk_open_harmony_host.cpp",
            "sdkopen_tray/src/main/cpp/types/libsdkopen_harmony/Index.d.ts",
            "sdkopen_tray/src/main/ets/services/SdkOpenRuntime.ets",
            "sdkopen_tray/src/main/ets/services/UsbHostTransport.ets",
        ]
        for relative in required:
            self.assertTrue((HARMONY / relative).is_file(), relative)

        popup = read("sdkopen_tray/src/main/ets/statusbar/SdkOpenStatusBarAbility.ets")
        controller = read("sdkopen_tray/src/main/ets/common/TrayServiceControl.ets")
        for source in (popup, controller):
            self.assertNotIn("libsdkopen_harmony.so", source)
            self.assertNotIn("UsbHostTransport", source)
            self.assertNotIn("SdkOpenRuntime", source)

    def test_native_cleanup_fallback_remains_registered(self) -> None:
        napi = read("native/sdk_open_harmony_napi.cpp")
        usb_napi = (SDK_ROOT.parent / "module" / "devices" /
                    "usb_host_napi_ohos.cpp").read_text(encoding="utf-8")
        self.assertIn("napi_add_async_cleanup_hook", usb_napi)
        self.assertIn("RegisterUsbHostNapiOhos", napi)


if __name__ == "__main__":
    unittest.main()
