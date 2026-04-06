# CZUR Open SDK

[中文文档](./README_ZH.md).

## Overview

`sdk_open` is the open-layer skeleton of the CZUR local SDK. It provides a unified local HTTP + WebSocket entrypoint set, a JSON command protocol skeleton, pluggable provider interfaces, and a runnable mock provider bundle for integration, debugging, and future extension.

This directory focuses on the open integration layer itself. It does not expose private capability-library implementation details. In its current shape, it can be used both as a minimal runnable demo and as the base framework for future real-device, image-processing, OCR, OFD, and auth integrations.

## Capabilities

### Available now

- Two local HTTP entrypoints:
  - `admin-site`: `http://127.0.0.1:17080`
  - `demo-site`: `http://127.0.0.1:17081`
- Two local WebSocket entrypoints:
  - command channel: `ws://127.0.0.1:17090`
  - video channel: `ws://127.0.0.1:17091`
- Implemented command methods:
  - `system.ping`
  - `system.info`
  - `system.capabilities`
  - `auth.validate`
  - `auth.refresh`
  - `auth.get_context`
- Default mock providers:
  - auth
  - device
  - graphic
  - ocr
  - ofd
- Unified JSON serialization/deserialization and status-code enums

### Designed for extension

- The following capability domains are reserved for future growth:
  - `device.*`
  - `capture.*`
  - `image.*`
  - `ocr.*`
  - `file.*`
- In the current repository, these domains mainly exist as protocol scaffolding, provider interfaces, and capability metadata. Real business integration can continue to evolve on top of them.

## Architecture and Extensibility

`sdk_open` is currently organized into 4 layers:

- `runtime/`
  - app composition, HTTP/WS servers, command dispatching, JSON protocol handling
- `interfaces/`
  - provider interfaces, auth types, status-code enums, provider bundle
- `providers/mock/`
  - default mock providers for a minimal runnable setup
- `third_party/`
  - vendored `cpp-httplib`, `websocketpp`, `asio`, `nlohmann/json`

How to extend it:

- If you only need a runnable demo or protocol-level integration, use the mock providers.
- If you need real capabilities, implement custom providers and wire them through `ProviderBundle`.
- The command entrypoint uses a unified JSON request model, so new methods can continue to be registered in the dispatcher while keeping the same response shape.

Main provider interfaces already reserved:

- `ISdkAuthProvider`
- `ISdkDeviceProvider`
- `ISdkGraphicProvider`
- `ISdkOcrProvider`
- `ISdkOfdProvider`

## Environment

- Platform: Linux
- Language standard: C++17
- Build system: CMake
- Bundled dependencies:
  - `cpp-httplib`
  - `websocketpp`
  - `asio`
  - `nlohmann/json`
- Optional frontend:
  - `frontend/admin-site`
  - `frontend/demo-site`
  - If you only want to run the backend skeleton, start with `BUILD_SDK_WEB=OFF`

## Quick Start

`sdk_open` is currently built through the repository root CMake flow. The minimal startup path is to disable web frontend builds.

### 1. Configure

```bash
cmake -S . -B build -DBUILD_SDK_OPEN=ON -DBUILD_SDK_WEB=OFF -DCMAKE_BUILD_TYPE=Debug
```

### 2. Build

```bash
cmake --build build --target sdk_open_runtime sdk_mock_providers sdk_open_app -j4
```

### 3. Run

```bash
./build/Debug/sdk_open_app
```

Default endpoints after startup:

- `http://127.0.0.1:17080`
- `http://127.0.0.1:17081`
- `ws://127.0.0.1:17090`
- `ws://127.0.0.1:17091`

## Minimal Request Examples

Minimal command-channel `ping`:

```json
{
  "request_id": "1",
  "method": "system.ping",
  "params": {},
  "auth": {},
  "client": {}
}
```

Capability query:

```json
{
  "request_id": "2",
  "method": "system.capabilities",
  "params": {},
  "auth": {},
  "client": {}
}
```

HTTP status endpoint:

```bash
curl http://127.0.0.1:17080/api/status
```

If `SDK_AUTH_TOKEN` is set, use:

```bash
curl -H "Authorization: Bearer <token>" http://127.0.0.1:17080/api/status
```

## Configuration and Environment Variables

The current `sdk_open_app` supports the following environment-variable overrides:

- `SDK_ADMIN_HTTP_PORT`
- `SDK_DEMO_HTTP_PORT`
- `SDK_COMMAND_WS_PORT`
- `SDK_VIDEO_WS_PORT`
- `SDK_AUTH_TOKEN`

## Current Project Status

`sdk_open` is currently closer to an open-layer foundation than a full production SDK. The parts that are already usable are mainly:

- local service entrypoints
- the unified command protocol skeleton
- `system.*` and `auth.*`
- mock providers

Device control, capture, image processing, OCR, and file processing directions already have clear extension points reserved, but concrete business capability integration can continue to evolve through real providers.

## Documentation

- Chinese version: [README_ZH.md](./README_ZH.md)
- Public error codes: [doc/ERROR_CODES.md](./doc/ERROR_CODES.md)
- Chinese error codes: [doc/ERROR_CODES_ZH.md](./doc/ERROR_CODES_ZH.md)
- Contribution guide: [CONTRIBUTING.md](./CONTRIBUTING.md)
