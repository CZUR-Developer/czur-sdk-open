# CZUR Open SDK

[中文文档](./README_ZH.md)

## Overview

The `sdk_open` project is the local runtime for CZUR Open SDK and the SDK-side integration entry for CZUR Open Platform. It turns device access, video preview, image capture, image processing and enhancement, OCR, barcode recognition, file conversion, and online/offline licensing into unified local HTTP + WebSocket APIs so business systems can bring paper, physical documents, and image assets into their own workflows.

The runtime keeps third-party integration boundaries stable through public DTOs, provider interfaces, a runnable mock-provider bundle, local admin/demo sites, and a reusable four-layer architecture. It can be built and run independently as the open SDK executable. Private capability-library types are not exposed.

## Official Resources

- Official site: <https://open.czur.com>
- Documentation center: <https://open.czur.com/docs/>

## Platform Support

The SDK Open runtime targets multi-platform support. The Linux version is implemented at this stage, and Windows and macOS support will be added gradually.

## Release Package

The SDK Open release package includes CZUR-provided default Provider capabilities. Developers can apply for an API Key on the CZUR Open Platform, then use it with the local runtime to experience and use the authorized SDK capabilities.

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

When TLS is enabled, the plaintext endpoints remain available and these additional listeners are exposed:

- `https://127.0.0.1:18082`
  - HTTPS asset API
- `wss://127.0.0.1:18090`
  - WSS command channel
- `wss://127.0.0.1:18091`
  - WSS video channel

The admin and demo sites keep their existing HTTP ports. TLS listeners use a separate bind address, so exposing WSS/HTTPS does not expose those sites.


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

With TLS enabled, use:

```text
wss://127.0.0.1:18091?session_token=ss-v2-xxxx&stream_id=stream-001
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

### 4. Windows Service

On Windows, `sdk_open_app.exe` can run either as a console process or as a Windows Service. Install and uninstall commands must be run from an elevated terminal:

```powershell
.\sdk_open_app.exe --install-service
.\sdk_open_app.exe --uninstall-service
```

Optional arguments:

- `--service-name <name>`: service name, defaults to `CZURSdkOpenApp`
- `--display-name <name>`: service display name, defaults to `CZUR SDK Open App`
- `--config <path>`: runtime config file path

After installation, start or stop the service through Services or `sc.exe`:

```powershell
sc.exe start CZURSdkOpenApp
sc.exe stop CZURSdkOpenApp
```

Service mode is launched by SCM with `sdk_open_app.exe --service ...`; normal debugging can still run `sdk_open_app.exe [config]` directly.

## Environment Variables

`sdk_open_app` supports these overrides:

- `SDK_ADMIN_HTTP_PORT`
- `SDK_DEMO_HTTP_PORT`
- `SDK_ASSET_HTTP_PORT`
- `SDK_ASSET_BASE_URL`
- `SDK_COMMAND_WS_PORT`
- `SDK_VIDEO_WS_PORT`
- `SDK_AUTH_TOKEN`
- `SDK_TLS_ENABLED`: set to `1` to enable the HTTPS/WSS dual-port listeners
- `SDK_TLS_BIND_HOST`: bind address for TLS listeners; defaults to `bind_host`
- `SDK_TLS_CERT_FILE`: PEM/fullchain file containing the server certificate and intermediates
- `SDK_TLS_KEY_FILE`: server PEM private key file
- `SDK_TLS_KEY_PASSWORD`: optional private-key password
- `SDK_ASSET_HTTPS_PORT`: defaults to `18082`
- `SDK_COMMAND_WSS_PORT`: defaults to `18090`
- `SDK_VIDEO_WSS_PORT`: defaults to `18091`

Example TLS deployment:

```bash
export SDK_TLS_ENABLED=1
export SDK_TLS_BIND_HOST=0.0.0.0
export SDK_TLS_CERT_FILE=/etc/czur-sdk/tls/fullchain.pem
export SDK_TLS_KEY_FILE=/etc/czur-sdk/tls/privkey.pem
export SDK_ASSET_BASE_URL=https://sdk.example.com:18082
```

### Default TLS for the local runtime

The official Windows and Linux SDK Open packages include a default certificate and private
key intended only for the local runtime. During installation they:

- enable `HTTPS 18082`, `WSS 18090`, and `WSS 18091` while retaining the plaintext ports;
- map `sdk-runtime.localhost` to `127.0.0.1`, with the TLS listeners bound to loopback only;
- add the Local Runtime Root CA to the operating-system trust store, so clients using that
  store can access `https://sdk-runtime.localhost:18082` without a default self-signed warning;
- use `https://sdk-runtime.localhost:18082` as the default asset URL.

The service certificate and private key can be replaced without being overwritten by an
upgrade:

- Windows: `C:\ProgramData\CZUR\sdk-open\tls\sdk-runtime.localhost.fullchain.pem` and
  `C:\ProgramData\CZUR\sdk-open\tls\sdk-runtime.localhost.key.pem`; configuration is in
  `C:\ProgramData\CZUR\sdk-open\runtime.env`.
- Linux: `/etc/czur/sdk-open/tls/sdk-runtime.localhost.fullchain.pem` and
  `/etc/czur/sdk-open/tls/sdk-runtime.localhost.key.pem`; configuration is in
  `/etc/czur/sdk-open/runtime.env`.

Replace the certificate chain and private key as a matching pair, then restart `sdk-open`.
If paths, host name, or ports change, update `runtime.env` too. A replacement private CA
must be trusted by the calling client. The default credentials and loopback mapping are for
the local runtime only, never for internet-facing or cross-customer use. When using
`SDK_TLS_BIND_HOST=0.0.0.0` or NAT/DNS, set `SDK_ASSET_BASE_URL` to the actual public HTTPS
address. TLS encrypts transport only; API keys, session tokens, asset authorization, and
firewall controls remain required.

Most clients using the operating-system trust store work immediately with the installed root
CA. A Linux browser or SDK client that maintains an independent NSS/Firefox certificate store
must import `local-runtime-root-ca.crt` into that store as well.

Asset API responses require session authorization:

```bash
curl -H "Authorization: Bearer <session_token>" \
  http://127.0.0.1:17082/api/assets/<task_id>/<asset_id>
```

When TLS is enabled and `SDK_ASSET_BASE_URL` is not set, returned asset URLs default to `https://<host>:18082`; the plaintext asset API remains available for existing clients.

## Documentation

- Target runtime architecture: [doc/RUNTIME_ARCHITECTURE_ZH.md](./doc/RUNTIME_ARCHITECTURE_ZH.md)
- Command channel flow: [doc/COMMAND_CHANNEL_FLOW.md](./doc/COMMAND_CHANNEL_FLOW.md)
- Error codes: [doc/ERROR_CODES.md](./doc/ERROR_CODES.md)
- 中文说明: [README_ZH.md](./README_ZH.md)
