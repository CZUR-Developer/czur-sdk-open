# SDK Open 目标运行时架构说明

> 状态：目标架构设计
>
> 适用范围：`src/sdk_open/` 与 `src/app/process/sdk/`
>
> 本文档只描述最终目标架构、协议模型和目录边界。

## 1. 目标

`sdk_open` 的目标形态是一个可独立运行、可独立开源的本地 SDK 运行时核心，同时也能被主仓内部入口复用。

最终需要同时支持两个可执行入口：

- `src/sdk_open/runtime/sdk_open_main.cpp`
  - 开源独立入口
  - 默认装配 mock providers
- `src/app/process/sdk/sdk_main.cpp`
  - 主仓内部入口
  - 由仓库构建开关决定装配 mock providers 或 private providers

两者共享同一套 `sdk_open` 核心库、相同的协议和相同的分层边界。

## 2. 四层架构

### 2.1 传输层 Transport Layer

职责：

- 承载两个 HTTP 站点：
  - admin site
  - demo site
- 承载 command WebSocket
- 承载 video WebSocket
- 负责连接建立、断开、消息收发、连接级上下文标识
- 不直接持有 provider，不直接实现业务能力

约束：

- 传输层不直接依赖任何 `ISdk*Provider`
- 传输层不直接处理 capability、device scope、账号等级等业务授权规则
- 传输层只负责协议收发、连接生命周期和向应用层回调

### 2.2 应用层 Application Layer

职责：

- command method 路由
- 请求字段校验
- token 创建 session
- session 校验与刷新
- 连接绑定的会话上下文管理
- capability 判定
- device scope 判定
- video stream 生命周期管理
- 统一错误码映射
- 限流、并发保护和任务编排

核心服务：

- `CommandApplicationService`
- `AuthorizationService`
- `VideoSessionService`
- `AdminApplicationService`

约束：

- 应用层不直接操作 WebSocket 或 HTTP 底层连接实现
- 应用层不直接依赖私有能力库头文件
- 应用层通过 Facade 调用业务能力

### 2.3 门面层 Facade Layer

职责：

- 承载按能力域划分的业务用例
- 向应用层暴露稳定的领域动作
- 向下调用 provider adapter

最终门面：

- `DeviceFacade`
- `GraphicFacade`
- `OcrFacade`
- `OfdFacade`

说明：

- `capture.*` 和 `video.*` 归属 `DeviceFacade`
- Facade 不处理 JSON，不直接感知 HTTP/WS 协议细节

### 2.4 适配层 Provider Adapter Layer

职责：

- 对接 mock providers
- 对接 private providers
- 对接仓库内部真实能力库
- 向上暴露 typed DTO 和稳定 provider interface

约束：

- 适配层可以依赖私有能力库
- 适配层不能把私有结构体、私有枚举和私有头文件泄漏给上层
- 所有 provider 向上只通过 `src/sdk_open/interfaces/` 中定义的公共类型交互

## 3. 最终协议模型

### 3.1 Command WS

最终模型：

- command WS 匿名建立连接
- 不在握手 query 中传业务 token
- 所有业务授权都由应用层控制

默认地址：

- `ws://127.0.0.1:17090`

请求结构：

```json
{
  "request_id": "req-001",
  "method": "auth.create_session",
  "params": {
    "token": "czur_xxx"
  },
  "client": {
    "source": "demo-site",
    "protocol_version": "2.0.0",
    "trace_id": "trc-001"
  }
}
```

响应结构：

```json
{
  "request_id": "req-001",
  "code": 0,
  "message": "ok",
  "data": {}
}
```

约束：

- 请求主键只保留 `request_id`
- 不再使用 `id`
- command 业务请求默认不再重复携带 `auth` 对象
- command 连接的会话状态由应用层绑定到连接上下文

### 3.2 会话方法

最终固定的授权方法：

- `auth.create_session`
- `auth.get_context`
- `auth.refresh_session`
- `auth.destroy_session`

规则：

- `auth.create_session` 使用长期 `token` 创建连接绑定会话
- `auth.get_context` 返回当前连接绑定会话的授权上下文
- `auth.refresh_session` 基于当前会话重新签发短期 `session_token`
- `auth.destroy_session` 清除当前连接绑定会话

### 3.3 业务方法

最终业务 method：

- `system.ping`
- `system.info`
- `system.capabilities`
- `device.list`
- `device.get`
- `device.open`
- `capture.take`
- `video.start`
- `video.stop`
- `video.set_format`
- `image.process`
- `ocr.recognize`
- `file.convert`

规则：

- `system.*` 可匿名访问
- 除 `system.*` 和 `auth.*` 外，其余 method 默认要求当前连接已绑定合法会话
- 应用层统一执行 capability 与 device scope 判定

### 3.4 Video WS

最终模型：

- video 控制不再走 video WS 文本控制消息
- `video.start`、`video.stop`、`video.set_format` 全部走 command WS
- video WS 只承载视频帧和必要事件

默认地址：

- `ws://127.0.0.1:17091`

连接参数：

- `session_token`
- `stream_id`

video WS 建连示例：

```text
ws://127.0.0.1:17091?session_token=ss-v2-xxxx&stream_id=stream-001
```

约束：

- video WS 不接受业务控制文本消息
- 应用层通过 `VideoSessionService` 校验 `session_token + stream_id`
- 视频帧默认使用 JPEG 二进制

## 4. 管理接口模型

`sdk_open` 暴露独立的管理观测接口，不复用业务 token。

最终接口分级：

- `GET /healthz`
  - 匿名
  - 仅返回最小健康状态
- `GET /api/status`
  - `Authorization: Bearer <auth_token>`
  - 返回运行态、provider 状态、WS 统计和端口信息

规则：

- 管理接口不参与 `account_type`
- 管理接口不参与 `device_scope`
- 管理接口不参与 method capability 判定
- 管理接口和业务鉴权完全分离

## 5. 目录边界

最终目录如下：

```text
src/
  sdk_open/
    runtime/
      sdk_app.h
      sdk_app.cpp
      sdk_config.h
      sdk_config.cpp
      sdk_logger.h
      sdk_logger.cpp
      sdk_json_utils.h
      sdk_json_utils.cpp
      sdk_open_main.cpp
    transport/
      sdk_http_server.h
      sdk_http_server.cpp
      sdk_ws_command_server.h
      sdk_ws_command_server.cpp
      sdk_ws_video_server.h
      sdk_ws_video_server.cpp
    application/
      command_application_service.h
      command_application_service.cpp
      authorization_service.h
      authorization_service.cpp
      video_session_service.h
      video_session_service.cpp
      admin_application_service.h
      admin_application_service.cpp
    facade/
      device_facade.h
      device_facade.cpp
      graphic_facade.h
      graphic_facade.cpp
      ocr_facade.h
      ocr_facade.cpp
      ofd_facade.h
      ofd_facade.cpp
    interfaces/
      ...
    providers/
      mock/
        ...
    doc/
      ...
```

## 6. 依赖边界

允许依赖方向：

- `runtime` -> `transport` + `application` + `interfaces`
- `transport` -> `interfaces`
- `application` -> `facade` + `interfaces`
- `facade` -> `interfaces`
- `providers/mock` -> `interfaces`
- `sdk_private/providers` -> `interfaces` + 私有能力库

禁止依赖方向：

- `transport` -> `providers/mock`
- `transport` -> `sdk_private`
- `application` -> 私有能力库
- `facade` -> HTTP/WS server 实现
- `sdk_open` -> `sdk_private` 头文件或目标

## 7. Provider 公共接口原则

所有 provider interface 必须使用 typed DTO。

至少包括：

- 设备描述 DTO：
  - `device_id`
  - `model`
  - `display_name`
  - `vid`
  - `pid`
  - `status`
  - `authorized`
  - `supports_video`
- 图像处理请求/结果 DTO
- OCR 请求/结果 DTO
- OFD 请求/结果 DTO
- 视频控制请求/结果 DTO

目的：

- 让应用层和门面层能执行 `device_scope` 判定
- 保证开放层不依赖私有结构体
- 保证 mock/private provider 共享同一份公共合同

## 8. 可执行入口边界

### 8.1 `sdk_open_app`

定位：

- 开源独立可执行入口
- 用于只依赖 `sdk_open` 本身和 mock providers 的独立运行

约束：

- 只链接 `sdk_open` 核心库与 `sdk_mock_providers`
- 不得 include `sdk_private` 头文件
- 不得链接 `sdk_private` 目标

### 8.2 `sdk_app`

定位：

- 主仓内部可执行入口
- 用于复用相同协议和核心库，但按构建开关装配内部 provider

约束：

- 可以按 `BUILD_SDK_PRIVATE` 装配 private providers
- 不修改 `sdk_open` 的公共边界和开放协议

## 9. 最终目标

最终 `sdk_open` 应满足以下条件：

- 可独立构建 `sdk_open_app`
- 可在 mock provider 下完整运行
- 具备明确的四层边界
- command 与 video 协议职责清晰
- 管理接口与业务接口完全分离
- `sdk_app` 与 `sdk_open_app` 共享同一核心实现
- 上层协议稳定，底层 provider 可替换
