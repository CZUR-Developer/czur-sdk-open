# CZUR Open SDK 中文说明

[English document](./README.md)

## 官方资源

- 官方站点：<https://open.czur.com>
- 文档中心：<https://open.czur.com/docs/>

## 项目简介

`sdk_open` 项目是 CZUR Open SDK 的本地运行时，也是 CZUR 开放平台在 SDK 侧的接入入口。它把设备接入、视频预览、拍照采集、图像处理与增强、OCR、条码识别、文件转换以及在线/离线授权统一成本地 HTTP + WebSocket API，帮助业务系统把纸质资料、实体文档和图像资产接入自己的业务流程。

该运行时通过公共 DTO、provider interface、可运行的 mock provider 组合、本地 admin/demo 站点，以及一套可复用的四层架构来稳定第三方集成边界。它可以作为开放 SDK 可执行程序独立构建和运行；私有能力库类型不会对外暴露。

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

## 当前公开方法

当前运行时公开的方法为：

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
<<<<<<< HEAD
- `image.enhance_capabilities`
- `image.enhance`
- `image.enhance_get`
- `image.enhance_cancel`
- `image.enhance_workflow_list`
- `image.enhance_workflow_get`
- `image.enhance_workflow_save`
- `image.enhance_workflow_delete`
=======
>>>>>>> Feature(sane): add async scan workflow
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

<<<<<<< HEAD
### 图像增强 Pipeline

`image.enhance_capabilities` 返回当前 provider 暴露的图像增强能力、运行方式、可用状态、默认参数、参数 schema 和 `localized.en/zh-CN` 展示文案。Demo 和第三方应用应优先通过这个接口动态展示能力，而不是硬编码算法列表。当前内置离线能力包括黑边优化、统一规格、旋转、空白页检测和红绿头；私有 provider 还会暴露在线能力 `doc_crop_enhance`、`remove_handwriting`、`doc_repair`、`remove_moire`，调用时使用 `auth.create_session` 传入的 API Key 作为 `X-Api-Key`。在线增强网关地址按 `CZUR_SDK_IMAGE_ENHANCE_BASE_URL`、Admin 运行时配置、默认 `https://gateway-cn.czur.com` 的顺序解析。在线授权网关使用独立地址，按 `CZUR_SDK_AUTHZ_BASE_URL`、Admin 运行时配置、同一个默认网关的顺序解析。

`image.enhance` 是异步图像增强任务入口，输入为一张或多张图片，`pipeline.steps` 按用户传入顺序执行，最终可输出图片页，或通过现有文件转换能力导出 PDF/OFD/TIFF。`sane.scan` 和 `capture.take` 也支持传入同一份 `pipeline` 作为后置处理；SANE 会在扫描页完成后先增强再导出，Capture 会在现有 final 图片资产上执行增强步骤。空白页检测默认参数为 `action=drop`。

`image.enhance_workflow_*` 方法用于把用户自定义的增强 pipeline 保存到 SDK 工作目录。Demo 会在图像增强页保存工作流，并在拍照采集或 SANE 扫描流程中选择复用。
=======
### SANE Linux-only 说明

`sane.*` 是 Linux 专属扫描仪能力域，用于第三方 SANE 扫描仪的发现、插拔监听、打开会话、配置读取/设置、配置记录和扫描任务。SDK 不沿用旧客户端的 publicd/子进程管理链路，私有 provider 在进程内直接管理 SANE runtime 和会话。非 Linux 平台保留方法可见性，但 `sane.status` 会返回 `available=false`，其他 `sane.*` 方法返回 SANE 不可用错误。

`sane.list` 默认只返回 SANE backend 已识别且可打开的设备，USB/finder 发现结果仅用于触发热插拔刷新和诊断。需要查看 finder 原始发现结果时，可传 `include_detected=true`，响应会额外返回 `detected_devices/detected_count`；Demo 默认不把这些诊断设备展示为可扫描设备，避免同一台扫描仪出现两行。SANE 只支持 Linux runtime。`sane.scan` 未指定输出目录时，会写入 SDK 自己的任务资产目录（可通过 `SDK_OPEN_WORK_DIR` 调整根目录），不再使用旧客户端工作目录。SDK 不对 SANE 暴露模拟的预览模式或分页模式；单页/连续取纸完全跟随设备参数：`source` 类似 Flatbed 时扫描一页，`source` 类似 ADF/Feeder/Duplex 时持续拉取页面直到设备返回无纸。`sane.scan` 是异步任务提交接口，立即返回 `accepted/task_id/task`；通过 `sane.scan_get`、`sane.scan_cancel` 和 `sane.scan_changed` 事件查看任务状态、当前页、转换阶段、完成、失败或取消。
>>>>>>> Feature(sane): add async scan workflow

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

## 环境变量

`sdk_open_app` 支持以下端口和鉴权覆盖：

- `SDK_ADMIN_HTTP_PORT`
- `SDK_DEMO_HTTP_PORT`
- `SDK_ASSET_HTTP_PORT`
- `SDK_ASSET_BASE_URL`
- `SDK_COMMAND_WS_PORT`
- `SDK_VIDEO_WS_PORT`
- `SDK_AUTH_TOKEN`

Asset API 需要使用会话授权访问：

```bash
curl -H "Authorization: Bearer <session_token>" \
  http://127.0.0.1:17082/api/assets/<task_id>/<asset_id>
```

## 文档

- 目标架构说明：[doc/RUNTIME_ARCHITECTURE_ZH.md](./doc/RUNTIME_ARCHITECTURE_ZH.md)
- 指令通道说明：[doc/COMMAND_CHANNEL_FLOW_ZH.md](./doc/COMMAND_CHANNEL_FLOW_ZH.md)
- 错误码说明：[doc/ERROR_CODES_ZH.md](./doc/ERROR_CODES_ZH.md)
- English README：[README.md](./README.md)
