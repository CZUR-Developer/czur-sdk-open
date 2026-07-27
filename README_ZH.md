# CZUR Open SDK 中文说明

[English document](./README.md)

## 项目简介

`sdk_open` 项目是 CZUR Open SDK 的本地运行时，也是 CZUR 开放平台在 SDK 侧的接入入口。它把设备接入、视频预览、拍照采集、图像处理与增强、OCR、条码识别、文件转换以及在线/离线授权统一成本地 HTTP + WebSocket API，帮助业务系统把纸质资料、实体文档和图像资产接入自己的业务流程。

该运行时通过公共 DTO、provider interface、可运行的 mock provider 组合、本地 admin/demo 站点，以及一套可复用的四层架构来稳定第三方集成边界。它可以作为开放 SDK 可执行程序独立构建和运行；私有能力库类型不会对外暴露。

## 官方资源

- 官方站点：<https://open.czur.com>
- 文档中心：<https://open.czur.com/docs/>

## 平台支持

SDK Open runtime 目标支持多平台。现阶段已实现 Linux 版本，后续会陆续支持 Windows 和 macOS。

## Release 包

SDK Open Release 包包含 CZUR 提供的默认 Provider 能力。开发者可以通过 CZUR 开放平台申请 API Key，并在本地 runtime 中使用该 Key 体验和使用已授权的 SDK 能力。

## 运行时入口

开放 SDK 的可执行入口为：

- `sdk_open_app`
  - 只装配 `sdk_open` 公共层和 mock providers
  - 作为开源项目的独立可执行入口

## 四层架构

`sdk_open` 项目按以下四层组织：

- `transport/`
  - HTTP 站点托管
  - command WebSocket
  - video WebSocket
  - 连接与会话的传输承载
- `application/`
  - 请求校验
  - token / session 鉴权
  - 任务编排
  - 统一错误码映射
- `facade/`
  - `DeviceFacade`
  - `GraphicFacade`
  - `OcrFacade`
  - `OfdFacade`
- `interfaces/` + `providers/*`
  - 公共 DTO 与 provider interface
  - mock/private provider 适配实现

完整目标边界说明见：[doc/RUNTIME_ARCHITECTURE_ZH.md](./doc/RUNTIME_ARCHITECTURE_ZH.md)

## 默认端点

默认启动后监听：

- `http://127.0.0.1:17080`
  - admin site
- `http://127.0.0.1:17081`
  - demo site
- `http://127.0.0.1:17082`
  - 采集图片、缩略图、文档输出等资源 API
- `ws://127.0.0.1:17090`
  - command channel
- `ws://127.0.0.1:17091`
  - video channel

启用 TLS 后，以上明文端点保持可用，并额外监听：

- `https://127.0.0.1:18082`
  - asset API 的 HTTPS 端点
- `wss://127.0.0.1:18090`
  - command channel 的 WSS 端点
- `wss://127.0.0.1:18091`
  - video channel 的 WSS 端点

admin site 和 demo site 仍使用其现有 HTTP 端口；TLS 监听地址可独立配置，因此不会因为对外开放 WSS/HTTPS 而暴露这两个站点。

## 协议模型

### Command WS

- command WebSocket 采用匿名建连
- WebSocket 握手 query 中不传业务 token
- 请求主键只使用 `request_id`
- 业务请求默认不再传 `auth` 对象
- 会话绑定在 command 连接上下文上

最小匿名请求：

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

创建会话：

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

响应结构：

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

- `device.close`、`video.start`、`video.stop`、`video.set_format` 走 command WS
- video WS 只用于视频流输出与相关事件
- video WS 使用 `session_token + stream_id` 建连

示例：

```text
ws://127.0.0.1:17091?session_token=ss-v2-xxxx&stream_id=stream-001
```

启用 TLS 时使用：

```text
wss://127.0.0.1:18091?session_token=ss-v2-xxxx&stream_id=stream-001
```

## 管理接口

管理接口与业务协议分离：

- `GET /healthz`
  - 匿名访问
- `GET /api/status`
  - 需要 `Authorization: Bearer <auth_token>`

示例：

```bash
curl http://127.0.0.1:17080/healthz
curl -H "Authorization: Bearer <token>" http://127.0.0.1:17080/api/status
```

## 构建与运行

### 1. 配置

```bash
cmake -S . -B build -DBUILD_SDK_OPEN=ON -DBUILD_SDK_WEB=OFF -DCMAKE_BUILD_TYPE=Debug
```

### 2. 构建

```bash
cmake --build build --target sdk_open_app -j4
```

### 3. 运行开源入口

```bash
./build/Debug/sdk_open_app
```

### 4. Windows Service

Windows 版本的 `sdk_open_app.exe` 同时支持控制台模式和 Windows Service 模式。安装或卸载服务需要在管理员终端执行：

```powershell
.\sdk_open_app.exe --install-service
.\sdk_open_app.exe --uninstall-service
```

可选参数：

- `--service-name <name>`：指定服务名，默认 `CZURSdkOpenApp`
- `--display-name <name>`：指定服务显示名，默认 `CZUR SDK Open App`
- `--config <path>`：指定运行时配置文件路径

安装后可通过系统服务管理器或 `sc.exe` 启停：

```powershell
sc.exe start CZURSdkOpenApp
sc.exe stop CZURSdkOpenApp
```

服务模式下实际启动命令为 `sdk_open_app.exe --service ...`，该参数由 SCM 托管时使用；普通调试仍可直接运行 `sdk_open_app.exe [config]`。

## 环境变量

`sdk_open_app` 支持以下端口和鉴权覆盖：

- `SDK_ADMIN_HTTP_PORT`
- `SDK_DEMO_HTTP_PORT`
- `SDK_ASSET_HTTP_PORT`
- `SDK_ASSET_BASE_URL`
- `SDK_COMMAND_WS_PORT`
- `SDK_VIDEO_WS_PORT`
- `SDK_AUTH_TOKEN`
- `SDK_TLS_ENABLED`：设置为 `1` 时启用 HTTPS/WSS 双端口监听
- `SDK_TLS_BIND_HOST`：TLS listener 的绑定地址，默认沿用 `bind_host`
- `SDK_TLS_CERT_FILE`：包含服务端证书与中间证书的 PEM/fullchain 文件
- `SDK_TLS_KEY_FILE`：服务端 PEM 私钥文件
- `SDK_TLS_KEY_PASSWORD`：可选的 PEM 私钥口令
- `SDK_ASSET_HTTPS_PORT`：默认 `18082`
- `SDK_COMMAND_WSS_PORT`：默认 `18090`
- `SDK_VIDEO_WSS_PORT`：默认 `18091`

TLS 配置示例：

```bash
export SDK_TLS_ENABLED=1
export SDK_TLS_BIND_HOST=0.0.0.0
export SDK_TLS_CERT_FILE=/etc/czur-sdk/tls/fullchain.pem
export SDK_TLS_KEY_FILE=/etc/czur-sdk/tls/privkey.pem
export SDK_ASSET_BASE_URL=https://sdk.example.com:18082
```

### 本机 runtime 默认 TLS

Windows 和 Linux 的官方 SDK Open 安装包会携带一组仅供本机 runtime 使用的默认
证书与私钥，并在安装时完成以下配置：

- 启用 `HTTPS 18082`、`WSS 18090` 与 `WSS 18091`，继续保留原有明文端口；
- 将 `sdk-runtime.localhost` 映射到 `127.0.0.1`，服务只监听回环地址；
- 将 Local Runtime Root CA 加入操作系统信任库，因此系统信任库客户端访问
  `https://sdk-runtime.localhost:18082` 时不会出现默认自签名证书错误；
- 默认 asset URL 为 `https://sdk-runtime.localhost:18082`。

默认文件位置如下，升级安装不会覆盖已由客户替换的服务端证书和私钥：

- Windows：`C:\ProgramData\CZUR\sdk-open\tls\sdk-runtime.localhost.fullchain.pem` 和
  `C:\ProgramData\CZUR\sdk-open\tls\sdk-runtime.localhost.key.pem`；配置文件为
  `C:\ProgramData\CZUR\sdk-open\runtime.env`。
- Linux：`/etc/czur/sdk-open/tls/sdk-runtime.localhost.fullchain.pem` 和
  `/etc/czur/sdk-open/tls/sdk-runtime.localhost.key.pem`；配置文件为
  `/etc/czur/sdk-open/runtime.env`。

如需替换为客户自己的证书，请将证书链和私钥作为匹配的一对替换，并重启 `sdk-open`
服务；如更改文件路径、域名或端口，再同步修改 `runtime.env`。私有 CA 的根证书必须
由客户自行加入调用方的信任库。默认凭据和 `sdk-runtime.localhost` 回环映射仅适合本机
runtime，不能用于互联网或跨客户部署。`SDK_TLS_BIND_HOST=0.0.0.0` 或 NAT/DNS
部署时必须设置实际可访问的 `SDK_ASSET_BASE_URL`。TLS 只加密传输，不替代 API Key、
session token、资产授权或防火墙策略。

多数系统信任库客户端可直接使用安装器写入的根 CA；若 Linux 上的浏览器或 SDK 客户端
维护独立的 NSS/Firefox 证书库，则还需要由该客户端按自身方式导入
`local-runtime-root-ca.crt`。

Asset API 需要使用会话授权访问：

```bash
curl -H "Authorization: Bearer <session_token>" \
  http://127.0.0.1:17082/api/assets/<task_id>/<asset_id>
```

TLS 启用且未显式设置 `SDK_ASSET_BASE_URL` 时，SDK 返回的 asset URL 默认使用 `https://<host>:18082`；明文 asset API 仍兼容保留。

## 文档

- 目标架构说明：[doc/RUNTIME_ARCHITECTURE_ZH.md](./doc/RUNTIME_ARCHITECTURE_ZH.md)
- 指令通道说明：[doc/COMMAND_CHANNEL_FLOW_ZH.md](./doc/COMMAND_CHANNEL_FLOW_ZH.md)
- 错误码说明：[doc/ERROR_CODES_ZH.md](./doc/ERROR_CODES_ZH.md)
- English README：[README.md](./README.md)
