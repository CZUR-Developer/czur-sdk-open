# Java 后端 ApiKey 生成集成指南

本文面向 Java 后端服务，说明如何通过 `sdk_private` 的 C ABI 动态库生成和解析 Open SDK ApiKey。

## 1. 推荐架构

Java 后端不要直接重写 ApiKey 加密、签名、payload 规则，也不要直接绑定 C++ 类。推荐边界如下：

```text
Java REST Controller
  -> Java ApiKeyIssuer service
  -> JNA
  -> libsdk_private_auth_c_api.so
  -> PrivateSdkAuthService
  -> PrivateSdkKeyCodec
```

这样可以保持三点稳定：

- ApiKey 格式只由 `sdk_private` 维护。
- Java 侧只处理业务权限、项目归属和明文 key 的一次性返回。
- C++ 侧后续更换内部实现时，不破坏 Java 调用接口。

## 2. Native 构建

在本仓库中打开 SDK 私有构建：

```bash
cmake -S . -B cmake-build-sdk-debug -GNinja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_SDK_OPEN=ON \
  -DBUILD_SDK_PRIVATE=ON

cmake --build cmake-build-sdk-debug --target sdk_private_auth_c_api
```

Linux Debug 构建产物默认位于：

```text
cmake-build-sdk-debug/Debug/libsdk_private_auth_c_api.so
```

默认情况下，Java 后端和 SDK 服务会使用 C++ 内置生产 master secret。测试、联调或私有部署需要覆盖默认值时，必须在 JVM 启动前配置：

```bash
export CZUR_SDK_MASTER_SECRET='<production-secret>'
```

`<production-secret>` 是覆盖默认值的 ApiKey 主密钥材料。`sdk_private` 会用当前生效的 master secret 派生 ApiKey 和 session token 的加密密钥、签名密钥。它不是客户 license、不是项目 ApiKey，也不能下发给前端或 Open SDK 调用方。

要求：

- Java 后端签发 ApiKey、SDK 服务解析 ApiKey 时必须使用同一套 master secret；覆盖 `CZUR_SDK_MASTER_SECRET` 时两端必须一致。
- 未覆盖 `CZUR_SDK_MASTER_SECRET` 时使用 C++ 内置默认 master secret。
- 当前仍是单 master secret 模型；直接更换会导致旧 ApiKey 无法解析，除非后续增加 `kid` 或多 secret 轮换。

## 3. C ABI

C ABI 头文件：

```text
src/sdk_open/bindings/c/czur_sdk_private_c_api.h
```

导出函数：

```c
const char* czur_sdk_private_generate_api_key_json(const char* request_json);
const char* czur_sdk_private_parse_api_key_json(const char* request_json);
const char* czur_sdk_private_generate_auth_code_json(const char* request_json);
const char* czur_sdk_private_version_json(void);
void czur_sdk_private_free_string(const char* value);
```

约定：

- 入参和返回值都是 UTF-8 JSON。
- 返回字符串由 native 分配，Java/JNA 读取后必须调用 `czur_sdk_private_free_string`。
- C ABI 内部会捕获 C++ 异常，异常不会穿透 C 边界。
- `code == 0` 表示成功；其他错误码沿用 SDK 错误码语义。

## 4. 生成 ApiKey 请求

```json
{
  "tier": "svip",
  "license_mode": "offline_api_key",
  "host_auth_mode": "activation_required",
  "commercial_authorized": false,
  "device_scope": [
    { "vid": 4660, "pid": 22136 }
  ],
  "issued_at": 0,
  "expires_at": 0,
  "key_id": "key_project_123_0001"
}
```

字段说明：

| 字段 | 说明 |
|---|---|
| `tier` | 支持 `vip`、`svip`、`svip_plus`，也兼容 `sr`、`sq`、`sp` |
| `license_mode` | 支持 `offline_api_key`、`online_api_key`，也兼容 `offline`、`online` |
| `host_auth_mode` | 宿主设备授权模式；推荐项目 ApiKey 使用 `activation_required` |
| `commercial_authorized` | Key 本身是否已完成商业授权；缺省为 `false`，离线正式态可由兼容成者设备自动获得，或由 Host 授权码获得 |
| `device_scope` | CZUR 设备白名单，按 `{vid, pid}` 约束 |
| `issued_at` | Unix 秒；传 `0` 时 native 使用当前时间 |
| `expires_at` | Unix 秒；`0` 表示不过期 |
| `key_id` | Java 后端生成的业务追踪 ID |

成功响应：

```json
{
  "code": 0,
  "message": "ok",
  "api_key": "sk-sq-v1-...",
  "key_id": "key_project_123_0001",
  "tier": "trial",
  "tier_code": 4,
  "licensed_account_type": "svip",
  "licensed_account_type_code": 1,
  "license_mode": "offline_api_key",
  "host_auth_mode": "activation_required",
  "entitlement_state": "offline_trial",
  "commercial_authorized": false,
  "issued_at": 1710000000,
  "expires_at": 0,
  "device_scope": [
    { "vid": 4660, "pid": 22136 }
  ]
}
```

## 5. Java JNA 模块

示例模块位于：

```text
src/sdk_open/bindings/java/czur-sdk-auth-jna
```

Java 后端可直接复制该模块，或把其中代码迁入自己的后端工程。

加载 native 库的推荐方式：

```bash
java \
  -Dczur.sdk.auth.library.path=/opt/czur-sdk/libsdk_private_auth_c_api.so \
  -jar backend.jar
```

也可以使用环境变量：

```bash
export CZUR_SDK_AUTH_LIBRARY_PATH=/opt/czur-sdk/libsdk_private_auth_c_api.so
```

Java 调用示例：

```java
CzurSdkApiKeyIssuer issuer = CzurSdkApiKeyIssuer.createDefault();

GenerateApiKeyRequest request = new GenerateApiKeyRequest();
request.setTier("svip");
request.setLicenseMode("offline_api_key");
request.setHostAuthMode("activation_required");
// 可选：表达 Key 本身商业授权事实。离线 Key 可由兼容成者设备自动正式化，或由 Host 授权码正式化。
request.setCommercialAuthorized(false);
request.setKeyId("key_project_123_0001");
request.setExpiresAt(0L);
request.setDeviceScope(List.of(new DeviceGrant(4660, 22136)));

GenerateApiKeyResponse response = issuer.generate(request);
String apiKey = response.getApiKey();
```

`commercialAuthorized` 会写入 ApiKey payload 并在解析响应中原样返回。服务端只有在订单/套餐/项目已经完成商业授权时才应设置为 `true`；旧 Key 或未传该字段会按 `false` 解析。离线 Key 即使为 `false`，兼容成者设备自动正式化或 Host 激活成功后仍进入 `offline_unlocked`；在线 Key 则以在线授权服务返回的 `commercialAuthorized` 和本地成者设备自动正式化共同决定运行态。

生成离线宿主授权码：

```java
CzurSdkApiKeyIssuer issuer = CzurSdkApiKeyIssuer.createDefault();

GenerateAuthCodeRequest request =
    new GenerateAuthCodeRequest("customer-machine-code", "20261231", 13);

GenerateAuthCodeResponse response = issuer.generateAuthCode(request);
String authCode = response.getAuthCode();
```

对应 C ABI 请求：

```json
{
  "machine_code": "customer-machine-code",
  "expires_date": "20261231",
  "auth_type": 13
}
```

说明：

- `license_key` 不对 Java/C ABI 暴露，native 内部固定使用 SDK 私有授权密钥。
- `expires_date` 使用 `YYYYMMDD`，沿用 `src/module/authorized` 的授权码格式。
- `auth_type` 使用底层数字值，但只接受 plugin-only 或 both 类型，避免生成无法被 `auth.activate_offline` 消费的 client-only 授权码。

## 6. REST API 建议

Java 后端对外建议提供：

```http
POST /api/projects/{projectId}/api-keys
```

请求体：

```json
{
  "tier": "svip",
  "licenseMode": "offline",
  "hostAuthMode": "activation_required",
  "deviceScope": [
    { "vid": 4660, "pid": 22136 }
  ],
  "expiresAt": 0
}
```

后端职责：

- 校验操作者是否有项目权限。
- 校验项目是否允许创建对应 tier。
- 生成全局唯一 `key_id`。
- 调用 `CzurSdkApiKeyIssuer.generate`。
- 只在创建响应中返回一次明文 ApiKey。
- 数据库只保存 `key_id`、ApiKey hash、项目 ID、购买 tier、license mode、host auth mode、设备白名单、过期时间、创建人和创建时间。

## 7. 离线宿主设备授权

离线 ApiKey 支持两层设备概念：

- `device_scope`：CZUR 扫描仪白名单，按 `{vid, pid}` 控制可访问的硬件型号。
- `host_auth_mode`：运行 SDK 的宿主电脑授权模式，按本机 machine code 激活。

推荐 Java 后端创建项目 ApiKey 时使用：

```json
{
  "license_mode": "offline_api_key",
  "host_auth_mode": "activation_required"
}
```

该模式下，同一个 ApiKey 被复制到未激活电脑时，不会直接获得购买等级能力：

- 未激活宿主：`tier=trial`，`licensed_account_type=<购买等级>`，`entitlement_state=offline_trial`
- 激活后宿主：`tier=<购买等级>`，`entitlement_state=offline_unlocked`

离线 ApiKey 在有兼容成者设备时可直接自动进入正式态，不需要 Host 授权码；没有该设备时，正式态由 Host 授权码决定。`commercial_authorized` 字段会原样进入解析上下文，但即使缺失或为 `false`，Host 激活成功后也进入 `offline_unlocked`。在线 ApiKey 在非成者设备上仍必须由在线授权服务明确返回商业授权；成者设备可按其可用等级自动正式化。

宿主激活状态会写入 `src/module/authorized` 的隐藏 lic 文件，并同步在 SDK 运行目录下的 `offline_entitlements.json` 保存当前 ApiKey 的 host 激活摘要；该摘要只用于 Open SDK 本地恢复，不保存完整授权码或 machine code。

trial 账号独立于 `vip/svip/svip_plus`：默认能力直接可用；访问 VIP/SVIP 受限能力时，按本机共享 `trial_vip` 额度默认 200 次试用。在线 Key 在服务端返回 `commercialAuthorized=false` 或缺失该字段、且没有兼容成者设备时降级为 `online_trial`；普通成者设备可自动激活 VIP/SVIP，配置 `SpecialAuthorization=true` 的成者设备还可自动激活 SVIP+。离线 Key 在没有兼容成者设备且未 Host 激活时是 `offline_trial`；成者自动正式化或 Host 激活后按 ApiKey 等级进入正式态。自动正式化不会将 `commercial_authorized` 改写为 `true`。`licensed_account_type` 保留购买等级。

## 8. 安全要求

- 生产环境默认使用 C++ 内置 master secret；测试、联调或私有部署覆盖 `CZUR_SDK_MASTER_SECRET` 时，签发端和解析端必须一致。
- 日志中不得打印完整 `api_key`，最多打印前 8 位和后 4 位。
- 数据库不保存明文 ApiKey。
- 当前仍是单 master secret 模型；后续如果要轮换密钥，需要先设计 `kid` 或多 secret 兼容策略。
- `device_scope` 为空表示不限制业务设备；只有非空 scope 才按 `{vid,pid}` 白名单限制目标设备。它不限制已连接成者设备对 Key 自动正式化的判定。

## 9. 验证

Native smoke test：

```bash
cmake --build cmake-build-sdk-debug --target sdk_private_c_api_smoke_test
cmake-build-sdk-debug/Debug/sdk_private_c_api_smoke_test
```

CLI 互操作验证：

```bash
cmake-build-sdk-debug/Debug/sdk_auth_tool parse-apikey --api-key '<generated-api-key>'
```
