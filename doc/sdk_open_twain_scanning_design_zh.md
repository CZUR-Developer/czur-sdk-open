# SDK Open TWAIN 扫描独立页面与协议设计

> 状态：第一版实现说明，按当前 `src/module/twain` 与 `src/sdk_open` 代码同步
>
> 适用范围：`src/module/twain/`、`src/sdk_open/`、`src/sdk_open/frontend/demo-site/`
>
> 目标平台：Windows TWAIN；Linux SANE 继续使用现有 `sane.*` 能力域。

## 1. 背景

当前 Demo 站点已经将 SANE 扫描做成独立页面 `/sane-scanning`，并通过独立的 `sane.*` command WebSocket 方法访问后端能力。对应后端也已经有独立的 `SaneFacade`、`ISdkSaneProvider` 和 `PrivateSaneProvider`。

TWAIN 和 SANE 都属于通用扫描仪接入能力，但两者底层模型不同：

- SANE 是 Linux 扫描框架，核心流程是发现 backend、打开 device、读取/设置 option、调用 scan/read 获取页数据。
- TWAIN 是 Windows 扫描框架，核心流程是 DSM 状态机、Source 枚举、Capability 读写、Source Enable、XFERREADY 事件和图像传输。

因此首版 TWAIN 不强行并入 `sane.*`，而是采用独立页面和独立 `twain.*` 协议域。

## 2. 目标

1. 新增 Demo 独立页面 `/twain-scanning`，用于 Windows TWAIN 扫描调试和集成演示。
2. 新增独立 command WS 方法域 `twain.*`，覆盖设备发现、属性读写、扫描提交、任务查询和取消。
3. 在 `src/module/twain` 层承载真实 TWAIN 能力，包括 DSM 调用、Source 会话、Capability、扫描状态机和 32/64 位 helper 桥接。
4. 在 `sdk_open` 层保持开放协议和 typed DTO，只做 `twain.*` command 协议、鉴权、任务 asset URL、后处理转换和事件适配。
5. app 主业务后续接入时直接调用 `src/module/twain`，再通过 NativeAction/FileManager 做业务适配，不反向依赖 `sdk_open`，也不需要绕到 SDK Open WebSocket。
6. 保持字段命名尽量接近现有 `sane.*`，为后续抽象统一 `scanner.*` 能力域保留迁移空间。

## 3. 非目标

1. 首版不替换现有 `sane.*` 页面和协议。
2. 首版不实现跨平台统一 `scanner.*` 协议；该能力可在 SANE/TWAIN 都稳定后再抽象。
3. 首版不把外部 TWAIN DS 驱动工程整体并入 SDK Open；TWAIN 官方文档以当前工程 `docs/TWAIN-2.4-Specification.pdf` 为准。
4. 首版不要求实现所有 TWAIN capability，只覆盖常用扫描属性和可泛化展示的 capability。
5. 首版不承诺所有 TWAIN Source 都支持无 UI 扫描；不支持时返回明确状态。

## 4. 总体架构

```text
Demo Site
  /twain-scanning
    |
    | command WS: twain.*
    v
sdk_open/application
  CommandApplicationService
    |
    v
sdk_open/facade
  TwainFacade
    |
    v
sdk_open/interfaces
  ISdkTwainProvider + SdkTwain* DTO
    |
    +--> sdk_open/providers/mock
    |      MockTwainProvider
    |
    +--> sdk_open/providers/private_windows
           WindowsPrivateTwainProvider
             |
             v
           LoadLibrary("sdk_private_providers.dll")
             |
             v
           czur_sdk_private_twain_*_json C API
             |
             v
           src/sdk_open/providers/private
             |
             v
           src/module/twain
             ITwainProvider + Twain* DTO
             Windows TWAIN DSM + Source
             twain_helper_x86.exe / twain_helper_x64.exe
```

分层原则延续现有 SDK Open 架构：

- `transport` 只负责 WebSocket 收发。
- `application` 负责 method 路由、鉴权、参数校验、事件派发和 task asset URL 补全。
- `facade` 只暴露 typed 用例，不处理 JSON。
- `sdk_open/providers/mock` 封装 mock 能力。
- `sdk_open/providers/private_windows` 只做 `SdkTwain*` 与 `sdk_private_providers.dll` 导出的 TWAIN JSON C API 适配，不直接编译或链接真实 TWAIN 实现。
- `src/sdk_open/providers/private` 负责承接 private DLL 边界，把 C API JSON 转换为 `src/module/twain` 的 typed 调用。
- `src/module/twain` 承载真实 TWAIN 能力，可被 `sdk_private` 和 app 主业务共同调用；该层不依赖 `sdk_open`。
- `src/module/twain` 内部可以依赖 Windows/TWAIN 私有实现，但不能把 TWAIN/Windows 细节暴露到 command 协议和 facade DTO 之外。

## 5. Demo 页面设计

### 5.1 路由与导航

新增路由：

```text
/twain-scanning
```

导航项：

```text
TWAIN Scanning / TWAIN 扫描
```

页面仅在 Windows runtime 下展示可用状态。非 Windows 平台仍可打开页面，但顶部提示：

```text
TWAIN is only available on Windows runtimes.
TWAIN 仅在 Windows runtime 中可用。
```

### 5.2 页面区块

页面结构参考 `SaneScanningPage.vue`，但文案、事件和方法前缀改为 TWAIN。

1. Runtime 状态
   - 调用 `twain.status`
   - 展示 DSM 是否可加载、TWAIN 版本、平台、不可用原因。

2. Source 列表
   - 调用 `twain.list`
   - 展示 Source 名称、ProductName、Manufacturer、ProtocolMajor/Minor、状态。
   - 支持 refresh。

3. Source 会话
   - 选择 source 后调用 `twain.open`
   - 成功后得到 `session_id`
   - 支持 `twain.close`

4. Capability / 属性
   - 调用 `twain.get_capabilities`
   - 基础属性优先显示：
     - 分辨率
     - 色彩模式
     - 位深
     - 纸张来源 / feeder
     - 输出格式
     - 传输方式
     - 扫描页数
   - 高级能力以原始 capability 列表展示。
   - 修改属性调用 `twain.set_capabilities`。

5. Profile
   - 复用 SANE 的 profile 交互模型。
   - 方法前缀改为 `twain.profile_*`。
   - profile 内容保存为 capability patch。

6. 扫描任务
   - 调用 `twain.scan`
   - 返回 `task_id` 和 task snapshot。
   - 支持 `twain.scan_get` / `twain.scan_cancel`
   - 监听 `twain.scan_changed`
   - 展示 task status、phase、progress、page_count、output_paths、assets。

7. 请求/响应 JSON 面板
   - 和 SANE 页面一致，用于 Demo 调试。

## 6. 协议方法

所有方法都走现有 command WebSocket：

```text
ws://127.0.0.1:17090
```

请求/响应沿用统一结构：

```json
{
  "request_id": "req-001",
  "method": "twain.status",
  "params": {}
}
```

```json
{
  "request_id": "req-001",
  "code": 0,
  "message": "ok",
  "data": {},
  "ts": 1710000000
}
```

### 6.1 `twain.status`

用途：查询当前 runtime 是否支持 TWAIN。

请求：

```json
{
  "request_id": "req-twain-status-001",
  "method": "twain.status",
  "params": {}
}
```

响应：

```json
{
  "request_id": "req-twain-status-001",
  "code": 0,
  "message": "ok",
  "data": {
    "available": true,
    "platform": "windows",
    "supported_platforms": ["windows"],
    "dsm_loaded": true,
    "dsm_path": "C:\\Windows\\twain_32.dll",
    "twain_version": "2.4",
    "reason": ""
  },
  "ts": 1710000000
}
```

非 Windows 或 DSM 不可用时：

```json
{
  "code": 1710,
  "message": "TWAIN is only available on Windows",
  "data": {
    "available": false,
    "platform": "linux",
    "supported_platforms": ["windows"],
    "dsm_loaded": false,
    "reason": "unsupported platform"
  }
}
```

### 6.2 `twain.list`

用途：枚举 TWAIN Source。

TWAIN 后端通过 `DAT_IDENTITY / MSG_GETFIRST` 和 `DAT_IDENTITY / MSG_GETNEXT` 枚举 Source。

请求：

```json
{
  "request_id": "req-twain-list-001",
  "method": "twain.list",
  "params": {
    "refresh": true
  }
}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "generation": 3,
    "sources": [
      {
        "source_id": "twain:czur-shine",
        "source_name": "CZUR Shine TWAIN",
        "manufacturer": "CZUR",
        "product_family": "Scanner",
        "product_name": "CZUR Shine",
        "protocol_major": 2,
        "protocol_minor": 4,
        "status": "online",
        "openable": true
      }
    ]
  }
}
```

### 6.3 `twain.watch_start` / `twain.watch_stop`

用途：启动或停止 Source 列表变化监听。

TWAIN 本身没有和 SANE 完全一致的热插拔模型。首版建议 provider 内部定时刷新 Source 列表并做 diff，向订阅 connection 推送 `twain.source_snapshot`。

请求：

```json
{
  "method": "twain.watch_start",
  "params": {}
}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "watching": true,
    "generation": 3
  }
}
```

### 6.4 `twain.open`

用途：打开一个 TWAIN Source 会话。

后端动作：

1. 确保 DSM 已打开。
2. 根据 `source_id` 找到 `TW_IDENTITY`。
3. 调用 `DAT_IDENTITY / MSG_OPENDS`。
4. 保存 `session_id -> source identity/state`。

请求：

```json
{
  "request_id": "req-twain-open-001",
  "method": "twain.open",
  "params": {
    "source_id": "twain:czur-shine"
  }
}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "opened": true,
    "session_id": "twain-session-1",
    "source": {
      "source_id": "twain:czur-shine",
      "source_name": "CZUR Shine TWAIN",
      "product_name": "CZUR Shine",
      "status": "online"
    }
  }
}
```

### 6.5 `twain.close`

用途：关闭 TWAIN Source 会话。

请求：

```json
{
  "method": "twain.close",
  "params": {
    "session_id": "twain-session-1"
  }
}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "closed": true,
    "was_opened": true
  }
}
```

### 6.6 `twain.get_capabilities`

用途：读取当前 Source 支持的 capability。

后端动作：

1. 优先读取 `CAP_SUPPORTEDCAPS`。
2. 对可识别 capability 调用 `DAT_CAPABILITY / MSG_GET` 或 `MSG_GETCURRENT`。
3. 将 TWAIN container 统一转换成 SDK option。

请求：

```json
{
  "method": "twain.get_capabilities",
  "params": {
    "session_id": "twain-session-1"
  }
}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "capabilities": [
      {
        "cap": "ICAP_XRESOLUTION",
        "cap_id": 4376,
        "name": "resolution_x",
        "title": "X Resolution",
        "type": "fixed",
        "value": 300,
        "constraint": {
          "type": "range",
          "min": 75,
          "max": 600,
          "quant": 75
        },
        "readonly": false,
        "settable": true,
        "advanced": false
      },
      {
        "cap": "ICAP_PIXELTYPE",
        "cap_id": 257,
        "name": "pixel_type",
        "title": "Pixel Type",
        "type": "enum",
        "value": "rgb",
        "constraint": {
          "type": "list",
          "values": ["bw", "gray", "rgb"]
        },
        "readonly": false,
        "settable": true,
        "advanced": false
      }
    ]
  }
}
```

### 6.7 `twain.set_capabilities`

用途：批量设置 capability。

请求支持两种形式：

```json
{
  "method": "twain.set_capabilities",
  "params": {
    "session_id": "twain-session-1",
    "capabilities": {
      "ICAP_XRESOLUTION": 300,
      "ICAP_YRESOLUTION": 300,
      "ICAP_PIXELTYPE": "rgb"
    }
  }
}
```

或：

```json
{
  "method": "twain.set_capabilities",
  "params": {
    "session_id": "twain-session-1",
    "items": [
      { "cap": "ICAP_XRESOLUTION", "value": 300 },
      { "cap": "ICAP_PIXELTYPE", "value": "rgb" }
    ]
  }
}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "applied": true,
    "requires_reload": false,
    "results": [
      {
        "cap": "ICAP_XRESOLUTION",
        "status": "applied",
        "message": "ok",
        "value": 300
      }
    ]
  }
}
```

### 6.8 `twain.profile_list` / `twain.profile_save` / `twain.profile_apply` / `twain.profile_delete`

用途：保存和复用 capability 配置。

profile 存储在：

```text
<TWAIN_WORK_DIR>/twain/profiles.json
```

`TWAIN_WORK_DIR` 由 `src/module/twain` 统一解析：优先使用 `CZUR_SDK_RUNTIME_DIR`，兼容旧的 `SDK_OPEN_WORK_DIR`；Windows 默认落在 `%LOCALAPPDATA%\.czur\sdk`，Linux root 默认 `/var/lib/czur/sdk-open`，普通用户默认 `~/.czur/sdk`。

profile 结构：

```json
{
  "profile_id": "twain-profile-1",
  "source_key": "twain:czur-shine",
  "name": "300dpi color",
  "capabilities": [
    { "cap": "ICAP_UNITS", "value": "inches" },
    { "cap": "ICAP_XRESOLUTION", "value": 300 },
    { "cap": "ICAP_YRESOLUTION", "value": 300 },
    { "cap": "ICAP_PIXELTYPE", "value": "rgb" }
  ],
  "created_at": "1710000000",
  "updated_at": "1710000000"
}
```

规则：
- `capabilities` 推荐使用有序数组。TWAIN 官方 `DG_CONTROL / DAT_CAPABILITY / MSG_SET` 一次只设置一个 `TW_CAPABILITY`，SDK 收到数组后按顺序逐项下发。
- 部分 Source 会在设置一个 capability 后联动调整其它 capability 的当前值、默认值或可选值；保存 profile 时保留用户实际应用顺序，才能在后续 `profile_apply` 中复现驱动状态。
- 为兼容旧调用，`twain.set_capabilities` 和 `twain.profile_save` 仍可接受 object 形式，但 object 不表达设置顺序，不建议用于需要联动复现的配置。

### 6.9 `twain.scan`

用途：提交异步扫描任务。

请求：

```json
{
  "request_id": "req-twain-scan-001",
  "method": "twain.scan",
  "params": {
    "session_id": "twain-session-1",
    "show_ui": false,
    "output": {
      "type": "images",
      "path": "",
      "dir": "",
      "export_type": "single-page"
    }
  }
}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "accepted": true,
    "task_id": "twain-scan-1",
    "task": {
      "task_id": "twain-scan-1",
      "session_id": "twain-session-1",
      "status": "queued",
      "phase": "queued",
      "progress": 0,
      "page_count": 0,
      "output_type": "images",
      "output_format": "",
      "output_paths": [],
      "assets": [],
      "message": "queued"
    }
  }
}
```

当前参数规则：

| 参数 | 默认值 | 当前支持 | 说明 |
| --- | --- | --- | --- |
| `output.type` / `output_type` | `images` | `images`、`pdf`、`ofd`、`tiff` | command 层会归一化并校验；非法值在开始扫描前返回 `InvalidParams`，不会下沉到 provider。 |
| `output.export_type` / `export_type` | `multi-page` | `single-page`、`multi-page` | `images` 会强制为 `single-page`；`pdf/ofd/tiff` 支持逐页和多页合一。 |
| `output.path` / `output_path` | 空 | 目录路径 | 当前只接受目录语义，不支持带文件后缀的输出路径。 |
| `output.dir` / `output_dir` | 空 | 目录路径 | 与 `output.path` 一样按目录处理；嵌套 `output.*` 会覆盖同名顶层参数。 |
| `show_ui` | `false` | `true`、`false` | 是否显示 Source UI；若 `CAP_UICONTROLLABLE=false`，demo 会强制勾选。 |

`output.format` / `output_format` 不再作为 TWAIN 扫描任务参数使用，也不会传给 provider 覆盖驱动格式。图像页格式由驱动层 capability `ICAP_IMAGEFILEFORMAT` 和实际传输方式决定；文档目标格式由 `output.type` 决定。

输出路径规则：

- `D:`、`D:/`、`D://`、`D:\`、`D:\\` 这类 Windows 盘符根目录会统一归一化为根目录。
- 根目录只检查是否存在，不尝试创建；不存在时在开始扫描前返回错误。
- 带子目录时兼容 `/` 和 `\` 混用；子目录不存在才递归创建。
- `output.path` 和 `output.dir` 都只表示目录。若最后一段看起来是带后缀的文件名，例如 `D:/scan.pdf` 或 `D:/twain-output.jpg`，会在创建阶段返回非法路径错误。

输出规则：

- `output.type=images`：逐页输出图片，最终 task 的 `output_paths/assets` 展示每页图片；具体页格式由 TWAIN capability `ICAP_XFERMECH` / `ICAP_IMAGEFILEFORMAT` 及 provider 实际传输结果决定。
- `output.type=pdf/ofd/tiff`：先输出 BMP 中间页，再复用当前 `file.convert` 流程封装成目标文档；多页合一时输出单个文档，逐页输出时输出多个文档。即使当前 `ICAP_XFERMECH=file`，文档目标也不会把 `ICAP_IMAGEFILEFORMAT` 的文件页直接当作最终文档。
- 扫描任务不再通过 `transfer.mechanism` 或 `output.format` 覆盖驱动层设置；`ICAP_XFERMECH`、`ICAP_IMAGEFILEFORMAT` 等参数应先通过 `twain.set_capabilities` 修改。
- `native` 传输得到 DIB/BMP 中间页；`memory` 传输当前按未压缩 raster 写 BMP 中间页；`file` 传输在 `output.type=images` 场景下会尽量让 Source 直接输出 `ICAP_IMAGEFILEFORMAT` 当前格式。
- `show_ui=false` 为默认值；Source 不支持无 UI 时返回明确错误或 `ui_required=true`，可改用 `show_ui=true` 走驱动 UI。

### 6.10 `twain.scan_get`

请求：

```json
{
  "method": "twain.scan_get",
  "params": {
    "task_id": "twain-scan-1"
  }
}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "task_id": "twain-scan-1",
    "task": {
      "status": "completed",
      "phase": "completed",
      "progress": 100,
      "page_count": 2,
      "output_paths": [
        "D:\\SDK\\work\\twain\\twain-scan-1\\assets\\page-001.jpg",
        "D:\\SDK\\work\\twain\\twain-scan-1\\assets\\page-002.jpg"
      ],
      "assets": [
        {
          "asset_id": "asset-twain-page-001",
          "kind": "twain_scan_page",
          "path": "D:\\SDK\\work\\twain\\twain-scan-1\\assets\\page-001.jpg",
          "content_type": "image/jpeg",
          "url": "/api/assets/twain-scan-1/asset-twain-page-001",
          "download_url": "/api/assets/twain-scan-1/asset-twain-page-001/download"
        }
      ]
    }
  }
}
```

### 6.11 `twain.scan_cancel`

用途：请求取消扫描任务。

TWAIN 后端应：

1. 标记 task cancel requested。
2. 调用 Source disable 或 pending transfer reset。
3. 必要时关闭 Source 会话。

请求：

```json
{
  "method": "twain.scan_cancel",
  "params": {
    "task_id": "twain-scan-1"
  }
}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "accepted": true,
    "task_id": "twain-scan-1",
    "task": {
      "status": "cancelled",
      "phase": "cancelled",
      "progress": 100,
      "cancel_requested": true
    }
  }
}
```

## 7. 事件

### 7.1 Source 列表事件

```json
{
  "event": "twain.source_snapshot",
  "code": 0,
  "message": "ok",
  "payload": {
    "generation": 4,
    "sources": [],
    "added_sources": [],
    "removed_sources": []
  },
  "ts": 1710000000
}
```

### 7.2 扫描任务事件

```json
{
  "event": "twain.scan_changed",
  "code": 0,
  "message": "ok",
  "payload": {
    "task": {
      "task_id": "twain-scan-1",
      "status": "running",
      "phase": "transferring",
      "progress": 50,
      "current_page": 1,
      "page_count": 1,
      "last_page_path": "D:\\SDK\\work\\twain\\twain-scan-1\\assets\\page-001.jpg"
    }
  },
  "ts": 1710000001
}
```

### 7.3 可选 UI 事件

如果 Source 要求 UI 或用户关闭 Source UI，可推送：

```text
twain.ui_required
twain.ui_opened
twain.ui_closed
twain.transfer_ready
```

首版 Demo 可只消费 `twain.scan_changed` 和 `twain.source_snapshot`。

## 8. Provider 接口设计

### 8.1 module/twain 公共接口

真实 TWAIN 能力位于 `src/module/twain`，对外暴露中立接口 `editor::module::twain::ITwainProvider` 和 `Twain*` DTO。该接口不依赖 `sdk_open`，便于后续 app 主业务复用：

```cpp
class ITwainProvider {
public:
    virtual ~ITwainProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual void SetSourceEventSink(TwainSourceEventCallback) {}
    virtual void SetScanTaskEventSink(TwainScanTaskEventCallback) {}
    virtual TwainStatusResult GetStatus() = 0;
    virtual TwainListResult ListSources(const TwainListRequest& request) = 0;
    virtual TwainWatchResult WatchStart(const TwainWatchRequest& request) = 0;
    virtual TwainWatchResult WatchStop(const TwainWatchRequest& request) = 0;
    virtual TwainOpenResult OpenSource(const TwainOpenRequest& request) = 0;
    virtual TwainCloseResult CloseSource(const TwainCloseRequest& request) = 0;
    virtual TwainGetCapabilitiesResult GetCapabilities(const TwainGetCapabilitiesRequest& request) = 0;
    virtual TwainSetCapabilitiesResult SetCapabilities(const TwainSetCapabilitiesRequest& request) = 0;
    virtual TwainProfileListResult ListProfiles(const TwainProfileRequest& request) = 0;
    virtual TwainProfileResult SaveProfile(const TwainProfileRequest& request) = 0;
    virtual TwainProfileResult ApplyProfile(const TwainProfileRequest& request) = 0;
    virtual TwainProfileResult DeleteProfile(const TwainProfileRequest& request) = 0;
    virtual TwainScanResult Scan(const TwainScanRequest& request) = 0;
    virtual TwainScanResult GetScan(const TwainScanGetRequest& request) = 0;
    virtual TwainScanResult CancelScan(const TwainScanCancelRequest& request) = 0;
};
```

Windows 下通过：

```cpp
std::shared_ptr<ITwainProvider> CreateWindowsTwainProvider();
```

创建真实 provider。非 Windows 构建当前只编译中立运行时文件，不编译 Windows TWAIN provider。

### 8.2 sdk_open 适配接口

`sdk_open` 新增 `ISdkTwainProvider`，只用于开放 SDK 协议适配：

```cpp
class ISdkTwainProvider {
public:
    virtual ~ISdkTwainProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual void SetSourceEventSink(SdkTwainSourceEventCallback) {}
    virtual void SetScanTaskEventSink(SdkTwainScanTaskEventCallback) {}
    virtual SdkTwainStatusResult GetStatus() = 0;
    virtual SdkTwainListResult ListSources(const SdkTwainListRequest& request) = 0;
    virtual SdkTwainWatchResult WatchStart(const SdkTwainWatchRequest& request) = 0;
    virtual SdkTwainWatchResult WatchStop(const SdkTwainWatchRequest& request) = 0;
    virtual SdkTwainOpenResult OpenSource(const SdkTwainOpenRequest& request) = 0;
    virtual SdkTwainCloseResult CloseSource(const SdkTwainCloseRequest& request) = 0;
    virtual SdkTwainGetCapabilitiesResult GetCapabilities(const SdkTwainGetCapabilitiesRequest& request) = 0;
    virtual SdkTwainSetCapabilitiesResult SetCapabilities(const SdkTwainSetCapabilitiesRequest& request) = 0;
    virtual SdkTwainProfileListResult ListProfiles(const SdkTwainProfileRequest& request) = 0;
    virtual SdkTwainProfileResult SaveProfile(const SdkTwainProfileRequest& request) = 0;
    virtual SdkTwainProfileResult ApplyProfile(const SdkTwainProfileRequest& request) = 0;
    virtual SdkTwainProfileResult DeleteProfile(const SdkTwainProfileRequest& request) = 0;
    virtual SdkTwainScanResult Scan(const SdkTwainScanRequest& request) = 0;
    virtual SdkTwainScanResult GetScan(const SdkTwainScanGetRequest& request) = 0;
    virtual SdkTwainScanResult CancelScan(const SdkTwainScanCancelRequest& request) = 0;
};
```

`ProviderBundle` 新增：

```cpp
std::shared_ptr<ISdkTwainProvider> twain_provider;
```

新增 `TwainFacade`，模式与 `SaneFacade` 一致。缺少 provider 或平台不支持时返回 `TwainNotAvailable`。

Windows private provider 当前实现为 `WindowsPrivateTwainProvider`：

```text
ISdkTwainProvider
  -> WindowsPrivateTwainProvider
      -> LoadLibrary("sdk_private_providers.dll")
      -> czur_sdk_private_twain_*_json C API
      -> src/sdk_open/providers/private
      -> editor::module::twain::ITwainProvider
```

这样真实 TWAIN 能力下沉在 `src/module/twain`，并通过 `src/sdk_open/providers/private` 的 DLL 边界暴露给 SDK Open。`sdk_open` 不直接链接 `twain_dev`，只承担 `twain.*` 协议、DTO 转换、事件转发和运行时文件复制。

## 9. TWAIN 后端实现

### 9.1 线程与窗口

TWAIN Source 往往依赖 Windows 消息循环。`src/module/twain` 的 Windows provider 创建专用 worker thread：

- 在线程内创建隐藏窗口或 message-only window。
- 在线程内打开 DSM 和 Source。
- 该线程负责 `MSG_PROCESSEVENT`、`MSG_XFERREADY`、Source UI 事件。
- `ITwainProvider` 对外方法通过任务队列投递到 TWAIN worker，避免跨线程直接操作 DSM/Source。

### 9.2 状态机映射

内部维护 session state：

```text
unloaded
  -> dsm_open
  -> source_open
  -> source_enabled
  -> transfer_ready
  -> transferring
  -> source_enabled/source_open
  -> closed
```

常用 TWAIN 调用映射：

| SDK 动作 | TWAIN 调用 |
| --- | --- |
| `twain.list` | `DG_CONTROL / DAT_IDENTITY / MSG_GETFIRST` + `MSG_GETNEXT` |
| `twain.open` | `DG_CONTROL / DAT_IDENTITY / MSG_OPENDS` |
| `twain.close` | `DG_CONTROL / DAT_IDENTITY / MSG_CLOSEDS` |
| `twain.get_capabilities` | `DG_CONTROL / DAT_CAPABILITY / MSG_GET` |
| `twain.set_capabilities` | `DG_CONTROL / DAT_CAPABILITY / MSG_SET` |
| `twain.scan` | `DG_CONTROL / DAT_USERINTERFACE / MSG_ENABLEDS` |
| transfer ready | `DG_CONTROL / DAT_EVENT / MSG_PROCESSEVENT` 得到 `MSG_XFERREADY` |
| native transfer | `DG_IMAGE / DAT_IMAGENATIVEXFER / MSG_GET` |
| memory transfer | `DG_IMAGE / DAT_IMAGEMEMXFER / MSG_GET` |
| file transfer | `DG_IMAGE / DAT_IMAGEFILEXFER / MSG_GET` |
| end transfer | `DG_CONTROL / DAT_PENDINGXFERS / MSG_ENDXFER` |
| cancel/reset | `DG_CONTROL / DAT_PENDINGXFERS / MSG_RESET` 或 disable source |

### 9.3 传输方式

当前实现支持三种传输方式，扫描时以 Source 当前 `ICAP_XFERMECH` capability 为准；未能读取时按 `native` 兜底：

1. `native`
   - 调用 `DAT_IMAGENATIVEXFER`
   - 得到 DIB handle
   - 保存 BMP 中间页，再按 `output.type` 进入后续转换或直接作为结果

2. `file`
   - 调用 `DAT_SETUPFILEXFER` + `DAT_IMAGEFILEXFER`
   - `output.type=images` 时优先使用 `DAT_SETUPFILEXFER.Format` 让 Source 直接写出 `ICAP_IMAGEFILEFORMAT` 当前格式
   - `output.type=pdf/ofd/tiff` 时仍以 BMP 中间页进入 sdk_open finalize，避免把临时页当作最终文档

3. `memory`
   - 调用 `DAT_SETUPMEMXFER` + `DAT_IMAGEMEMXFER`
   - 当前按未压缩 raster 写 BMP 中间页
   - 会在传输前尽量设置 `ICAP_COMPRESSION=none`，但不把 `ICAP_COMPRESSION` 作为通用可展示 capability

`ICAP_IMAGEFILEFORMAT` 是驱动层文件传输格式设置。`file + output.type=images` 时 provider 会读取该 capability 的当前值，并传给 `DAT_SETUPFILEXFER.Format`；若 Source 不支持设置某个格式，应在 `twain.set_capabilities` 阶段返回失败或在扫描传输阶段失败。`pdf/ofd/tiff` 目标始终走 SDK Open 的后处理封装，不把 `ICAP_IMAGEFILEFORMAT` 解释为业务最终格式。

## 10. Capability 映射

当前 `twain.get_capabilities` 会尝试读取以下基础 capability；Source 不支持或当前状态不可读时跳过，不作为失败返回：

| SDK name | TWAIN capability | 说明 |
| --- | --- | --- |
| `supported_caps` | `CAP_SUPPORTEDCAPS` | Source 声明支持的 capability 列表，只读数组 |
| `device_online` | `CAP_DEVICEONLINE` | 设备在线状态，只读 |
| `ui_controllable` | `CAP_UICONTROLLABLE` | 是否支持无 UI 扫描，只读 |
| `enable_ds_ui_only` | `CAP_ENABLEDSUIONLY` | 是否支持仅打开 Source UI |
| `resolution_x` | `ICAP_XRESOLUTION` | X DPI |
| `resolution_y` | `ICAP_YRESOLUTION` | Y DPI |
| `native_resolution_x` | `ICAP_XNATIVERESOLUTION` | X 方向光学原生分辨率，只读 |
| `native_resolution_y` | `ICAP_YNATIVERESOLUTION` | Y 方向光学原生分辨率，只读 |
| `pixel_type` | `ICAP_PIXELTYPE` | 官方 `TWPT_*` 值域，例如 `bw` / `gray` / `rgb` |
| `bit_depth` | `ICAP_BITDEPTH` | 1 / 8 / 24 |
| `compression` | `ICAP_COMPRESSION` | 官方 `TWCP_*` 压缩方式 |
| `xfer_count` | `CAP_XFERCOUNT` | 扫描页数，`-1` 表示全部 |
| `feeder_enabled` | `CAP_FEEDERENABLED` | ADF 开关 |
| `feeder_loaded` | `CAP_FEEDERLOADED` | ADF 是否有纸，只读 |
| `paper_detectable` | `CAP_PAPERDETECTABLE` | 是否可检测纸张，只读 |
| `auto_feed` | `CAP_AUTOFEED` | ADF 自动进纸 |
| `indicators` | `CAP_INDICATORS` | 是否允许 Source 显示进度/提示 |
| `duplex` | `CAP_DUPLEX` | 双面能力模式，只读/枚举 |
| `duplex_enabled` | `CAP_DUPLEXENABLED` | 双面扫描开关 |
| `image_file_format` | `ICAP_IMAGEFILEFORMAT` | 文件传输格式 |
| `xfer_mech` | `ICAP_XFERMECH` | native / file / memory |
| `units` | `ICAP_UNITS` | 单位 |
| `bit_order` | `ICAP_BITORDER` | 位序 |
| `pixel_flavor` | `ICAP_PIXELFLAVOR` | 0 值像素语义 |
| `planar_chunky` | `ICAP_PLANARCHUNKY` | chunky / planar |
| `supported_sizes` | `ICAP_SUPPORTEDSIZES` | 纸张尺寸枚举 |
| `orientation` | `ICAP_ORIENTATION` | 扫描方向 |
| `physical_width` | `ICAP_PHYSICALWIDTH` | 物理扫描宽度，只读 |
| `physical_height` | `ICAP_PHYSICALHEIGHT` | 物理扫描高度，只读 |
| `max_frames` | `ICAP_MAXFRAMES` | 最大 frame 数 |
| `brightness` | `ICAP_BRIGHTNESS` | 亮度 |
| `contrast` | `ICAP_CONTRAST` | 对比度 |

说明：

- capability 读取按 Source 实际支持情况返回，不保证每个 Source 都包含完整列表。
- `ICAP_IMAGEFILEFORMAT` 会返回 Source 声明的文件格式能力；扫描任务自身不再提供 `output.format` 来覆盖该驱动层设置。
- `ICAP_XRESOLUTION` / `ICAP_YRESOLUTION` 官方类型是 `TW_FIX32` 数值；如果 Source 返回 `TW_ENUMERATION`，SDK 会以 `type=fixed + constraint.type=list` 表示离散 DPI 列表，而不是把 DPI 当作 TWAIN 常量枚举。
- `ICAP_IMAGEFILEFORMAT` 的 TWAIN capability ID 是 `0x110C`；`0x1112` 是 `ICAP_PHYSICALHEIGHT`，不能混用。

container 映射：

| TWAIN container | SDK constraint |
| --- | --- |
| `TWON_ONEVALUE` | `type=none`，当前值 |
| `TWON_ENUMERATION` | `type=list`，含 values/default/current |
| `TWON_RANGE` | `type=range`，含 min/max/quant |
| `TWON_ARRAY` | `type=array`，只读数组；例如 `CAP_SUPPORTEDCAPS` |

## 11. 错误码建议

现有 SANE 使用 `1700-1705`。TWAIN 建议新增 `1710-1716`：

| code | 名称 | 说明 |
| ---: | --- | --- |
| `1710` | `TwainNotAvailable` | 当前平台或 DSM 不可用 |
| `1711` | `TwainSourceNotFound` | Source 不存在 |
| `1712` | `TwainSourceBusy` | Source 或 session 正忙 |
| `1713` | `TwainCapabilityInvalid` | capability 不存在、不可设置或值非法 |
| `1714` | `TwainScanFailed` | 扫描或图像传输失败 |
| `1715` | `TwainProfileNotFound` | profile 不存在 |
| `1716` | `TwainUiRequired` | Source 要求 UI 扫描 |

所有错误响应仍使用统一 command WS response 结构。`message` 记录 TWRC/TWCC 摘要，`data` 可附带：

```json
{
  "twrc": "TWRC_FAILURE",
  "twcc": "TWCC_BADVALUE",
  "operation": "DAT_CAPABILITY/MSG_SET"
}
```

## 12. 鉴权与能力

新增 capability：

```text
twain.status
twain.list
twain.watch_start
twain.watch_stop
twain.open
twain.close
twain.get_capabilities
twain.set_capabilities
twain.profile_list
twain.profile_save
twain.profile_apply
twain.profile_delete
twain.scan
twain.scan_get
twain.scan_cancel
```

首版规则：

- `twain.status` 可以登录后访问，也可按产品需要放开为匿名诊断。
- 其他 `twain.*` 默认要求当前 command connection 已绑定合法 session。
- `twain.scan` 可消费与 `sane.scan` 类似的扫描 quota，若当前授权体系还没有独立 bucket，可先挂到 `capture` 或新增 `scanner.scan` bucket。
- TWAIN Source 不是 CZUR USB 摄像头，不应复用现有 VID/PID device scope 判断；如需限制，应新增 source allowlist 或 capability 维度控制。

## 13. 与 SANE 的关系

首版关系：

```text
Linux:   /sane-scanning  -> sane.*
Windows: /twain-scanning -> twain.*
```

字段尽量保持相似：

| SANE | TWAIN | 说明 |
| --- | --- | --- |
| `device_id` | `source_id` | TWAIN 使用 Source 概念 |
| `device_name` | `source_name` | 展示名 |
| `option` | `capability` | 底层命名不同 |
| `session_id` | `session_id` | 保持一致 |
| `scan task` | `scan task` | 保持一致 |
| `assets` | `assets` | 保持一致 |

后续若抽象 `scanner.*`：

- `scanner.list` 可以聚合 `sane.list` / `twain.list`。
- `scanner.open` 根据 `backend` 或 id 前缀分发。
- `scanner.options` 内部映射 SANE option 与 TWAIN capability。

## 14. 实施步骤

1. `src/module/twain`
   - 新增 `twain_types.h`，定义中立 `Twain*` DTO、`TwainStatusCode` 和 `ITwainProvider`
   - 新增 `twain_runtime.h/.cpp`，提供 JSON、路径、工作目录和 TWAIN logger 等 module 层基础能力
   - Windows 下实现真实 TWAIN provider，包含 DSM 调用、Source session、capability 映射和 `native/file/memory` transfer
   - Windows 下支持同位数直连和异位数 helper 桥接；非 Windows 仅编译中立运行时，避免引入 Windows API

2. `sdk_open/interfaces`
   - 新增 `i_sdk_twain_provider.h`
   - 新增或扩展 `sdk_provider_types.h` 中的 `SdkTwain*` DTO
   - 新增 TWAIN 错误码

3. `sdk_open/facade`
   - 新增 `twain_facade.h/.cpp`
   - `ProviderBundle` 增加 `twain_provider`

4. `sdk_open/application`
   - 注册 `twain.*` methods
   - 新增 JSON parse/build helper
   - 新增 source watch connection 管理
   - 新增 scan task asset URL 补全和事件派发

5. `sdk_open/providers/mock`
   - 新增 `MockTwainProvider`
   - 支持完整 mock source、capability、profile、scan task

6. `sdk_open/providers/private_windows`
   - 新增 `WindowsPrivateTwainProvider`
   - 只负责 `SdkTwain*` 与 `sdk_private_providers.dll` 的 TWAIN JSON C API 转换
   - 通过 `LoadLibrary`/`GetProcAddress` 访问 `czur_sdk_private_twain_*_json`
   - 不直接 include `src/module/twain`，不直接链接 `twain_dev`
   - 非 Windows 不编译 Windows TWAIN adapter，由 facade/provider 返回 unavailable

7. `src/sdk_open/providers/private`
   - 导出 `czur_sdk_private_twain_*_json` C API
   - 负责 JSON 与 `module::twain::Twain*` DTO 的转换
   - 持有共享 Windows TWAIN provider，并承接 source/scan 事件 callback
   - Windows 下链接 `twain_dev`；非 Windows 返回 `TwainNotAvailable`

8. Windows 32/64 位 Source 桥接
   - 主进程按自身位数直接访问同位数 TWAIN Source
   - 位数不匹配时通过 `twain_helper_x86.exe` / `twain_helper_x64.exe` 访问对应位数 Source
   - 主进程和 helper 使用 Windows Named Pipe 做 IPC
   - IPC 请求/响应 payload 仍保持一行一个 JSON 对象，即 helper 内部 Named Pipe 的 JSON line 帧格式；这不是对外 command WS 的新协议，也不要求业务前端额外轮询。
   - helper 构建目标使用最终名 `twain_helper`，不同架构直接输出 `twain_helper_x86.exe` / `twain_helper_x64.exe`
   - CMake 通过 `TWAIN_HELPER_X86_EXE` / `TWAIN_HELPER_X64_EXE` 指定要随主构建拷贝的异位数 helper；这些变量是构建输入，不是 `src/module/twain` 的运行时接口概念。

9. Demo 页面
   - 新增 `TwainScanningPage.vue`
   - 新增路由、导航、i18n 文案
   - 复用 SANE 页面的 UI 布局，但改为 source/capability 语义

## 15. 构建参数

### 15.1 Windows SDK Open private 模式

private 模式用于真实 TWAIN Source。`sdk_open_app` 只编译开放层协议和 private DLL adapter；真实 TWAIN provider 由 `sdk_private_providers.dll` 承载，helper 由 `src/module/twain` 生成。

如需同时支持 32 位和 64 位 Source，先构建异位数 helper，再构建主程序。以下示例为 x64 `sdk_open_app` 携带 x86 helper：

```powershell
cmake -S . -B cmake-build-twain_helper_x86 `
  -G "Visual Studio 17 2022" `
  -A Win32 `
  -DBUILD_SDK_OPEN=ON `
  -DBUILD_SDK_WEB=OFF `
  -DSDK_OPEN_ENABLE_HTTP_SERVER=OFF `
  -DBUILD_SDK_PRIVATE=OFF `
  -DSDK_OPEN_USE_PRIVATE_PROVIDER=OFF

cmake --build cmake-build-twain_helper_x86 `
  --config Release `
  --target twain_helper `
  -- /m:4

$x86Helper = Join-Path $PWD "cmake-build-twain_helper_x86\src\module\twain\Release\twain_helper_x86.exe"

cmake -S . -B cmake-build-open_sdk_windows `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DBUILD_SDK_OPEN=ON `
  -DBUILD_SDK_WEB=ON `
  -DSDK_OPEN_ENABLE_HTTP_SERVER=ON `
  -DBUILD_SDK_PRIVATE=ON `
  -DSDK_OPEN_USE_PRIVATE_PROVIDER=ON `
  -DTWAIN_HELPER_X86_EXE="$x86Helper"

cmake --build cmake-build-open_sdk_windows `
  --config Release `
  --target twain_helper sdk_private_providers sdk_open_app sdk_auth_tool `
  -- /m:4
```

构建后，`sdk_open_app` 运行目录需要包含：

- `sdk_open_app.exe`
- `sdk_private_providers.dll`
- `twain_helper_x64.exe`
- `twain_helper_x86.exe`，当需要访问 32 位 Source 时

### 15.2 Windows SDK Open mock 模式

mock 模式用于开放层协议、Demo 页面和自动化链路验证，不访问真实 TWAIN DSM/Source，不需要 `sdk_private_providers.dll`，也不需要 `twain_helper_x86.exe` / `twain_helper_x64.exe`。

如果仍需同时生成 `sdk_auth_tool`，可以保留 `BUILD_SDK_PRIVATE=ON`，但必须关闭 `SDK_OPEN_USE_PRIVATE_PROVIDER`：

```powershell
cmake -S . -B cmake-build-open_sdk_windows_mock `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DBUILD_SDK_OPEN=ON `
  -DBUILD_SDK_WEB=ON `
  -DSDK_OPEN_ENABLE_HTTP_SERVER=ON `
  -DBUILD_SDK_PRIVATE=ON `
  -DSDK_OPEN_USE_PRIVATE_PROVIDER=OFF

cmake --build cmake-build-open_sdk_windows_mock `
  --config Release `
  --target sdk_open_app sdk_auth_tool `
  -- /m:4
```

只验证开放层后端、不构建 Demo Web 资源时，可把 `BUILD_SDK_WEB` 改为 `OFF`。

## 16. 验收场景

Mock provider：

1. `/twain-scanning` 可以打开。
2. `twain.status` 返回可用 mock 状态。
3. `twain.list` 返回至少一个 mock source。
4. `twain.open` 返回 `session_id`。
5. `twain.get_capabilities` 返回基础 capability。
6. 修改 capability 后 `twain.set_capabilities` 返回 applied。
7. `twain.scan` 返回 task，随后收到 `twain.scan_changed`。
8. `twain.scan_get` 返回 completed task 和可下载 asset。
9. `twain.scan_cancel` 可取消 running task。

Windows 真机：

1. 未安装 DSM 或非 Windows 环境，`twain.status` 返回 unavailable，页面显示明确提示。
2. 安装 TWAIN Source 后，`twain.list` 能枚举 Source。
3. 打开 Source 后能读取常用 capability。
4. 设置 300 DPI、RGB 后扫描成功。
5. 分别验证 `native`、`file`、`memory` transfer 的扫描结果。
6. 分别验证 `images/pdf/ofd/tiff` 与 `single-page/multi-page` 的输出组合；`images` 期望强制逐页输出。
7. Source 要求 UI 时，返回 `TwainUiRequired` 或可选打开 UI 扫描。
8. 扫描失败时 response/event 包含 TWRC/TWCC 和 operation。
9. 任务结束后 Source/session 状态可继续扫描或正常关闭。

回归：

1. 现有 `/sane-scanning` 和 `sane.*` 行为不变。
2. 非 Windows 构建不因 TWAIN 头文件或 Windows API 失败。
3. `sdk_open_app` mock provider 模式不依赖私有 TWAIN 实现。

## 17. 风险与处理

| 风险 | 处理 |
| --- | --- |
| TWAIN Source 依赖 UI 和消息循环 | provider 内部使用专用 worker thread + hidden window |
| 不同 Source capability 行为差异大 | 基础 capability 做强映射，高级 capability 原样透传 |
| 无 UI 扫描兼容性不稳定 | 默认无 UI，失败时返回 `TwainUiRequired`，允许 `show_ui=true` |
| 传输方式兼容性差异 | 当前实现支持 `native/file/memory`，每种方式按 Source 实际能力返回明确错误或完成状态 |
| Windows 32/64 位 DSM/Source 兼容性 | 同位数 Source 由主进程处理；异位数 Source 通过对应 helper 进程和 Windows Named Pipe 桥接 |
| 输出路径歧义 | `output.path/output.dir` 统一按目录处理，带后缀路径在 command 层拒绝 |
| 输出类型歧义 | `output.type` 仅支持 `images/pdf/ofd/tiff`，非法值在 command 层拒绝 |
| 扫描取消后 Source 状态不稳定 | cancel 后必要时 disable/close source，并在 task message 中记录恢复策略 |

## 18. 设计结论

首版 TWAIN 采用独立页面 `/twain-scanning` 和独立 `twain.*` 协议域最合适。它符合当前 SANE 页面已有模式，也能隔离 TWAIN 的 Windows/DSM/Source 状态机复杂度。

同时，TWAIN DTO 应尽量复用 SANE 的会话、任务、asset 和 profile 形态。这样短期能快速落地，长期也能平滑抽象到统一的 `scanner.*` 能力域。

GBrain：已查询，命中 SDK Open SANE 作为 Linux 专属能力域、private provider 进程内管理 SANE 的背景；本设计与当前仓库实现一致，并将 TWAIN 作为 Windows 独立能力域引入。
