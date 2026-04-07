# CZUR Open SDK 中文说明

[English document](./README.md)。

## 项目简介

`sdk_open` 是 CZUR 本地 SDK 的开放层 skeleton。它提供统一的本地 HTTP + WebSocket 入口、JSON 命令协议骨架、可插拔 provider 接口，以及一个默认可运行的 mock provider 组合，便于外部开发者先接入、调试和扩展。

这个目录关注的是开放接口层本身，不包含私有能力库的实现细节。当前代码既可以作为一个最小可运行 demo，也可以作为后续接入真实设备、图像处理、OCR、OFD、授权模块的基础框架。

## 当前能力

### Available now

- 本地双 HTTP 入口：
  - `admin-site`: `http://127.0.0.1:17080`
  - `demo-site`: `http://127.0.0.1:17081`
- 本地双 WebSocket 入口：
  - 命令通道：`ws://127.0.0.1:17090`
  - 视频通道：`ws://127.0.0.1:17091`
- 已落地的命令 method：
  - `system.ping`
  - `system.info`
  - `system.capabilities`
  - `auth.validate`
  - `auth.refresh`
  - `auth.get_context`
- 默认 mock provider：
  - auth
  - device
  - graphic
  - ocr
  - ofd
- 统一 JSON 序列化/反序列化和统一状态码枚举

### Designed for extension

- 预留了以下开放能力域的扩展入口：
  - `device.*`
  - `capture.*`
  - `image.*`
  - `ocr.*`
  - `file.*`
- 这些域当前在 `sdk_open` 中主要体现为协议骨架、provider interface 和 capability 描述，真实业务接入仍可继续演进。

## 架构与可扩展性

`sdk_open` 当前的结构分为 4 层：

- `runtime/`
  - 应用装配、HTTP/WS server、命令分发、JSON 协议处理
- `interfaces/`
  - provider interface、授权类型、状态码枚举、provider bundle
- `providers/mock/`
  - 默认 mock provider，实现最小可运行闭环
- `third_party/`
  - vendored `cpp-httplib`、`websocketpp`、`asio`、`nlohmann/json`

扩展方式：

- 如果只需要跑通 demo 或联调协议，可以直接使用 mock provider。
- 如果需要接入真实能力，可以实现自定义 provider，并通过 `ProviderBundle` 装配到 `SdkApp`。
- 命令入口采用统一的 JSON 请求格式，新的 method 可以继续挂到 dispatcher 中，并保持相同的响应结构。

当前预留的主要 provider interface：

- `ISdkAuthProvider`
- `ISdkDeviceProvider`
- `ISdkGraphicProvider`
- `ISdkOcrProvider`
- `ISdkOfdProvider`

## 开发环境

- 平台：Linux
- 语言标准：C++17
- 构建系统：CMake
- 内置依赖：
  - `cpp-httplib`
  - `websocketpp`
  - `asio`
  - `nlohmann/json`
- 可选前端：
  - `frontend/admin-site`
  - `frontend/demo-site`
  - 如果只需要启动后端 skeleton，建议先关闭 `BUILD_SDK_WEB`

## 快速启动

`sdk_open` 当前通过主仓根目录 CMake 一起构建。最小启动路径建议关闭前端构建。

### 1. 配置

```bash
cmake -S . -B build -DBUILD_SDK_OPEN=ON -DBUILD_SDK_WEB=OFF -DCMAKE_BUILD_TYPE=Debug
```

### 2. 构建

```bash
cmake --build build --target sdk_open_runtime sdk_mock_providers sdk_open_app -j4
```

### 3. 运行

```bash
./build/Debug/sdk_open_app
```

启动后默认监听：

- `http://127.0.0.1:17080`
- `http://127.0.0.1:17081`
- `ws://127.0.0.1:17090`
- `ws://127.0.0.1:17091`

## 最小请求示例

命令通道最小 `ping`：

```json
{
  "request_id": "1",
  "method": "system.ping",
  "params": {},
  "auth": {},
  "client": {}
}
```

查询能力列表：

```json
{
  "request_id": "2",
  "method": "system.capabilities",
  "params": {},
  "auth": {},
  "client": {}
}
```

HTTP 状态接口：

```bash
curl http://127.0.0.1:17080/api/status
```

如果设置了 `SDK_AUTH_TOKEN`，则需要带上：

```bash
curl -H "Authorization: Bearer <token>" http://127.0.0.1:17080/api/status
```

## 配置与环境变量

编译期开关：

- `SDK_OPEN_ENABLE_HTTP_SERVER=ON`
  - 默认值。
  - 启动内置 `admin-site` 和 `demo-site` HTTP 服务，占用 `17080/17081`。
- `SDK_OPEN_ENABLE_HTTP_SERVER=OFF`
  - 禁用内置 HTTP 服务。
  - 适合直接在 `frontend/demo-site` 或 `frontend/admin-site` 下执行 `pnpm run dev`，避免和 `sdk_open_app` 的端口冲突。

示例：

```bash
cmake -S . -B build-sdk-open \
  -DBUILD_SDK_OPEN=ON \
  -DBUILD_SDK_WEB=OFF \
  -DSDK_OPEN_ENABLE_HTTP_SERVER=OFF \
  -DCMAKE_BUILD_TYPE=Debug
```

当前 `sdk_open_app` 支持以下环境变量覆盖默认配置：

- `SDK_ADMIN_HTTP_PORT`
- `SDK_DEMO_HTTP_PORT`
- `SDK_COMMAND_WS_PORT`
- `SDK_VIDEO_WS_PORT`
- `SDK_AUTH_TOKEN`

## 当前状态说明

`sdk_open` 当前更偏向开放层基础框架，而不是完整业务 SDK。已经可用的部分主要是：

- 本地服务入口
- 统一命令协议骨架
- `system.*` 和 `auth.*`
- mock provider

设备控制、拍摄、图像处理、OCR、文件处理等方向已经为后续接入保留了清晰的扩展点，但具体业务能力仍可继续向真实 provider 演进。

## 文档

- 英文版本：[README.md](./README.md)
- 英文错误码文档：[doc/ERROR_CODES.md](./doc/ERROR_CODES.md)
- 中文错误码文档：[doc/ERROR_CODES_ZH.md](./doc/ERROR_CODES_ZH.md)
- 贡献说明：[CONTRIBUTING.md](./CONTRIBUTING.md)
