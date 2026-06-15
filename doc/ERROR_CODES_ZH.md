# CZUR Open SDK 错误码说明

[English version](./ERROR_CODES.md)

## 概述

本文档定义 `sdk_open` 当前公开的稳定错误码语义。

适用范围：

- command WebSocket 响应
- command/video 相关事件的错误状态
- 业务调用统一错误映射

说明：

- 管理接口同时还可能返回标准 HTTP 状态码
- 业务错误码以 `src/sdk_open/interfaces/sdk_status_code.h` 为准

## 响应结构

```json
{
  "request_id": "req-001",
  "code": 1101,
  "message": "token invalid",
  "data": {},
  "ts": 1710000000
}
```

字段说明：

- `request_id`：对应请求标识
- `code`：稳定错误码
- `message`：简短错误说明
- `data`：可选扩展信息
- `ts`：服务端时间戳

## 访问面

`sdk_open` 有两类访问面：

- SDK 业务协议
- 管理观测接口

规则：

- SDK 业务协议采用 `token -> session_token -> connection-bound session` 模型
- 管理接口不参与业务 capability 判定
- `GET /healthz` 可匿名访问
- `GET /api/status` 使用 `Authorization: Bearer <auth_token>`

## 错误码分组

| 范围 | 类别 | 说明 |
|---|---|---|
| `0` | 成功 | 请求执行成功 |
| `1000-1099` | 请求协议错误 | 请求结构、方法、参数、限流 |
| `1100-1199` | 认证与授权错误 | token、session、capability、scope |
| `1300-1399` | 视频流错误 | stream 生命周期相关 |
| `1900-1999` | 运行时内部错误 | provider 与内部失败 |

## 当前定义的错误码

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `0` | `OK` | 成功 |
| `1000` | `INVALID_REQUEST` | 请求体结构非法 |
| `1001` | `INVALID_METHOD` | `method` 缺失或非法 |
| `1002` | `INVALID_PARAMS` | `params` 缺失、不完整或类型不匹配 |
| `1003` | `UNSUPPORTED_METHOD` | method 未公开或尚未实现 |
| `1004` | `RATE_LIMITED` | 请求命中限流或并发保护 |
| `1005` | `UPLOAD_FILE_EMPTY` | 上传字段存在，但文件内容为空 |
| `1006` | `UPLOAD_FILE_TOO_LARGE` | 上传文件超过 50 MB 限制 |
| `1100` | `AUTH_REQUIRED` | 当前方法需要已绑定的合法会话 |
| `1101` | `TOKEN_INVALID` | `token` 非法或不可解析 |
| `1102` | `TOKEN_EXPIRED` | `token` 已过期 |
| `1103` | `SESSION_TOKEN_INVALID` | 当前连接绑定的 `session_token` 非法或已过期 |
| `1104` | `ACCOUNT_TYPE_NOT_ALLOWED` | 当前账号等级不允许访问目标能力 |
| `1105` | `DEVICE_NOT_IN_AUTH_SCOPE` | 目标设备不在授权设备范围内 |
| `1106` | `AUTH_SCENE_MISMATCH` | 当前授权场景不满足 SDK 要求 |
| `1107` | `CAPABILITY_NOT_ALLOWED` | 当前会话未授予目标 method capability |
| `1300` | `STREAM_NOT_FOUND` | `stream_id` 不存在、已关闭或不属于当前会话 |
| `1900` | `INTERNAL_ERROR` | 未分类内部错误 |
| `1901` | `PROVIDER_NOT_READY` | provider 未装配或未初始化完成 |
| `1902` | `PROVIDER_CALL_FAILED` | provider 调用失败 |

## HTTP 管理接口

管理接口额外使用标准 HTTP 状态码：

| HTTP 状态码 | 说明 |
|---:|---|
| `200` | 请求成功 |
| `401` | `Authorization` 缺失或无效 |
| `404` | 路径不存在 |

## 使用建议

- 接入方应优先依据 `code` 做稳定判定
- `message` 用于日志和排障，不应作为唯一分支条件
- command WS 与 video WS 都应把 `1100-1107` 视为业务授权错误

## 文档链接

- 目标架构说明：[RUNTIME_ARCHITECTURE_ZH.md](./RUNTIME_ARCHITECTURE_ZH.md)
- 指令通道说明：[COMMAND_CHANNEL_FLOW_ZH.md](./COMMAND_CHANNEL_FLOW_ZH.md)
- 主项目说明：[../README_ZH.md](../README_ZH.md)
