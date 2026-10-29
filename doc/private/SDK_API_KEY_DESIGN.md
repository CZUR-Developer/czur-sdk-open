# SDK 私有 ApiKey 设计方案

> 状态：当前实现说明
>
> 适用范围：`src/sdk_open/providers/private`
>
> 最近同步范围：
> - 离线 / 在线双模式 ApiKey
> - 本机解封与离线配额
> - session token 签发与校验
> - `sdk_open` 指令通道接入
> - OpenSSL 1.1 / 3 双构建链路

## 1. 背景

`sdk_private` 负责 SDK 私有授权能力，包括：

- ApiKey 生成与解析
- ApiKey 校验
- session token 签发与校验
- 离线模式的本机解封与本地配额控制
- 在线模式的授权校验与能力消耗

当前设计阶段实现：

- ApiKey 分为 `offline_api_key` 和 `online_api_key`
- 离线模式会关联本机 machine code，并支持通过授权码解封
- 离线 ApiKey 可通过 `host_auth_mode=activation_required` 要求宿主电脑激活；未激活时降级为 trial
- `src/module/authorized` 已经进入主链路，负责 machine code 与授权码解析
- `sdk_open` 通过 `ISdkAuthProvider` 消费 `sdk_private` 产出的授权结果和 session token

## 2. 目标与边界

本方案的目标是提供一套可落地的 SDK 私有授权基础设施，满足以下要求：

- 对外只暴露 ApiKey、session token 和标准 `AuthContext`
- 支持离线体验和在线计费两种模式
- 支持按账号等级映射能力范围
- 支持离线模式下的本机解封
- 支持在线商务授权字段 `commercialAuthorized` 降级 trial
- 支持 trial 访问 VIP 受限能力时的本机共享 200 次试用额度
- 支持后续 provider 基于 session token 做会话态鉴权

本方案当前不覆盖以下内容：

- 多发行方、多租户密钥体系
- 复杂的密钥轮换管理平台
- 在线模式下的 HTTPS 客户端能力
- 细粒度商业策略后台，只保留必要的调用协议

## 3. 总体架构

授权链路拆分如下：

1. `PrivateSdkKeyCodec`
   负责 ApiKey 的生成、解密、签名校验、payload 解析。
2. `PrivateSdkAuthService`
   负责离线 / 在线校验、离线解封、配额消耗、能力集计算。
3. `PrivateSdkSessionService`
   负责 session token 的签发与校验。
4. `PrivateAuthProvider`
   对 `sdk_open` 暴露统一 `ISdkAuthProvider` 接口。
5. `src/module/authorized`
   负责 machine code 生成、授权码解析，以及离线解封时的宿主机绑定判断。

## 4. ApiKey 模型

### 4.1 两种 license mode

当前实现支持两种 ApiKey：

| `license_mode` | 说明 |
|---|---|
| `offline_api_key` | 本地可解析，可在未联网时完成基础授权判断，并带有限次体验能力 |
| `online_api_key` | 本地先做签名与格式校验，再通过在线授权服务完成最终授权与配额判定 |

### 4.2 账号等级

ApiKey 头部的 `tier` 与账号类型映射如下：

| tier 段 | account_type | account_type_code |
|---|---|---:|
| `sr` | `vip` | 0 |
| `sq` | `svip` | 1 |
| `sp` | `svip_plus` | 2 |

运行态还存在独立的 `trial` 账号，`account_type_code=4`。trial 不直接写入 ApiKey 头部 tier，而是在 `activation_required` 的离线 key 尚未完成本机激活时，由授权服务把当前宿主的 effective account 降级为 trial。`licensed_account_type` 会保留 ApiKey 原始购买等级。

### 4.3 格式

ApiKey 固定格式如下：

```text
sk-<tier>-v1-<payload><signature>
```

字段说明：

- `sk-`：固定前缀
- `<tier>`：两位账号等级码
- `v1`：格式版本
- `<payload>`：AES 加密后的二进制 envelope，经 `base64url` 编码
- `<signature>`：固定 32 个字符的小写十六进制签名

### 4.4 payload 示例

`payload` 解密后为 JSON，当前版本示例如下：

```json
{
  "ver": 1,
  "tier": "svip",
  "tier_code": 1,
  "license_mode": "offline_api_key",
  "host_auth_mode": "activation_required",
  "commercial_authorized": false,
  "auth_scene": "plugin",
  "device_scope": [
    { "vid": 4660, "pid": 22136 }
  ],
  "issued_at": 1710000000,
  "expires_at": 0,
  "key_id": "key_20260402_0001",
  "nonce": "5b6f04b8d2c544c0"
}
```

字段约束：

| 字段 | 类型 | 说明 |
|---|---|---|
| `ver` | `int` | 固定为 `1` |
| `tier` | `string` | `vip/svip/svip_plus` |
| `tier_code` | `int` | `0/1/2` |
| `license_mode` | `string` | `offline_api_key` 或 `online_api_key` |
| `host_auth_mode` | `string` | `none` 或 `activation_required`，缺省按 `none` 兼容旧 key |
| `commercial_authorized` | `bool` | Key 本身是否已完成商业授权；缺省为 `false`，离线正式态仍以 Host 激活为准 |
| `auth_scene` | `string` | 当前固定为 `plugin` |
| `device_scope` | `array` | 允许访问的设备 `vid/pid` 列表 |
| `issued_at` | `int64` | 签发时间 |
| `expires_at` | `int64` | 到期时间，`0` 表示不设到期 |
| `key_id` | `string` | 业务追踪 ID |
| `nonce` | `string` | 随机数，避免相同配置生成完全一致的 key |

### 4.5 加密与签名

当前实现采用以下规则：

- 对称加密：`AES-256-CBC`
- payload 二进制格式：`iv || ciphertext`
- payload 编码：`base64url`
- 完整性签名：`HMAC-SHA256`
- 签名截断：取前 16 字节，输出为 32 个十六进制字符
- 密钥派生：`HKDF-SHA256`

密钥材料只存在于 `sdk_private` 内部，不进入 `sdk_open`。

### 4.6 最终授权规则摘要

当前 Open SDK 授权分为离线 Key 与在线 Key：

- `offline_api_key`：Key 可本地解析。成者设备可使兼容购买等级直接进入 `offline_unlocked`，不需要 Host 授权码；没有可用成者设备时，`host_auth_mode=activation_required` 的 Key 未完成 Host 激活为 `offline_trial`，Host 激活后进入 `offline_unlocked`。离线正式态不要求 `commercial_authorized=true`。
- `online_api_key`：Key 先做本地格式校验，再以在线授权服务返回的 `commercialAuthorized` / `commercial_authorized` 判定正式态；字段缺失按未商业授权处理。可用成者设备也可使兼容购买等级进入正式态。
- 普通成者设备可自动激活 VIP/SVIP；只有设备配置带 `SpecialAuthorization=true` 的成者设备可自动激活 SVIP+。
- 非成者设备在线模式必须由服务端明确商务授权；离线模式可通过 Host 授权码正式化，且不要求商务授权。
- `device_scope` 为空表示不限制业务设备；非空时只允许其中 `{vid,pid}` 对应的目标设备执行业务操作。该白名单不影响已连接成者设备为 Key 提供自动正式化。
- `commercial_authorized` 会写入 ApiKey payload 和 `AuthContext`，用于表达 Key/服务端的商务授权事实；成者设备自动正式化不会把该字段伪造为 `true`。

### 4.7 在线商务授权与 trial 降级

在线 ApiKey 不要求本机 host 激活，但在线授权服务会返回 `commercialAuthorized` / `commercial_authorized`：

| 字段值 | 运行态行为 |
|---|---|
| `true` | `account_type` 使用服务端返回等级，`entitlement_state=online_validated` |
| `false` 或字段缺失 | 无可用兼容成者设备时，`account_type` 降级为 `trial`，`licensed_account_type` 保留购买等级，`entitlement_state=online_trial` |

成者设备可在服务端未返回商业授权时本地自动正式化：普通设备支持 VIP/SVIP，配置 `SpecialAuthorization=true` 的设备还支持 SVIP+。`commercial_authorized` 仍保持服务端商务授权事实。非成者设备的 `online_trial` 与离线 `offline_trial` 使用同一套权益策略：默认能力可直接使用；访问 VIP/SVIP 受限能力时消耗本机本地共享 `trial_vip` 试用额度，默认 200 次；SVIP+ 受限能力在未正式化时直接返回 `CapabilityNotAllowed`。
## 5. 离线模式

### 5.1 基本校验

`offline_api_key` 的校验分两层：

1. 本地格式、签名、解密、payload 结构校验
2. 本机 machine code 与隐藏授权文件中的宿主激活状态装配

本地校验通过后，`AuthContext` 会进入以下两种状态之一：

| `entitlement_state` | 含义 |
|---|---|
| `offline_limited` | 未解封，仅可按默认配额受限使用 |
| `offline_trial` | ApiKey 要求宿主激活，但当前机器尚未通过授权码激活；账号降级为 `trial` |
| `offline_unlocked` | 已通过本机授权码或兼容的成者设备自动正式化，不再受本地默认配额限制 |

`host_auth_mode` 规则：

| `host_auth_mode` | 行为 |
|---|---|
| `none` | 旧模式。ApiKey 可本地解析，未解封时进入 `offline_limited` |
| `activation_required` | 新模式。ApiKey 可分发，但未激活宿主只获得 `trial` 体验账号；激活后恢复到 ApiKey 的购买等级 |

### 5.2 统一权益与试用额度

权益统一由 `sdk_entitlement_policy` 管理。method 级默认开放 system/auth/device/video/capture/sane、barcode、image 默认处理能力和图片转单层 PDF/OFD；`image`、`ocr`、`file` 内部按参数映射最低等级：

- VIP：文字 OCR、自动旋转、显式双层 PDF/OFD、TXT。
- SVIP：白纸印章、空白页检测、统一规格、红绿头、曲面书籍展平、JPG/PNG/BMP/TIF 输出。
- SVIP+：古籍、孔洞填充预留、在线文档矫正/去手写/去底纹/去摩尔纹。

trial 账号访问高于默认等级且不超过 `licensed_account_type` 的 VIP/SVIP 能力时，会扣 `trial_vip` 共享额度；超过购买等级或在未正式化状态下访问 SVIP+ 能力时直接返回 `CapabilityNotAllowed`。离线 ApiKey 一旦通过 Host 授权码或兼容成者设备进入 `offline_unlocked`，按实际 `account_type` 授权，不再因 `commercial_authorized=false` 限制正式态能力。同一个非空 `request_id` 重试不重复扣本地额度。

### 5.3 machine code 与授权码

离线模式会复用 `src/module/authorized`：

- 通过 `AuthManager` 生成本机 machine code
- 通过 `ParseAuthCode` 解析用户输入的授权码
- 校验授权码是否具备 plugin 能力
- 校验授权码绑定的 machine code 是否与当前机器一致
- 校验授权码等级是否不低于 ApiKey 等级

只有以上 Host 激活条件全部满足，`auth.activate_offline` 才会成功；离线 ApiKey 的 `commercial_authorized` 字段不阻断 Host 激活。即使 ApiKey 未携带商业授权字段或字段为 `false`，只要本机授权码校验通过，也进入正式态。已被成者设备自动正式化的 Key 仍可执行该 API，以持久化 Host 授权。激活成功后，授权码会写入 `src/module/authorized` 维护的隐藏 lic 文件，并同步写入 `offline_entitlements.json` 中当前 ApiKey 的 host 激活摘要，本机 `AuthContext.entitlement_state` 进入 `offline_unlocked`；同一个 ApiKey 复制到其他机器时，如果没有兼容成者设备，仍会重新进入 `offline_trial`，直到提交绑定该机器 machine code 的授权码。

`czur_auth_app -x` 清除隐藏 lic 文件后，SDK 仍可通过 `offline_entitlements.json` 中当前 ApiKey 的 host 激活摘要恢复 Open SDK 的离线激活态；如需完全清除 Open SDK 本机激活状态，需要同时清理 SDK runtime 下对应 `offline_entitlements.json` 记录。

### 5.4 本地持久化

离线模式会把本地 quota 计数落盘到 SDK 运行目录：

- 默认目录：`~/.czur/sdk`
- 目录不可写时回退：`/tmp/.czur/sdk`
- 可通过环境变量覆盖：`CZUR_SDK_RUNTIME_DIR`
- quota 文件：`offline_entitlements.json`

文件中按 ApiKey 摘要维护本地 quota 记录，并保存当前 ApiKey 的 host 激活摘要（是否已激活、授权类型、授权等级）。它不保存完整授权码、machine code 或激活时间。

设备是否已经通过离线授权激活优先由 `src/module/authorized` 的隐藏 lic 文件确认；激活成功后也会写入 `offline_entitlements.json` 作为 Open SDK 本地恢复依据。

`CZUR_SDK_RUNTIME_DIR` 只影响 SDK quota/runtime 文件和日志位置，不影响底层隐藏 lic 文件位置。未显式设置 `CZUR_AUTH_LICENSE_DIR` 时，`sdk_private` 与同平台客户端共用 `AuthManager` 的默认隐藏授权文件；显式设置该变量时，`sdk_private` 使用指定目录，便于测试隔离、诊断和私有运行时部署。

### 5.5 离线默认配额

当前离线模式仅对三个能力桶做本地计数：

| bucket | methods | trial 默认限制 | 旧 `offline_limited` 默认限制 |
|---|---|---:|---:|
| `capture` | `capture.take` | 200 | 100 |
| `image` | `image.process` | 200 | 100 |
| `file` | `file.convert` | 200 | 100 |

`trial` 使用独立体验配额；旧 `offline_limited` 继续沿用当前实现的默认值，避免影响已有离线体验逻辑。后续可再按商业策略统一外提配置。

配额执行规则：

- `offline_limited` / `offline_trial`：本地扣减，超限后返回 `usage limit exceeded`
- `offline_unlocked`：该三类桶标记为 `unlocked`，不再做本地次数限制

## 6. 在线模式

### 6.1 基本思路

`online_api_key` 先做本地校验，再访问在线授权中心。

本地负责：

- 格式解析
- 签名校验
- 解密 payload
- 基础过期判断

在线服务负责：

- 最终授权放行
- 账号类型回传
- 在线配额与余量回传
- 每次能力消耗确认

### 6.2 环境变量

当前在线模式依赖以下环境变量：

| 环境变量 | 说明 |
|---|---|
| `CZUR_SDK_AUTHZ_BASE_URL` | 在线授权服务基地址，选填；未设置时由端侧传入的 Admin 配置或默认 `https://gateway-cn.czur.com` 决定 |
| `CZUR_SDK_ONLINE_AUTH_TIMEOUT_MS` | 超时毫秒数，选填，默认 `10000` |

### 6.3 当前接口

当前实现固定调用两个接口：

1. `POST <base>/api/v1/capeditor/open/authz/validate`
2. `POST <base>/api/v1/capeditor/open/authz/consume-confirm`

`/api/v1/capeditor/open/authz/validate` 请求体：

```json
{
  "apiKey": "sk-...",
  "capability": "image.enhance",
  "client": {
    "metadataVersion": "capeditor.open.authz.client.v1",
    "machineCode": "local-machine-code",
    "host": {
      "hostname": "pc-name",
      "os": {
        "sysname": "Linux",
        "release": "6.8.0",
        "version": "...",
        "machine": "x86_64",
        "id": "ubuntu",
        "versionId": "24.04",
        "prettyName": "Ubuntu 24.04 LTS"
      }
    },
    "devices": [
      {
        "deviceId": "device-id",
        "model": "ET24 Pro",
        "displayName": "CZUR ET24 Pro",
        "vid": 4660,
        "pid": 22136,
        "status": "online",
        "supportsVideo": true,
        "imageTransferProtocol": true
      }
    ]
  }
}
```

`client` 为可选上报信息，用于服务端记录在线授权校验发生时的本机与 CZUR 设备环境；客户端采集失败时可省略具体字段，不应影响 `apiKey` 与 `capability` 的授权校验语义。默认不上报设备序列号等更敏感字段。

`/authz/validate` 返回体至少需要覆盖：

- `code`
- `message`
- `data.allowed`
- `data.accountType`、`data.account_type` 或 `data.tier`
- `data.commercialAuthorized` 或 `data.commercial_authorized`，缺失时端侧按 `false` 降级为 `online_trial`

`/api/v1/capeditor/open/authz/consume-confirm` 请求体：

```json
{
  "apiKey": "sk-...",
  "capability": "doc_crop_enhance",
  "idempotencyKey": "image.enhance:image-enhance-1:doc_crop_enhance",
  "usageAmount": 1
}
```

`/authz/consume-confirm` 返回体至少需要覆盖：

- `code`
- `message`
- `data.allowed` 或 `data.accepted`

可选返回：

- `data.accountType`、`data.account_type` 或 `data.tier`
- `expires_at`
- `device_scope`
- `quota_buckets`
- `bucket`
- `limit`
- `remainingQuota` 或 `remaining`

### 6.4 当前限制

当前构建链路已启用 `cpp-httplib + OpenSSL`，在线授权支持 `http://` 和 `https://`。

## 7. Session Token

### 7.1 目标

session token 是短期会话凭证，设计目的如下：

- 首次建立会话时只使用 ApiKey
- 校验通过后签发短期 session token
- 后续 provider 可以基于 session token 做会话态能力校验
- 避免后续每个请求都重复携带长 ApiKey

### 7.2 格式

session token 固定前缀为：

```text
ss-v1-
```

完整格式如下：

```text
ss-v1-<payload><signature>
```

内部仍然采用：

- `AES-256-CBC`
- `HMAC-SHA256`
- `base64url`

### 7.3 session payload

当前 session payload 会包含以下关键信息：

- `ver`
- `tier`
- `tier_code`
- `auth_scene`
- `license_mode`
- `entitlement_state`
- `machine_code`
- `device_scope`
- `quota_buckets`
- `issued_at`
- `expires_at`
- `nonce`

### 7.4 TTL 与刷新

当前默认 session TTL 为 `7200` 秒。

支持两种刷新路径：

1. 基于 ApiKey 重新签发
   适用于首次授权、在线模式按当前原始 ApiKey 重新取最新授权态，以及离线模式按隐藏 lic 重新计算宿主激活态。
2. 基于已有 session token 轮换
   适用于不需要重新读取外部授权状态的本地续签场景。

## 8. AuthContext 与能力集

### 8.1 当前输出字段

`sdk_private` 产出的 `AuthContext` 当前包含：

- `is_valid`
- `account_type`
- `account_type_code`
- `licensed_account_type`
- `licensed_account_type_code`
- `auth_scene`
- `license_mode`
- `host_auth_mode`
- `entitlement_state`
- `machine_code`
- `device_scope`
- `expires_at`
- `capabilities`
- `quota_buckets`

其中：

- 在线模式通常把 `machine_code` 置空
- 离线模式会填入当前机器的 machine code
- `licensed_account_type` 表示 ApiKey 购买等级；未激活宿主上的 `account_type` 可为 `trial`
- `commercial_authorized` 表示 Key 或在线授权结果是否具备商业授权；离线成者设备自动正式化或 Host 激活成功后该字段可仍为 `false`
- `quota_buckets` 会用于结果展示和后续 provider 的决策

### 8.2 能力集映射

所有合法会话默认都包含：

- `system.ping`
- `system.info`
- `system.capabilities`
- `auth.create_session`
- `auth.get_context`
- `auth.refresh_session`
- `auth.destroy_session`
- `auth.activate_offline`

账号等级能力差异如下：

| account_type | 额外能力 |
|---|---|
| `trial` | 当前沿用 `vip` 能力集，但受本地体验配额约束 |
| `vip` | `device.list`、`device.get`、`device.open`、`device.close`、`capture.take`、`video.start`、`video.stop`、`video.set_format`、`image.process`、`file.convert` |
| `svip` | `vip` 全部能力，加 `ocr.recognize`、`ocr.get`、`ocr.cancel`、`ocr.extract_text`、`recognition.barcode_detect` |
| `svip_plus` | `svip` 全部能力 |

## 9. 与 `sdk_open` 的衔接

当前 `sdk_open` 指令通道已经接入这套授权逻辑，关键方法如下：

- `auth.create_session`
- `auth.get_context`
- `auth.refresh_session`
- `auth.activate_offline`
- `auth.destroy_session`

典型时序：

1. 前端或调用方先带 `api_key` 连接指令 WebSocket
2. `sdk_open` 通过 `ISdkAuthProvider` 校验 ApiKey
3. 校验成功后签发 `session_token`
4. 前端缓存 `session_token`
5. 后续 provider 或后续通道可选择基于 `session_token` 做会话态校验

更完整的对外时序说明见：

- `src/sdk_open/doc/COMMAND_CHANNEL_FLOW.md`
- `src/sdk_open/doc/COMMAND_CHANNEL_FLOW_ZH.md`

## 10. CLI 工具

当前 `sdk_private` 内置 `sdk_auth_tool`，用于开发与联调。

### 10.1 生成 ApiKey

```bash
sdk_auth_tool gen-apikey --mode offline|online --tier sp|sq|sr --device vid:pid[,vid:pid] [--host-auth-mode none|activation-required] [--expires-at ts] [--key-id id]
```

示例：

```bash
./sdk_auth_tool gen-apikey --mode offline --tier sq --device 4660:22136
```

需要宿主设备激活的离线 ApiKey：

```bash
./sdk_auth_tool gen-apikey --mode offline --tier sq --device 4660:22136 --host-auth-mode activation-required
```

### 10.2 解析 ApiKey

```bash
sdk_auth_tool parse-apikey --api-key <key>
```

### 10.3 生成 machine code

```bash
sdk_auth_tool gen-machine-code
```

该命令会初始化 `AuthManager`，并使用与离线解封相同的 machine code 生成逻辑。

## 10.4 C ABI / Java JNA 集成

除 CLI 外，`sdk_private` 还提供 C ABI 动态库，面向 Java 后端等非 C++ 服务集成。

目标：

- Java 后端不直接重写 ApiKey 加密、签名和 payload 规则
- Java 后端不直接绑定 C++ 类 ABI
- 通过稳定的 UTF-8 JSON 字符串协议调用 native 授权能力

C ABI 头文件：

```text
src/sdk_open/bindings/c/czur_sdk_private_c_api.h
```

导出函数：

```c
const char* czur_sdk_private_generate_api_key_json(const char* request_json);
const char* czur_sdk_private_parse_api_key_json(const char* request_json);
const char* czur_sdk_private_version_json(void);
void czur_sdk_private_free_string(const char* value);
```

调用约定：

- 所有输入和输出都是 UTF-8 JSON
- 返回字符串由 native 分配，调用方读取后必须调用 `czur_sdk_private_free_string`
- `code == 0` 表示成功，其他错误码沿用 SDK 错误码语义
- C ABI 内部只包装 `PrivateSdkAuthService`，不改变现有 ApiKey 生成与解析逻辑

构建目标：

```bash
cmake --build <build-dir> --target sdk_private_auth_c_api
```

Java/JNA 示例模块：

```text
src/sdk_open/bindings/java/czur-sdk-auth-jna
```

完整 Java 后端接入说明见：

```text
src/sdk_open/providers/private/JAVA_API_KEY_INTEGRATION_GUIDE.md
```

## 11. 构建与依赖

### 11.1 `module/authorized` 依赖关系

`sdk_private` 当前已经依赖 `src/module/authorized`，用途包括：

- machine code 生成
- 离线授权码解析
- 授权等级与 plugin 能力判断

这部分是当前设计的正式组成部分，不再是“仅参考实现”。

### 11.2 OpenSSL 1.1 / 3 双链路

当前默认链路仍然可以继续使用已有 OpenSSL 1.1 目标。

同时，`sdk_private` 已支持在不影响父项目默认链路的情况下，引入 OpenSSL 3 变体目标：

- 默认目标：`sdk_private_auth_core`、`sdk_private_providers`、`sdk_auth_tool`
- C ABI 目标：`sdk_private_auth_c_api`
- OpenSSL 3 变体：`sdk_private_auth_core_ossl3`、`sdk_private_providers_ossl3`、`sdk_auth_tool_ossl3`、`sdk_private_auth_c_api_ossl3`

如果希望默认 `sdk_private` 直接切换到 OpenSSL 3，可在 CMake 中打开：

```cmake
SDK_PRIVATE_USE_OPENSSL3=ON
```

对应依赖目录约定为：

- `third_party_repo/openssl`
- `third_party_repo/openssl3`

当前 Linux 下的 OpenSSL 3 运行时库会通过 `BUILD_RPATH` 指向 `third_party_repo/openssl3/lib/<arch>`。

## 12. 关键错误语义

当前鉴权链路中常见错误包括：

| code | 含义 |
|---|---|
| `1100` | `auth required` |
| `1101` | `token invalid` |
| `1102` | `token expired` |
| `1103` | `session token invalid` |
| `1106` | `auth scene mismatch` |
| `1108` | `offline auth code invalid` |
| `1109` | `online auth unavailable` |
| `1110` | `usage limit exceeded` |
| `1111` | `offline binding mismatch` |

实际错误文案可能会根据上下文返回更具体的信息，但这些错误码是对外判断的主依据。

## 13. 当前已知限制

- 离线默认配额目前均为硬编码：trial 为 `200`，旧 `offline_limited` 为 `100`
- OpenSSL 3 构建下，`module/authorized` 中遗留 MD5 用法会产生 deprecated warning，但不影响当前编译通过
- 目前仍为单主密钥模型，未做多 `kid` 轮换

## 14. 后续演进方向

后续建议沿以下方向演进：

1. 在线授权补齐 HTTPS 客户端能力。
2. 把离线和在线的额度策略外提为统一配置，而不是硬编码常量。
3. 为不同 provider 定义更稳定的 session 校验接入约定。
4. 逐步把旧授权模块中依赖 MD5 的实现迁移到更合适的现代算法。
