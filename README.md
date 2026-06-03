# CZUR Open SDK

[中文文档](./README_ZH.md)

## Official Resources

- Official site: <https://open.czur.com>
- Documentation center: <https://open.czur.com/docs/>

## Overview

The `sdk_open` project is the local runtime for CZUR Open SDK and the SDK-side integration entry for CZUR Open Platform. It turns device access, video preview, image capture, image processing and enhancement, OCR, barcode recognition, file conversion, and online/offline licensing into unified local HTTP + WebSocket APIs so business systems can bring paper, physical documents, and image assets into their own workflows.

The runtime keeps third-party integration boundaries stable through public DTOs, provider interfaces, a runnable mock-provider bundle, local admin/demo sites, and a reusable four-layer architecture. It can be built and run independently as the open SDK executable. Private capability-library types are not exposed.

## Executable

The open SDK executable is:

- `sdk_open_app`
  - links only the public `sdk_open` runtime and mock providers
  - serves as the standalone open-source executable

## Four-Layer Architecture

The `sdk_open` project is organized into these four layers:

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
- `image.enhance_capabilities`
- `image.enhance`
- `image.enhance_get`
- `image.enhance_cancel`
- `image.enhance_workflow_list`
- `image.enhance_workflow_get`
- `image.enhance_workflow_save`
- `image.enhance_workflow_delete`
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

### Image Enhancement Pipeline

`image.enhance_capabilities` returns the image enhancement capabilities exposed by the current provider, including runtime type, availability, defaults, parameter schema, and `localized.en/zh-CN` display text. Demo and third-party clients should use this method to render capability choices dynamically instead of hard-coding algorithm lists. The built-in offline capability list includes black edge optimization, normalization, rotation, blank page detection, and red/green head detection. The private provider also exposes online capabilities `doc_crop_enhance`, `remove_handwriting`, `doc_repair`, and `remove_moire`; they use the API key supplied to `auth.create_session` as `X-Api-Key`. The online enhancement gateway host is resolved as `CZUR_SDK_IMAGE_ENHANCE_BASE_URL`, then Admin runtime config, then the default `https://gateway-cn.czur.com`. Online authorization uses a separate host resolved as `CZUR_SDK_AUTHZ_BASE_URL`, then Admin runtime config, then the same default gateway.

`image.enhance` submits an async enhancement task for one or more image pages. `pipeline.steps` run in the user-provided order, and final output can be image pages or PDF/OFD/TIFF through the existing file conversion provider. `sane.scan` and `capture.take` accept the same optional `pipeline` for post-processing; SANE enhances scanned pages before export, and Capture enhances the existing final image assets. Blank page detection defaults to `action=drop`.

`image.enhance_workflow_*` methods store reusable user-defined enhancement pipelines in the SDK work directory. The Demo uses these methods to save workflows from the Image Enhancement page and reuse the selected workflow from Capture or SANE scan flows.

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
cmake --build build --target sdk_open_app -j4
```

### 3. Run the open executable

```bash
./build/Debug/sdk_open_app
```

## Environment Variables

`sdk_open_app` supports these overrides:

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
