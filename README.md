# CZUR Open SDK

[中文文档](./README_ZH.md)

## Overview

`src/sdk_open` is the open-source runtime for the CZUR local SDK. It provides unified local HTTP + WebSocket endpoints, stable public provider interfaces, a runnable mock-provider bundle, and a four-layer runtime structure that can also be reused by the internal product entrypoint.

This directory is designed to be independently buildable and runnable. It does not expose private capability-library types.

## Executables

Two executables are intentionally retained:

- `sdk_open_app`
  - implemented by `src/sdk_open/runtime/sdk_open_main.cpp`
  - links only the public `sdk_open` runtime and mock providers
  - serves as the standalone open-source executable
- `sdk_app`
  - implemented by `src/app/process/sdk/sdk_main.cpp`
  - reuses the same `sdk_open` core runtime
  - wires mock or private providers through main-repo build switches

## Four-Layer Architecture

`src/sdk_open` is organized into these four layers:

- `transport/`
  - HTTP site hosting
  - command WebSocket
  - video WebSocket
  - connection and transport-session handling
- `application/`
  - request validation
  - token / session auth
  - orchestration
  - unified error mapping
- `facade/`
  - `DeviceFacade`
  - `GraphicFacade`
  - `OcrFacade`
  - `OfdFacade`
- `interfaces/` + `providers/*`
  - public DTOs and provider interfaces
  - mock/private provider adapter implementations

See the target architecture guide for the final boundary definition:
[doc/RUNTIME_ARCHITECTURE_ZH.md](./doc/RUNTIME_ARCHITECTURE_ZH.md)

## Default Endpoints

Default runtime endpoints:

- `http://127.0.0.1:17080`
  - admin site
- `http://127.0.0.1:17081`
  - demo site
- `http://127.0.0.1:17082`
  - asset API for captured images, thumbnails, and document outputs
- `ws://127.0.0.1:17090`
  - command channel
- `ws://127.0.0.1:17091`
  - video channel

## Public Methods

The current runtime exposes:

- `system.ping`
- `system.info`
- `system.capabilities`
- `auth.create_session`
- `auth.get_context`
- `auth.refresh_session`
- `auth.destroy_session`
- `device.list`
- `device.get`
- `device.open`
- `device.close`
- `capture.take`
- `capture.get`
- `video.start`
- `video.stop`
- `video.set_format`
- `video.set_profile`
- `image.process`
- `image.process_page`
- `image.apply_color_mode`
- `ocr.recognize`
- `ocr.get`
- `ocr.cancel`
- `ocr.extract_text`
- `recognition.barcode_detect`
- `file.convert`
- `sane.status`
- `sane.list`
- `sane.watch_start`
- `sane.watch_stop`
- `sane.open`
- `sane.close`
- `sane.get_options`
- `sane.set_options`
- `sane.profile_list`
- `sane.profile_save`
- `sane.profile_apply`
- `sane.profile_delete`
- `sane.scan`
- `sane.scan_get`
- `sane.scan_cancel`

### SANE Linux-only Notes

`sane.*` is a Linux-only scanner capability domain for third-party SANE scanner discovery, hotplug watching, sessions, option reads/writes, option profiles, and scan tasks. The SDK does not reuse the legacy publicd/subprocess management path; the private provider manages the SANE runtime and sessions in-process. Non-Linux runtimes keep the methods visible, but `sane.status` reports `available=false` and other `sane.*` methods return a SANE unavailable error.

`sane.list` returns only devices recognized by a SANE backend and openable by default. USB/finder detections are used to trigger hotplug refreshes and diagnostics only. Pass `include_detected=true` to also receive `detected_devices/detected_count`; the Demo keeps these diagnostic rows out of the scan device list so one scanner is not displayed twice. SANE support is Linux-only. When `sane.scan` is called without an output directory, files are written under the SDK task asset directory, whose root can be overridden with `SDK_OPEN_WORK_DIR`; the legacy client work directory is not used. The SDK does not expose a synthetic preview/page mode for SANE. Page behavior follows device options: Flatbed-like `source` values scan one page, while ADF/Feeder/Duplex-like `source` values keep pulling pages until the device reports no documents. `sane.scan` submits an async task and returns `accepted/task_id/task`; use `sane.scan_get`, `sane.scan_cancel`, and the `sane.scan_changed` event for task status, page progress, conversion, completion, failure, or cancellation.

## Protocol Model

### Command WS

- command WebSocket connects anonymously
- no business token is passed in the WebSocket handshake query
- requests use only `request_id`
- business requests no longer carry an `auth` object
- session state is bound to the command connection context

Minimal anonymous request:

```json
{
  "request_id": "req-ping-001",
  "method": "system.ping",
  "params": {},
  "client": {
    "source": "demo-site",
    "protocol_version": "2.0.0",
    "trace_id": "trc-001"
  }
}
```

Create a bound session:

```json
{
  "request_id": "req-auth-001",
  "method": "auth.create_session",
  "params": {
    "token": "demo-token-42F8"
  },
  "client": {
    "source": "demo-site",
    "protocol_version": "2.0.0",
    "trace_id": "trc-002"
  }
}
```

Response shape:

```json
{
  "request_id": "req-auth-001",
  "code": 0,
  "message": "ok",
  "data": {
    "session_token": "ss-v2-xxxx",
    "expires_in": 7200
  },
  "ts": 1710000000
}
```

### Video WS

- `device.close`, `video.start`, `video.stop`, and `video.set_format` go through command WS
- video WS is reserved for frame output and related events
- video WS connects with `session_token + stream_id`

Example:

```text
ws://127.0.0.1:17091?session_token=ss-v2-xxxx&stream_id=stream-001
```

## Admin APIs

Admin APIs are separate from SDK business auth:

- `GET /healthz`
  - anonymous
- `GET /api/status`
  - requires `Authorization: Bearer <auth_token>`

Example:

```bash
curl http://127.0.0.1:17080/healthz
curl -H "Authorization: Bearer <token>" http://127.0.0.1:17080/api/status
```

## Build and Run

### 1. Configure

```bash
cmake -S . -B build -DBUILD_SDK_OPEN=ON -DBUILD_SDK_WEB=OFF -DCMAKE_BUILD_TYPE=Debug
```

### 2. Build

```bash
cmake --build build --target sdk_open_app sdk_app -j4
```

### 3. Run the open executable

```bash
./build/Debug/sdk_open_app
```

## Environment Variables

`sdk_open_app` and `sdk_app` support these overrides:

- `SDK_ADMIN_HTTP_PORT`
- `SDK_DEMO_HTTP_PORT`
- `SDK_ASSET_HTTP_PORT`
- `SDK_ASSET_BASE_URL`
- `SDK_COMMAND_WS_PORT`
- `SDK_VIDEO_WS_PORT`
- `SDK_AUTH_TOKEN`

Asset API responses require session authorization:

```bash
curl -H "Authorization: Bearer <session_token>" \
  http://127.0.0.1:17082/api/assets/<task_id>/<asset_id>
```

## Documentation

- Target runtime architecture: [doc/RUNTIME_ARCHITECTURE_ZH.md](./doc/RUNTIME_ARCHITECTURE_ZH.md)
- Command channel flow: [doc/COMMAND_CHANNEL_FLOW.md](./doc/COMMAND_CHANNEL_FLOW.md)
- Error codes: [doc/ERROR_CODES.md](./doc/ERROR_CODES.md)
- 中文说明: [README_ZH.md](./README_ZH.md)
