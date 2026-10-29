# CZUR SDK 统一授权、管理观测接口边界与错误码规范

> 状态：草案 v1
>
> 适用范围：`sdk_app` 对外指令协议、事件协议、能力授权判定，以及管理站观测接口边界
>
> 配套文档：`Plugin_Capability.md`

## 1. 设计目标

SDK 统一授权层的目标是：

- 对外仅暴露稳定的 `ApiKey -> AuthContext` 语义
- 支持本地离线授权判定
- 统一账号等级与设备白名单校验
- 统一错误码，屏蔽内部 `AuthManager` 细节
- 明确 SDK 业务能力授权与管理站观测接口的边界，避免两套校验语义混用

SDK 不直接把现有客户端授权文件结构、备份授权逻辑、机器码文件等内部实现暴露给接入方。

## 2. 统一授权模型

## 2.1 请求侧 auth 对象

所有受保护 method 都使用统一 `auth` 对象：

```json
{
  "auth": {
    "api_key": "czur_xxx",
    "session_token": "optional"
  }
}
```

规则：

- 若同时携带 `api_key` 和 `session_token`，优先校验 `session_token`
- 若 `session_token` 缺失或失效，可回退到 `api_key`
- 匿名能力可不传 `auth`

## 2.2 AuthContext

服务端内部与对外返回统一使用 `AuthContext` 语义。

```json
{
  "is_valid": true,
  "account_type": "svip_plus",
  "account_type_code": 2,
  "auth_scene": "plugin",
  "license_mode": "offline_api_key",
  "device_scope": [
    {
      "vid": 4660,
      "pid": 22136
    }
  ],
  "expires_at": "",
  "capabilities": [
    "capture.take",
    "ocr.recognize"
  ]
}
```

字段定义：

| 字段 | 类型 | 含义 |
|---|---|---|
| `is_valid` | `bool` | ApiKey 是否合法 |
| `account_type` | `string` | 账号等级，见 3.1 |
| `account_type_code` | `int` | 账号等级数值映射 |
| `auth_scene` | `string` | 当前授权场景，SDK 固定为 `plugin` |
| `license_mode` | `string` | 当前授权模式，v1 约定为 `offline_api_key` |
| `device_scope` | `array` | 允许访问的 CZUR 设备 `vid/pid` 白名单 |
| `expires_at` | `string` | 到期时间；永久授权可为空 |
| `capabilities` | `array` | 当前账号允许访问的 method 列表或 capability 列表 |

## 2.3 本地离线 ApiKey 的最低要求

在本地离线模式下，SDK 必须能够从 ApiKey 解析出三个核心信息：

1. `is_valid`：ApiKey 是否合法
2. `account_type`：账号类型
3. `device_scope`：支持的 CZUR 设备信息，至少包含 `vid`、`pid`

这三个信息是 SDK 侧统一授权判定的最小闭环。

## 3. 账号类型模型

## 3.1 对外暴露的等级

SDK 对外统一收敛为以下账号等级：

| `account_type` | `account_type_code` | 说明 |
|---|---:|---|
| `unknown` | `-1` | 未识别或未授权 |
| `vip` | `0` | VIP 能力等级 |
| `svip` | `1` | SVIP 能力等级 |
| `svip_plus` | `2` | SVIPPLUS 能力等级 |
| `custom` | `3` | 定制项目能力 |

说明：

- 文档对外不区分 `CLIENT / PLUGIN / BOTH`
- 文档对外不强制区分 `PERMANENT / LIMITED / TRIAL`
- 如果后续确实需要对外暴露时长或试用态，应新增独立字段，而不是扩展 `account_type`

## 3.2 与现有内部授权类型的映射

SDK 外部语义与当前 `AuthType` 的映射如下：

| 内部类型范围 | 对外 `account_type` |
|---|---|
| `TYPE_*_VIP_*` | `vip` |
| `TYPE_*_SVIP_*` | `svip` |
| `TYPE_*_SVIPLUS_*` | `svip_plus` |
| `TYPE_INVITE_90_DAYS_SVIPLUS_BOTH` | `svip_plus` |
| `TYPE_CUSTOM_KEY_WORDS_DETECT` | `custom` |
| `TYPE_UNKNOWN` | `unknown` |

SDK 的默认授权场景为 `plugin`，因此内部若仍沿用 `AuthManager`，应按插件场景解释当前授权类型。

## 4. 授权校验规则

## 4.1 校验流程

对于受保护请求，服务端按以下顺序校验：

1. 提取 `session_token` 或 `api_key`
2. 生成 `AuthContext`
3. 判断 `is_valid`
4. 判断 method 所需 `auth_scope`
5. 若 method 绑定 CZUR 设备，校验目标设备是否落在 `device_scope`
6. 通过后再进入业务层

## 4.2 device_scope 规则

`device_scope` 仅约束 CZUR 设备访问权限，规则如下：

- `device_scope` 为空，表示“无 CZUR 设备权限”
- 不允许把空数组解释为“无限制”
- 匹配以 `{vid, pid}` 为最小粒度
- 若一个 ApiKey 支持多个机型，则 `device_scope` 为多项列表
- `device.list` 只能返回命中的设备
- `device.open`、`capture.take`、`stream.subscribe` 等方法必须对目标设备逐一校验

示例：

```json
{
  "device_scope": [
    { "vid": 4660, "pid": 22136 },
    { "vid": 4660, "pid": 22137 }
  ]
}
```

## 4.3 非 CZUR 设备能力规则

以下场景不受 `device_scope` 约束：

- `system.*`
- `auth.*`
- `image.*`
- `ocr.*`
- `file.*`
- `sane.*`

这些能力只受账号等级与 method 级 capability 控制。

## 4.4 授权失败判定

| 场景 | 结果 |
|---|---|
| 未提供凭证但 method 需要认证 | `1100 AUTH_REQUIRED` |
| ApiKey 非法 | `1101 API_KEY_INVALID` |
| ApiKey 已过期 | `1102 API_KEY_EXPIRED` |
| 账号类型不足 | `1104 ACCOUNT_TYPE_NOT_ALLOWED` |
| 目标设备不在白名单 | `1105 DEVICE_NOT_IN_AUTH_SCOPE` |
| 授权场景与 SDK 不匹配 | `1106 AUTH_SCENE_MISMATCH` |


## 4.5 管理站观测接口边界

`sdk_app` 内部存在两类完全不同的访问面：

1. SDK 业务能力接口
2. 管理站观测接口（admin observability APIs）

两者必须分开定义、分开校验，不能把管理站状态接口混入 `ApiKey -> AuthContext` 的业务授权流程。

### 4.5.1 边界原则

- `src/sdk_open/providers/private/auth` 只负责 SDK 业务能力授权
- 管理站接口单独定义为 `admin observability APIs`
- 管理站接口默认不走 `api_key` / `session_token` 校验
- 管理站里只有最基础 health 可匿名，其余状态/日志类接口统一走 `auth_token`
- 管理站接口不参与 `account_type`、`device_scope`、`capabilities` 判定
- SDK 业务能力接口不应复用管理站 `auth_token` 作为授权凭证

### 4.5.2 为什么要分离

- 管理站接口的目标是运行态观测，不是业务能力授权
- 业务授权关注账号等级、能力清单和设备白名单
- 管理站接口关注的是本机服务状态、WS 状态、Provider 状态、日志和诊断信息
- 若两者混用，会导致管理页必须依赖业务 ApiKey 才能打开，语义和实现都不合理

### 4.5.3 推荐的校验模型

管理站接口建议分为两级：

| 分级 | 接口类型 | 认证要求 | 说明 |
|---|---|---|---|
| `admin_public` | 最基础 health 检查 | 匿名 | 仅回答“服务是否活着” |
| `admin_protected` | 状态、日志、诊断、配置类接口 | `auth_token` | 用于后台页和内部排障 |

管理站接口明确不使用以下业务授权要素：

- `api_key`
- `session_token`
- `AuthContext`
- `account_type`
- `device_scope`
- method 级 capability

### 4.5.4 当前与后续接口归类规则

当前和未来的接口建议按以下规则归类：

| 接口 | 归类 | 校验要求 | 备注 |
|---|---|---|---|
| `GET /healthz` 或等价基础健康检查接口 | `admin_public` | 匿名 | 建议仅返回存活状态、版本摘要、时间戳 |
| `GET /api/status` | `admin_protected` | `Authorization: Bearer <auth_token>` | 当前管理站主状态接口 |
| `GET /api/logs` | `admin_protected` | `auth_token` | 未来日志查询接口 |
| `GET /api/ws_stats` | `admin_protected` | `auth_token` | 未来 WS 统计接口 |
| `GET /api/providers` | `admin_protected` | `auth_token` | 未来 Provider 状态接口 |
| `GET /api/config` | `admin_protected` | `auth_token` | 未来配置与端点接口 |
| `GET /api/errors/recent` | `admin_protected` | `auth_token` | 未来错误聚合接口 |

说明：

- 若某接口会暴露端口、连接数、Provider 名称、日志、配置、错误明细，则不得归入匿名接口
- 匿名 health 接口只应返回最小必要信息，不应返回日志、路径、Provider 明细、授权上下文等敏感内容
- 若当前尚未单独实现 `healthz`，可在后续实现中新增，不建议直接把 `/api/status` 放开为匿名

### 4.5.5 与 `system.*` 的关系

`system.*` 属于 SDK 对外协议的一部分，不属于管理站观测接口命名空间。

建议边界如下：

- `system.ping`
- `system.info`
- `system.capabilities`

这些 method 继续按 SDK 协议语义维护，可用于 SDK 接入调试和 Demo 验证。

但管理站页面的运行态观测，不应长期依赖 `system.*` 作为唯一数据源，建议逐步迁移到专用 `admin observability APIs`。

理由：

- `system.*` 是面对 SDK 客户端的协议能力
- 管理站观测接口是面对内部后台页的运行态接口
- 两者职责不同，后续字段扩展频率和安全边界也不同

## 5. 鉴权接口定义

## 5.1 `auth.validate`

用途：

- 验证 ApiKey 是否可用
- 返回完整 `AuthContext`
- 作为接入方初始化和调试时的首选鉴权接口

示例请求：

```json
{
  "request_id": "auth-001",
  "method": "auth.validate",
  "params": {},
  "auth": {
    "api_key": "czur_xxx"
  }
}
```

示例响应：

```json
{
  "request_id": "auth-001",
  "code": 0,
  "message": "ok",
  "data": {
    "auth_context": {
      "is_valid": true,
      "account_type": "svip_plus",
      "account_type_code": 2,
      "auth_scene": "plugin",
      "license_mode": "offline_api_key",
      "device_scope": [
        { "vid": 4660, "pid": 22136 }
      ],
      "expires_at": "",
      "capabilities": [
        "device.list",
        "device.open",
        "capture.take",
        "ocr.recognize"
      ]
    }
  }
}
```

## 5.2 `auth.refresh`

用途：

- 用长期 `api_key` 换取短期 `session_token`
- 降低后续请求中长期密钥暴露的频率

v1 约束：

- 若尚未实现独立会话系统，可暂时返回 `120m` 以内的本地 session
- 若未实现该能力，允许客户端每次直接传 `api_key`

返回建议：

```json
{
  "session_token": "sess_xxx",
  "expires_in": 7200,
  "auth_context": {
    "is_valid": true,
    "account_type": "vip",
    "account_type_code": 0,
    "auth_scene": "plugin",
    "license_mode": "offline_api_key",
    "device_scope": [
      { "vid": 4660, "pid": 22136 }
    ]
  }
}
```

## 6. 统一错误响应格式

所有失败响应都遵循统一结构：

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

- `code`：稳定数值错误码
- `message`：面向接入方的简短描述
- `error.name`：稳定错误名
- `error.details`：可选扩展诊断字段
- `trace_id`：服务端日志追踪号

## 7. 错误码分段

| 分段 | 范围 | 说明 |
|---|---|---|
| 通用请求错误 | `1000-1099` | 协议格式、参数、方法错误 |
| 认证与授权错误 | `1100-1199` | ApiKey、session、账号等级、设备范围 |
| 设备错误 | `1200-1299` | 设备发现、打开、占用、模型不支持 |
| 采集与视频错误 | `1300-1399` | 视频流、拍照、预览、翻页检测 |
| 图像处理错误 | `1400-1499` | 图像处理、规格化、水印等 |
| OCR 与识别错误 | `1500-1599` | OCR、语言包、条码识别 |
| 文件与 OFD 错误 | `1600-1699` | 文件不存在、转换失败、上传打印 |
| SANE 错误 | `1700-1799` | 第三方扫描设备错误 |
| 内部系统错误 | `1900-1999` | provider、内部异常、不可恢复错误 |

## 8. 统一错误码定义

## 8.1 成功

| code | name | 说明 |
|---:|---|---|
| `0` | `OK` | 成功 |

## 8.2 通用请求错误

| code | name | 说明 |
|---:|---|---|
| `1000` | `INVALID_REQUEST` | 请求体结构非法 |
| `1001` | `INVALID_METHOD` | method 字段缺失或非法 |
| `1002` | `INVALID_PARAMS` | 参数不完整或类型不匹配 |
| `1003` | `UNSUPPORTED_METHOD` | method 未开放或未实现 |
| `1004` | `REQUEST_TIMEOUT` | 请求超时 |
| `1005` | `RATE_LIMITED` | 命中限流 |
| `1006` | `TEXT_MESSAGE_REQUIRED` | 指令通道要求文本消息 |
| `1007` | `BINARY_MESSAGE_REQUIRED` | 视频通道要求二进制消息 |

## 8.3 认证与授权错误

| code | name | 说明 |
|---:|---|---|
| `1100` | `AUTH_REQUIRED` | 当前 method 需要认证 |
| `1101` | `API_KEY_INVALID` | ApiKey 非法或不可解析 |
| `1102` | `API_KEY_EXPIRED` | ApiKey 已过期 |
| `1103` | `SESSION_TOKEN_INVALID` | session token 非法或过期 |
| `1104` | `ACCOUNT_TYPE_NOT_ALLOWED` | 账号等级不足 |
| `1105` | `DEVICE_NOT_IN_AUTH_SCOPE` | 目标设备不在 ApiKey 白名单中 |
| `1106` | `AUTH_SCENE_MISMATCH` | 当前授权场景不适用于 SDK |
| `1107` | `CAPABILITY_NOT_GRANTED` | 当前账号未被授予该能力 |

## 8.4 设备错误

| code | name | 说明 |
|---:|---|---|
| `1200` | `DEVICE_NOT_FOUND` | 未找到目标设备 |
| `1201` | `DEVICE_BUSY` | 设备已被占用 |
| `1202` | `DEVICE_OPEN_FAILED` | 设备打开失败 |
| `1203` | `DEVICE_NOT_OPEN` | 设备尚未打开 |
| `1204` | `DEVICE_MODEL_UNSUPPORTED` | 当前机型不支持该能力 |
| `1205` | `DEVICE_DISCONNECTED` | 调用期间设备断开 |

## 8.5 采集与视频错误

| code | name | 说明 |
|---:|---|---|
| `1300` | `STREAM_NOT_READY` | 视频流未就绪 |
| `1301` | `CAPTURE_FAILED` | 拍照失败 |
| `1302` | `PREVIEW_RESOLUTION_UNSUPPORTED` | 预览分辨率不支持 |
| `1303` | `TURN_DETECT_NOT_SUPPORTED` | 当前设备不支持翻页检测 |
| `1304` | `SCAN_AREA_INVALID` | 扫描幅面或区域参数非法 |

## 8.6 图像处理错误

| code | name | 说明 |
|---:|---|---|
| `1400` | `IMAGE_INPUT_INVALID` | 输入图像非法 |
| `1401` | `IMAGE_PROCESS_FAILED` | 图像处理失败 |
| `1402` | `WATERMARK_INVALID` | 水印参数非法 |
| `1403` | `NORMALIZATION_FAILED` | 规格化失败 |
| `1404` | `IMAGE_OPERATION_UNSUPPORTED` | 当前 provider 不支持该处理操作 |

## 8.7 OCR 与识别错误

| code | name | 说明 |
|---:|---|---|
| `1500` | `OCR_FAILED` | OCR 执行失败 |
| `1501` | `OCR_LANGUAGE_UNSUPPORTED` | OCR 语言不支持 |
| `1502` | `BARCODE_DECODE_FAILED` | 条码识别失败 |
| `1503` | `REALTIME_BARCODE_NOT_SUPPORTED` | 当前设备不支持实时条码检测 |

## 8.8 文件与 OFD 错误

| code | name | 说明 |
|---:|---|---|
| `1600` | `FILE_NOT_FOUND` | 文件不存在 |
| `1601` | `FILE_CONVERT_FAILED` | 文件转换失败 |
| `1602` | `OFD_PROCESS_FAILED` | OFD 处理失败 |
| `1603` | `UPLOAD_FAILED` | 文件上传失败 |
| `1604` | `PRINT_FAILED` | 文件打印失败 |
| `1605` | `FILE_METADATA_INVALID` | 文件元数据非法 |

## 8.9 SANE 错误

| code | name | 说明 |
|---:|---|---|
| `1700` | `SANE_NOT_AVAILABLE` | 宿主机未启用 SANE 能力 |
| `1701` | `SANE_DEVICE_NOT_FOUND` | 未找到目标 SANE 设备 |
| `1702` | `SANE_OPTION_INVALID` | SANE 参数非法 |
| `1703` | `SANE_SCAN_FAILED` | SANE 扫描失败 |
| `1704` | `SANE_SCAN_CANCELED` | SANE 扫描已取消 |

## 8.10 内部系统错误

| code | name | 说明 |
|---:|---|---|
| `1900` | `INTERNAL_ERROR` | 未分类内部错误 |
| `1901` | `PROVIDER_NOT_READY` | provider 未装配完成 |
| `1902` | `PROVIDER_CALL_FAILED` | provider 调用失败 |
| `1903` | `SERVICE_UNAVAILABLE` | 服务暂不可用 |

## 9. 与现有 AuthErrorCode 的映射原则

SDK 对外错误码不直接复用当前 `AuthErrorCode` 的数值，内部需要建立映射层。

推荐映射如下：

| 现有内部错误 | SDK 对外错误 |
|---|---|
| `LICENSE_KEY_INVALID` | `1101 API_KEY_INVALID` |
| `EXPIRED_LICENSE` | `1102 API_KEY_EXPIRED` |
| `MACHINE_CODE_MISMATCH` | `1105 DEVICE_NOT_IN_AUTH_SCOPE` |
| `INVALID_GET_TIMELEFT_FLAG` | `1106 AUTH_SCENE_MISMATCH` |
| `SOFTWARE_RUNTIME_EXCEED_LICENSE` | `1102 API_KEY_EXPIRED` |
| `CHECKSUM_VALIDATION_FAILED` | `1101 API_KEY_INVALID` |
| `SYSTEM_CLOCK_VALIDATION_ABNORMAL` | `1101 API_KEY_INVALID` |
| `LICENSE_FILE_NOT_FOUND` | `1903 SERVICE_UNAVAILABLE` 或 `1101 API_KEY_INVALID` |
| `LICENSE_FILE_DECRYPT_FAILED` | `1101 API_KEY_INVALID` |
| `UNKNOWN_ERROR` | `1900 INTERNAL_ERROR` |

说明：

- 对外协议稳定优先，内部可继续保留当前授权实现
- 若一个内部错误在 SDK 语义上存在多种解释，应优先选择对接入方最可执行的错误语义
- 原始内部错误码应保留在日志或 `trace_id` 关联日志中，不直接暴露给外部

## 10. 建议的 capability 判定模型

除 `account_type` 外，服务端还应保留“按 method 授权”的二级判定结果。推荐输出为 capability 集合。

示例：

```json
{
  "account_type": "vip",
  "capabilities": [
    "system.*",
    "auth.*",
    "device.*",
    "capture.take",
    "image.process",
    "ocr.recognize",
    "file.convert"
  ]
}
```

推荐约束：

- `account_type` 决定默认能力上限
- `capabilities` 决定最终可调用 method 集
- 如存在定制客户或特殊项目，优先通过 `capabilities` 精确收口，而不是继续扩充 `account_type`

## 11. 建议的管理站接口分级清单

为避免实现阶段反复争论，当前建议直接采用以下可执行清单。

### 11.1 匿名可访问

仅允许最小健康检查：

- `GET /healthz`
- `GET /readyz`（如后续需要区分存活与就绪）

返回字段建议限制在：

- `ok`
- `service`
- `ts`
- `version`

禁止在匿名接口中返回：

- 端口详情
- Provider 名称
- WS 连接数
- 授权上下文
- 日志内容
- 文件路径
- 详细配置

### 11.2 必须走 `auth_token`

以下接口统一归入 `admin_protected`：

- `GET /api/status`
- `GET /api/logs`
- `GET /api/ws_stats`
- `GET /api/providers`
- `GET /api/config`
- `GET /api/errors/recent`
- 其他所有管理站内部诊断接口

这些接口统一使用：

- `Authorization: Bearer <auth_token>`

不使用：

- `api_key`
- `session_token`
- `AuthContext`

### 11.3 必须走 SDK 业务授权

以下接口或 method 继续走正式 SDK 授权链路：

- 所有 `auth.*`
- 所有 `device.*`
- 所有 `stream.*`
- 所有 `capture.*`
- 所有 `image.*`
- 所有 `ocr.*`
- 所有 `recognition.*`
- 所有 `file.*`
- 所有 `sane.*`

这些能力继续按本文档既有规则执行：

- `auth_scope`
- `device_scope`
- `account_type`
- `capabilities`
- 统一错误码

### 11.4 当前代码实现对应关系

结合当前仓库实现，边界可解释为：

- `/api/status` 已是 `admin_protected`
- `system.ping`、`system.info`、`system.capabilities` 当前仍是 SDK 侧匿名 method
- `auth.validate`、`auth.refresh`、`auth.get_context` 继续属于 SDK 业务授权域

因此当前实现方向应为：

- 保持 `/api/status` 的 `auth_token` 校验
- 新增匿名 health 接口时，只开放最小健康信息
- 不把管理站状态页改造成必须输入 `api_key` 才能访问
- 不把业务 method 的鉴权逻辑迁移到管理站 token 上

## 12. 与当前 runtime stub 的兼容说明

当前仓库中的 runtime stub 尚未接入正式授权层，存在以下差异：

- 指令请求里仍使用 `id` 字段，而非正式 `request_id`
- 错误码仍直接返回 `400`、`404`
- 尚未解析 `auth` 对象

这些行为仅视为开发阶段占位，不构成正式对外契约。后续实现应以本文档定义为准，并建立向后兼容层：

- `id` 兼容映射为 `request_id`
- `400` 迁移到 `1000-1002`
- `404` 迁移到 `1003`

## 13. 落地要求

- 所有正式 method 必须绑定稳定错误码
- 所有受保护 method 必须走统一 `AuthContext`
- 所有与 CZUR 设备交互的 method 必须显式做 `vid/pid` 白名单校验
- 文档新增字段和错误码只能扩展，不能随意重定义既有含义
- 管理站观测接口必须与 SDK 业务授权链路分开实现
- 匿名管理接口必须严格限制返回字段，只允许最小健康检查
- 管理站状态、日志、诊断类接口默认必须走 `auth_token`
