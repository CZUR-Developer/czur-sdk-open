# CZUR Open SDK 错误码说明

[English version](./ERROR_CODES.md)

## 概述

本文档定义 `sdk_open` 对外公开的错误码契约，适用于命令接口、事件接口，以及相关管理观测接口。

该文档面向外部集成方，作为稳定的公开参考。内部实现可以继续演进，但这些错误码对外的语义应保持稳定。

当前说明：

- 当前 `sdk_open` runtime 只实现了完整协议面中的一部分。
- 本文档描述的是 open SDK 对外的完整规划错误码模型。
- 即使某些 method 或域尚未落地，其错误码区间和语义也应视为已预留。

## 响应结构

命令响应和错误响应统一采用如下 JSON 结构：

```json
{
  "request_id": "req-001",
  "code": 1101,
  "message": "api key invalid",
  "data": {},
  "error": {
    "name": "API_KEY_INVALID",
    "details": {
      "reason": "signature_mismatch"
    }
  },
  "trace_id": "trace-001",
  "ts": 1710000000
}
```

字段说明：

- `code`：稳定的数值错误码。
- `message`：面向接入方的简短说明。
- `error.name`：稳定错误名，可选返回。
- `error.details`：可选诊断信息。
- `trace_id`：服务端链路追踪号，用于排障和日志定位。
- `ts`：服务端 Unix 时间戳。

## 访问模型

`sdk_open` 暴露两类访问面：

- SDK 业务能力接口
- 管理观测接口

校验规则如下：

- SDK 业务能力接口使用 `api_key`、`session_token`、`auth_scope` 和 capability 判定。
- 管理观测接口不使用 `api_key` 或 `session_token`。
- 最基础的健康检查接口可以匿名访问。
- 受保护的管理接口应使用 `Authorization: Bearer <auth_token>`。

本文档中的错误码主要面向 SDK 业务协议。受保护的管理接口在传输层还可能返回 HTTP `401 Unauthorized`。

## 错误码分段

| 范围 | 类别 | 含义 |
|---|---|---|
| `0` | 成功 | 请求执行成功 |
| `1000-1099` | 通用请求错误 | 协议格式、方法、参数、超时、限流 |
| `1100-1199` | 认证与授权错误 | ApiKey、session、账号等级、能力、设备范围 |
| `1200-1299` | 设备错误 | 设备发现、打开、占用、断开、机型支持 |
| `1300-1399` | 采集与视频错误 | 视频流、拍照、预览、扫描区域、翻页检测 |
| `1400-1499` | 图像处理错误 | 图像输入、处理、规格化、水印 |
| `1500-1599` | OCR 与识别错误 | OCR、语言支持、条码识别 |
| `1600-1699` | 文件与 OFD 错误 | 文件查询、转换、上传、打印、元数据 |
| `1700-1799` | SANE 错误 | 第三方扫描仪相关错误 |
| `1900-1999` | 内部系统错误 | Provider 状态、内部失败、服务不可用 |

## 成功

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `0` | `OK` | 成功 |

## 通用请求错误

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `1000` | `INVALID_REQUEST` | 请求体结构非法 |
| `1001` | `INVALID_METHOD` | `method` 缺失或非法 |
| `1002` | `INVALID_PARAMS` | 参数缺失、不完整或类型不匹配 |
| `1003` | `UNSUPPORTED_METHOD` | method 未开放或尚未实现 |
| `1004` | `REQUEST_TIMEOUT` | 请求超时 |
| `1005` | `RATE_LIMITED` | 请求命中限流 |
| `1006` | `TEXT_MESSAGE_REQUIRED` | 指令通道要求文本消息 |
| `1007` | `BINARY_MESSAGE_REQUIRED` | 视频通道要求二进制消息 |

## 认证与授权错误

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `1100` | `AUTH_REQUIRED` | 当前 method 需要认证 |
| `1101` | `API_KEY_INVALID` | ApiKey 非法或不可解析 |
| `1102` | `API_KEY_EXPIRED` | ApiKey 已过期 |
| `1103` | `SESSION_TOKEN_INVALID` | session token 非法或过期 |
| `1104` | `ACCOUNT_TYPE_NOT_ALLOWED` | 当前账号等级不足 |
| `1105` | `DEVICE_NOT_IN_AUTH_SCOPE` | 目标设备不在授权设备范围内 |
| `1106` | `AUTH_SCENE_MISMATCH` | 当前授权场景不符合 SDK 要求 |
| `1107` | `CAPABILITY_NOT_GRANTED` | 当前账号未被授予该 method 或 capability |

## 设备错误

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `1200` | `DEVICE_NOT_FOUND` | 未找到目标设备 |
| `1201` | `DEVICE_BUSY` | 设备已被占用 |
| `1202` | `DEVICE_OPEN_FAILED` | 设备打开失败 |
| `1203` | `DEVICE_NOT_OPEN` | 设备尚未打开 |
| `1204` | `DEVICE_MODEL_UNSUPPORTED` | 当前机型不支持该能力 |
| `1205` | `DEVICE_DISCONNECTED` | 调用过程中设备断开 |

## 采集与视频错误

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `1300` | `STREAM_NOT_READY` | 视频流未就绪 |
| `1301` | `CAPTURE_FAILED` | 拍照失败 |
| `1302` | `PREVIEW_RESOLUTION_UNSUPPORTED` | 预览分辨率不支持 |
| `1303` | `TURN_DETECT_NOT_SUPPORTED` | 当前设备不支持翻页检测 |
| `1304` | `SCAN_AREA_INVALID` | 扫描区域或幅面参数非法 |

## 图像处理错误

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `1400` | `IMAGE_INPUT_INVALID` | 输入图像非法 |
| `1401` | `IMAGE_PROCESS_FAILED` | 图像处理失败 |
| `1402` | `WATERMARK_INVALID` | 水印参数非法 |
| `1403` | `NORMALIZATION_FAILED` | 图像规格化失败 |
| `1404` | `IMAGE_OPERATION_UNSUPPORTED` | 当前 provider 不支持该图像处理操作 |

## OCR 与识别错误

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `1500` | `OCR_FAILED` | OCR 执行失败 |
| `1501` | `OCR_LANGUAGE_UNSUPPORTED` | OCR 语言不支持 |
| `1502` | `BARCODE_DECODE_FAILED` | 条码或二维码识别失败 |
| `1503` | `REALTIME_BARCODE_NOT_SUPPORTED` | 当前设备不支持实时条码识别 |

## 文件与 OFD 错误

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `1600` | `FILE_NOT_FOUND` | 文件不存在 |
| `1601` | `FILE_CONVERT_FAILED` | 文件转换失败 |
| `1602` | `OFD_PROCESS_FAILED` | OFD 处理失败 |
| `1603` | `UPLOAD_FAILED` | 上传失败 |
| `1604` | `PRINT_FAILED` | 打印失败 |
| `1605` | `FILE_METADATA_INVALID` | 文件元数据非法 |

## SANE 错误

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `1700` | `SANE_NOT_AVAILABLE` | 宿主机未启用 SANE 能力 |
| `1701` | `SANE_DEVICE_NOT_FOUND` | 未找到目标 SANE 设备 |
| `1702` | `SANE_OPTION_INVALID` | SANE 参数非法 |
| `1703` | `SANE_SCAN_FAILED` | SANE 扫描失败 |
| `1704` | `SANE_SCAN_CANCELED` | SANE 扫描已取消 |

## 内部系统错误

| 错误码 | 名称 | 说明 |
|---:|---|---|
| `1900` | `INTERNAL_ERROR` | 未分类内部错误 |
| `1901` | `PROVIDER_NOT_READY` | Provider 未初始化或未装配 |
| `1902` | `PROVIDER_CALL_FAILED` | Provider 调用失败 |
| `1903` | `SERVICE_UNAVAILABLE` | 服务暂时不可用 |

## 兼容性说明

当前 runtime 的兼容性说明：

- 历史实现中可能仍接受 `id` 并映射为 `request_id`。
- 早期 runtime stub 可能仍返回过渡状态码，如 `400` 或 `404`。
- 对外接入时，应以本文档中的错误码作为目标稳定契约。

## 文档链接

- 英文版本：[ERROR_CODES.md](./ERROR_CODES.md)
- 主项目说明：[../README_ZH.md](../README_ZH.md)
