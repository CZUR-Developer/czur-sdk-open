# SDK Open 拍照采集任务流设计

> 状态：功能设计
>
> 适用范围：`src/sdk_open/`、`src/sdk_open/providers/private/`
>
> 本文档描述拍照采集后端的模块拆分、协议模型、任务流编排和 provider 边界。本文档不包含代码实现。

## 1. 目标

拍照采集是 SDK 的核心图像输入能力。首版目标是把一次采集拆成可观测、可扩展的异步任务流：

1. 拍照，得到原始图片，部分设备还会得到辅助激光图。
2. 根据 Profile 做纸张处理，例如单页、曲面、区域。
3. 根据 Profile 做色彩模式处理，例如自动优化、黑白、彩色、白纸印章。
4. 根据 Profile 做目标格式转换，例如 `jpg`、`png`、`tiff`。
5. 按 Profile 配置生成缩略图，并返回当次采集文件列表。

核心边界：

- 任务流编排放在 `sdk_open`，作为可开源能力。
- 真实设备拍照、图像算法、格式转换能力放在 `sdk_private` provider 中实现。
- `sdk_open/providers/mock` 提供完整 mock provider，保证开源项目可独立运行和演示。
- 首版采用异步任务模型：`capture.take` 返回任务 ID，`capture.get` 查询结果，command WS 推送任务事件。

## 2. 架构拆分

### 2.1 sdk_open 侧

`sdk_open` 增加采集应用服务和编排能力：

- `CaptureTaskService`
  - 管理任务生命周期。
  - 维护 `task_id -> task state`。
  - 支持 `capture.take` 创建任务、`capture.get` 查询任务和 `capture.session.get` 查询当前连接统计。
  - 将物理采集和后续处理拆成两个阶段：原图成功后立即进入按设备隔离的处理 FIFO。
  - 负责把任务事件推送到 command WS。

- `CapturePipelineService`
  - 根据 Profile 构建 CGraph DAG。
  - 编排拍照、纸张处理、色彩处理、格式转换、缩略图和结果汇总。
  - 聚合阶段结果、warning 和最终 assets。

- `CaptureFacade`
  - 对应用层暴露 typed 用例。
  - 不处理 JSON。
  - 不依赖 `sdk_private` 头文件。

- `interfaces/`
  - 扩展采集 Profile、任务状态、资产和阶段结果 DTO。
  - 扩展 provider 抽象，让 private/mock provider 能按阶段提供能力。

### 2.2 sdk_private 侧

`sdk_private` 负责真实能力接入：

- `PrivateDeviceProvider`
  - 实现真实拍照。
  - 输出原始图片路径和可选激光图路径。
  - 复用现有 `DeviceManager`、`DeviceCommand::takePhoto()`、still image callback 和视频截帧能力。

- `PrivateGraphicProvider`
  - 实现纸张处理。
  - 实现色彩模式处理。
  - 实现缩略图生成。
  - 对接现有图像算法能力，例如单页裁切、曲面处理、激光图处理、`algoConvertColor`。

- `PrivateFileConvertProvider` 或扩展现有 `PrivateOfdProvider`
  - 首版建议在公共接口中抽象为格式转换能力，不绑定 OFD 命名。
  - 私有实现可复用现有 TIFF、图片写出和导出能力。

### 2.3 mock 侧

`sdk_open/providers/mock` 需要提供完整 mock 链路：

- `CaptureStill` 生成 mock 原图文件路径。
- 纸张处理返回 mock page processed 文件。
- 色彩处理返回 mock color processed 文件。
- 格式转换返回 mock final 文件。
- 缩略图生成返回 mock thumbnail 文件。

mock provider 必须覆盖 fallback 场景，方便 demo-site 和自动化测试验证任务流。

## 3. CGraph 引入策略

采集任务流直接引入 CGraph 作为编排执行器。

CGraph 是 C++ DAG/流水线任务编排库，支持 C++11、无第三方依赖，适合当前 `sdk_open` 的 C++11 工程约束。源码地址：

- <https://github.com/ChunelFeng/CGraph>

落地方式：

- vendored 到 `src/sdk_open/third_party/CGraph`。
- 保留上游 LICENSE 和版本说明。
- CMake 中新增 `sdk_open_cgraph` interface target。
- `sdk_open_runtime` 链接 `sdk_open_cgraph`。
- `sdk_open` 自己封装 `CapturePipelineService`，业务代码不直接暴露 CGraph 类型到 public provider interface。

设计约束：

- CGraph 是执行后端，不是协议模型。
- Profile、Task、Stage、Asset 等公共 DTO 仍由 `sdk_open/interfaces` 定义。
- 如果后续需要替换执行器，只替换 `CapturePipelineService` 内部实现。

## 4. 协议模型

### 4.1 capture.take

`capture.take` 从同步完成改为异步提交。

请求：

```json
{
  "request_id": "req-capture-001",
  "method": "capture.take",
  "params": {
    "device_id": "device-001",
    "profile": {
      "profile_version": "capture.profile.v1",
      "revision": 1,
      "device": {
        "device_id": "device-001",
        "resolution": {
          "width": 1280,
          "height": 720,
          "fps": 15
        }
      },
      "capture": {
        "page_processing": "single_page",
        "single_page": {
          "realtime_detect_rects": true,
          "crop_border": {
            "enabled": false,
            "width": 0,
            "height": 0
          },
          "id_card_round_corner": false,
          "auto_rotate": false,
          "smart_black_edge_optimize": true,
          "multi_target_paging": false
        },
        "curved_book": {
          "remove_finger": {
            "enabled": true,
            "finger_type": "with_sleeve"
          },
          "smart_paging": true,
          "crop_border": {
            "enabled": false,
            "width": 0,
            "height": 0
          },
          "auto_complete": false
        },
        "color_mode": "auto_optimize"
      },
      "output": {
        "format": "jpg",
        "quality": 90,
        "target_size": 1600,
        "thumbnails": {
          "original": true,
          "page_processed": true,
          "color_processed": false
        }
      }
    },
    "output_dir": "",
    "include_base64": false
  }
}
```

成功响应：

```json
{
  "request_id": "req-capture-001",
  "code": 0,
  "message": "ok",
  "data": {
    "accepted": true,
    "task_id": "cap-20260428-000001",
    "status": "queued"
  },
  "ts": 1777372800
}
```

规则：

- `device_id` 必填。
- `profile` 必填。
- `profile.profile_version` 首版固定为 `capture.profile.v1`。
- `output_dir` 可选；为空时由 runtime 选择默认采集输出目录。
- `include_base64` 首版只影响最终 response/query 是否带 base64，不影响内部文件流。
- `capture.take` 通过采集准入并成功登记任务后消耗 quota，一次任务只消耗一次；参数错误、限频、物理采集中和登记失败不扣减配额。

### 4.2 capture.get

新增 `capture.get` 查询异步任务。

请求：

```json
{
  "request_id": "req-capture-get-001",
  "method": "capture.get",
  "params": {
    "task_id": "cap-20260428-000001"
  }
}
```

响应：

```json
{
  "request_id": "req-capture-get-001",
  "code": 0,
  "message": "ok",
  "data": {
    "task_id": "cap-20260428-000001",
    "status": "succeeded",
    "device_id": "device-001",
    "profile_revision": 1,
    "stages": [
      {
        "name": "capture_raw",
        "status": "succeeded",
        "input": [],
        "output": ["asset-original"],
        "provider": "czur-device-provider",
        "message": "ok"
      }
    ],
    "assets": [
      {
        "asset_id": "asset-original",
        "kind": "original",
        "path": "/tmp/sdk-open/capture/original.jpg",
        "content_type": "image/jpeg",
        "width": 0,
        "height": 0,
        "size": 0
      }
    ],
    "warnings": []
  },
  "ts": 1777372801
}
```

规则：

- `capture.get` 要求当前 command 连接已授权。
- `capture.get` 不消耗 quota。
- 首版只允许查询当前连接创建的任务。
- 任务状态枚举：`queued`、`running`、`succeeded`、`failed`。
- 首版不设计 `capture.cancel`。

### 4.3 command WS 事件

command WS 需要支持按 `connection_id` 主动发送事件。

事件格式沿用现有 `BuildWsEvent`：

```json
{
  "event": "capture.stage.updated",
  "code": 0,
  "message": "ok",
  "payload": {
    "task_id": "cap-20260428-000001",
    "status": "running",
    "stage": {
      "name": "page_process",
      "status": "running"
    }
  },
  "ts": 1777372801
}
```

事件：

- `capture.started`
- `capture.stage.updated`
- `capture.completed`
- `capture.failed`
- `capture.session.updated`

## 5. Profile 与内部枚举

Profile 首版使用 demo-site 当前 JSON 结构，后端不直接依赖前端类型。

页面处理枚举：

| Profile 值 | 内部含义 | 私有侧参考 |
| --- | --- | --- |
| `single_page` | 单页 | `SinglePage` |
| `curved_book` | 曲面书籍 | `CurvedBooks` |
| `selected_area` | 区域 | `SelectArea` |

纸张处理扩展参数：

| Profile 路径 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `capture.single_page.realtime_detect_rects` | bool | `false` | 视频流是否返回实时识别框；关闭后后端不在每帧做目标检测。 |
| `capture.single_page.crop_border.enabled` | bool | `false` | 单页裁边开关。 |
| `capture.single_page.crop_border.width` | int | `0` | 单页横向裁边参数，范围 `-100..100`。 |
| `capture.single_page.crop_border.height` | int | `0` | 单页纵向裁边参数，范围 `-100..100`。 |
| `capture.single_page.id_card_round_corner` | bool | `false` | 证件圆角留白。 |
| `capture.single_page.auto_rotate` | bool | `false` | 页面自动转正。 |
| `capture.single_page.smart_black_edge_optimize` | bool | `true` | 智能优化黑边。 |
| `capture.single_page.multi_target_paging` | bool | `false` | 多目标自动分页；开启后可输出多页资产。 |
| `capture.curved_book.remove_finger.enabled` | bool | `true` | 曲面书籍清除手指开关。 |
| `capture.curved_book.remove_finger.finger_type` | string | `with_sleeve` | `with_sleeve` 为带指套，`without_sleeve` 为不带指套。 |
| `capture.curved_book.smart_paging` | bool | `true` | 是否左右分页；关闭时输出整张展平图。 |
| `capture.curved_book.crop_border.enabled` | bool | `false` | 曲面书籍裁边开关。 |
| `capture.curved_book.crop_border.width` | int | `0` | 曲面书籍横向裁边参数，范围 `-100..100`。 |
| `capture.curved_book.crop_border.height` | int | `0` | 曲面书籍纵向裁边参数，范围 `-100..100`。 |
| `capture.curved_book.auto_complete` | bool | `false` | 页面自动补全。 |

`video.set_profile` 可运行时更新 `capture.single_page.realtime_detect_rects` 和 `capture.single_page.multi_target_paging`，不需要重新打开视频流。

色彩模式枚举：

| Profile 值 | 内部含义 | 私有侧参考 |
| --- | --- | --- |
| `auto_optimize` | 自动优化 | `AutoOpti` |
| `black_white` | 黑白 | `BlackWhite` |
| `color` | 彩色增强 | `ColorEnhance` / `FILTER_COLOR_DEFAULT` |
| `white_paper_seal` | 白纸印章 | `WhitePaper` |

私有侧兼容历史参数别名：

- `none`、`original`、`NoOpti`、`FILTER_NOTHING`：不执行色彩算法，透传上一阶段结果。
- `grayscale`、`GrayScale`、`FILTER_GRAY_DEFAULT`：灰度增强。
- 未知值：按客户端既有逻辑回退为 `FILTER_COLOR_DEFAULT`，并记录告警日志。

输出格式：

- `jpg`
- `png`
- `tiff`

输出压缩与目标大小：

- `quality`：可选，仅对 `jpg` / `jpeg` 最终输出生效，取值范围 `1-100`。不传时沿用 provider 默认质量。
- `target_size`：可选，使用客户端同款“万像素档位”，例如 `500`、`1600`。
- `target_size` 不表示把结果图强制拉伸到该档位的绝对宽高。SDK 会先把档位映射为目标拍照尺寸，再按 `min(target_width / original_width, target_height / original_height)` 计算缩放比例，对页面处理、色彩处理后的最终结果图等比缩放。
- 仅 `device.open` 的成功响应会在设备 JSON 中返回 `capture_output` 能力；`device.list` / `device.get` 不暴露该结构字段。
- `capture_output.target_size_supported=true` 时，`target_size` 必须属于 `capture_output.target_sizes` 中声明的数字档位，否则返回 `InvalidParams`。
- `capture_output.target_size_supported=false` 或旧 provider 未返回该能力时，采集 profile 中的 `target_size` 会被忽略。
- `capture_output.target_sizes` 来自设备配置 `ResolutionRange` 中可映射的数字档位，会跳过 `自定义`；Open SDK 对外不支持自定义目标大小。
- 视频通道传图类可通过 `target_size` 控制最终输出大小；截帧类仍通过打开设备时指定分辨率控制原始帧尺寸，不传 `target_size` 时不额外缩放。

`device.open` 成功响应中的目标大小能力示例：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "device_id": "device-001",
    "opened": true,
    "capture_output": {
      "target_size_supported": true,
      "target_sizes": [
        { "target_size": 500, "width": 2592, "height": 1944 },
        { "target_size": 1600, "width": 4608, "height": 3456, "is_device_default": true }
      ]
    }
  }
}
```

缩略图：

- `original`：原始图缩略图。
- `page_processed`：纸张处理结果缩略图。
- `color_processed`：色彩模式结果缩略图。

## 6. 任务流 DAG

首版 CGraph DAG：

```text
CaptureRaw
  ├─ OriginalThumbnail
  └─ PageProcess
       ├─ PageProcessedThumbnail
       └─ ColorMode
            ├─ ColorProcessedThumbnail
            └─ FormatConvert
                 └─ FinalizeResult
```

节点职责：

- `CaptureRaw`
  - 调用 `ISdkDeviceProvider::CaptureStill`。
  - 输出原始图 asset。
  - 可选输出激光图 asset。

- `OriginalThumbnail`
  - 当 Profile 开启 `output.thumbnails.original` 时执行。
  - 输入原始图。
  - 失败只记录 warning。

- `PageProcess`
  - 根据 `capture.page_processing` 调用 provider。
  - 无 provider 或 unsupported 时，输出沿用原始图。

- `PageProcessedThumbnail`
  - 当 Profile 开启 `output.thumbnails.page_processed` 时执行。
  - 输入纸张处理结果。
  - 失败只记录 warning。

- `ColorMode`
  - 根据 `capture.color_mode` 调用 provider。
  - 无 provider 或 unsupported 时，输出沿用纸张处理结果。

- `ColorProcessedThumbnail`
  - 当 Profile 开启 `output.thumbnails.color_processed` 时执行。
  - 输入色彩处理结果。
  - 失败只记录 warning。

- `FormatConvert`
  - 根据 `output.format` 做目标格式转换。
  - 如果输入已经是目标格式，可以直接沿用。
  - 如果需要转换但无 provider，任务失败。

- `FinalizeResult`
  - 汇总最终文件、阶段结果、缩略图、warning。
  - 更新任务状态为 `succeeded` 或 `failed`。

## 7. Provider 接口设计

### 7.1 设备 provider

扩展 `SdkCaptureRequest`：

- `device_id`
- `output_dir`
- `include_base64`
- `timeout_ms`

扩展 `SdkCaptureResult`：

- `captured`
- `original_path`
- `laser_path`
- `content_type`
- `width`
- `height`
- `size`
- `payload`

规则：

- `CaptureStill` 只负责拿到原始采集结果，不做纸张处理和色彩处理。
- 设备未打开时返回 `DeviceNotOpen`。
- 设备断开、拍照超时或没有收到 still image 时返回 `CaptureFailed`。

### 7.2 图像 provider

首版建议把现有粗粒度 `Process` 保留，同时新增阶段化接口：

- `ProcessPage`
  - 输入：原始图、可选激光图、页面处理模式、输出路径。
  - 输出：纸张处理结果。

- `ApplyColorMode`
  - 输入：上一阶段图片、色彩模式、输出路径。
  - 输出：色彩处理结果。

- `GenerateThumbnail`
  - 输入：源图片、缩略图类型、输出路径。
  - 输出：缩略图结果。

- `ConvertImageFormat`
  - 输入：源图片、目标格式、输出路径。
  - 输出：最终格式文件。

如果后续希望保持能力边界更清晰，也可以把 `ConvertImageFormat` 拆到独立 `ISdkFileConvertProvider`。首版为减少 provider 数量，可先放在图像 provider 中。

## 8. fallback 与失败策略

硬失败：

- 授权失败。
- capability 不允许。
- device scope 不允许。
- `device_id` 为空或设备不存在。
- 设备未打开。
- 拍照失败。
- 格式转换必需但 provider 不可用。
- CGraph 执行异常。

软 fallback：

- 纸张处理 provider 不存在或返回 unsupported：使用原始图作为纸张处理结果。
- 色彩模式 provider 不存在或返回 unsupported：使用纸张处理结果作为色彩处理结果。
- 缩略图生成失败：任务继续完成，记录 warning。

任务失败时：

- 任务状态设为 `failed`。
- 保留已完成阶段和已生成 assets。
- 推送 `capture.failed`。
- `capture.get` 返回失败原因。

## 9. 状态与并发

任务状态字段：

- `task_id`
- `connection_id`
- `device_id`
- `status`
- `capture_source`
- `acquisition_status`
- `processing_status`
- `profile_revision`
- `created_at`
- `updated_at`
- `stages`
- `assets`
- `warnings`
- `error`

并发策略：

- 每个 device 仅在真实物理采集动作进行时短暂占用；该状态优先返回 `DeviceBusy`，不会被后续处理占用。
- 同一 device 固定 1500ms 采集准入窗口，窗口内的手动拍照和硬拍返回 `RateLimited (1004)` 与 `retry_after_ms`。
- 原图成功后按原图完成顺序进入该 device 的处理 FIFO；纸张处理、色彩、格式、缩略图和后置增强均在该 FIFO 内执行。
- 不同 device 的处理可并行；同一 device 的处理严格 FIFO。关闭设备不等待已入队任务处理完成。
- `capture.session.get` 按当前 Command WS connection 返回 `captured_count`、`processed_count`、`failed_count`、`pending_count` 和按设备明细；每次统计变化以 `capture.session.updated` 携带完整汇总推送。
- 同一 connection 可以有多个历史任务。
- task store 使用内存存储。
- 首版任务完成后保留最近 N 条，默认 N=50。
- command 连接断开后，已运行任务可以继续完成，但事件不再发送；任务结果可按实现策略保留到 TTL 到期。

## 10. 与现有代码的关系

当前实现状态：

- `CommandApplicationService::HandleCaptureTake` 仍是同步调用 `DeviceFacade::CaptureStill`。
- `PrivateDeviceProvider::CaptureStill` 尚未实现真实拍照。
- `ISdkGraphicProvider::Process` 只有粗粒度 `input_path/output_path`。
- command WS 当前只按请求响应发送消息，尚不支持应用层主动按连接推送事件。

后续实现顺序建议：

1. 引入 CGraph third_party 和 CMake target。
2. 扩展 command WS，支持 `SendEvent(connection_id, Json)`。
3. 增加 capture DTO、`CaptureTaskService`、`CapturePipelineService` 和 `CaptureFacade`。
4. 把 `capture.take` 改为异步提交，新增 `capture.get`。
5. 完成 mock provider 全链路。
6. 完成 `sdk_private` raw capture。
7. 完成 `sdk_private` 纸张处理、色彩处理、格式转换和缩略图。
8. demo-site 接入真实 `capture.take/capture.get` 和事件。

## 11. 验收场景

mock provider：

- `capture.take` 返回 `accepted=true` 和 `task_id`。
- command WS 收到 `capture.started`、阶段事件和 `capture.completed`。
- `capture.get` 返回 `succeeded`、完整 stages、最终文件和缩略图 assets。

fallback：

- 关闭纸张处理 provider 后，任务仍成功，`PageProcess` 记录 fallback。
- 关闭色彩处理 provider 后，任务仍成功，`ColorMode` 记录 fallback。
- 缩略图生成失败后，任务仍成功，warnings 包含缩略图失败原因。
- 目标格式为 `tiff` 且格式转换 provider 不可用时，任务失败。

协议与授权：

- 未授权调用 `capture.take` 返回授权错误。
- 授权后 `capture.take` 消耗一次 capture quota。
- `capture.get` 不消耗 quota。
- 查询不存在 task 返回 `InvalidParams` 或专用 task not found 错误。
- 非任务创建连接查询 task 返回权限错误。

真实设备：

- 设备未打开时 `capture.take` 返回 `DeviceNotOpen`。
- 设备打开后可完成 raw capture。
- 支持激光图设备返回 `laser_path`。
- 不支持激光图设备 `laser_path` 为空但任务继续。
