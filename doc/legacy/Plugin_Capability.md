# CZUR SDK 对外能力与协议规范

> 状态：草案 v1
>
> 适用范围：`sdk_app` 对外开放协议
>
> 配套文档：`SDK_INTERNAL_ARCH_PLAN.md`、`SDK_Auth_Error_Code.md`

## 1. 文档目标

本文档用于替代旧 2.0 时代按 `CZUR_ID_*` 函数列举的能力说明，统一定义新 SDK 的：

- 对外能力域与 method 命名
- WebSocket 指令协议格式
- 视频流与事件格式
- 能力授权边界
- 旧接口到新协议的映射关系

本文档是对外能力契约，不直接暴露内部 `devices_lib`、`graphic_service_lib`、`ocr_lib`、`ofd_lib` 的私有结构。

## 2. 产品定位与服务入口

`sdk_app` 是独立于 CEF UI 主进程运行的本地后台进程，面向 Linux 本地场景提供统一 SDK 能力入口。

### 2.1 默认入口

| 入口类型 | 地址 | 用途 |
|---|---|---|
| HTTP 管理站 | `127.0.0.1:17080` | 运行状态检查、管理页 |
| HTTP 演示站 | `127.0.0.1:17081` | Demo 页、集成演示 |
| HTTP 资源服务 | `127.0.0.1:17082` | 图片、缩略图、文档等二进制资源访问 |
| WS 指令通道 | `127.0.0.1:17090` | JSON 指令、事件订阅、能力调用 |
| WS 视频通道 | `127.0.0.1:17091` | 视频帧二进制推流、视频控制 |

### 2.2 约束

- 资源服务独立于管理站和演示站，第三方业务系统应使用 `http://127.0.0.1:17082/api/assets/...` 访问采集输出资源。
- 资源接口继续使用会话授权：`Authorization: Bearer <session_token>`。

- 所有入口默认仅监听 `localhost`
- 指令通道与视频通道分离
- 对外协议统一采用 JSON `method + params + auth` 封装
- 不再对外暴露“主摄/副摄/红外/通用摄像头分别占一个端口”的旧协议模型

## 3. 协议总览

## 3.1 method 命名规范

- 采用点分风格：`domain.action`
- `domain` 表示能力域，如 `device`、`capture`、`ocr`
- `action` 使用动词或动宾短语，如 `list`、`take`、`recognize_batch`
- 同类能力尽量复用统一壳，不为每个细项定义完全独立的消息结构

## 3.2 指令请求格式

```json
{
  "request_id": "3f4e7582-54cf-4f6c-b6e5-a8d2f4c8b0b5",
  "method": "capture.take",
  "params": {
    "device_id": "czur-001",
    "format": "jpg"
  },
  "auth": {
    "api_key": "czur_xxx",
    "session_token": "optional"
  },
  "client": {
    "sdk_version": "1.0.0",
    "app_id": "demo-site"
  }
}
```

字段约束：

- `request_id`：必填，由调用方生成，建议使用 UUID
- `method`：必填，必须符合本文档定义
- `params`：必填，固定为 JSON 对象，没有参数时传空对象
- `auth`：受保护能力必填
- `client`：可选，用于审计、兼容分析和日志关联

## 3.3 指令响应格式

```json
{
  "request_id": "3f4e7582-54cf-4f6c-b6e5-a8d2f4c8b0b5",
  "code": 0,
  "message": "ok",
  "data": {
    "task_id": "task-123"
  },
  "trace_id": "9e8f2d31-c806-4f8d-bbe0-2b3d1c0fd2f2",
  "ts": 1710000000
}
```

字段约束：

- `code = 0` 表示成功
- `code != 0` 时，`data` 可为空对象，详细错误语义见 `SDK_Auth_Error_Code.md`
- `trace_id` 由服务端生成，用于日志排查
- `ts` 为服务端 Unix 时间戳

## 3.4 事件消息格式

```json
{
  "event": "device.online",
  "code": 0,
  "message": "ok",
  "payload": {
    "device_id": "czur-001"
  },
  "ts": 1710000000
}
```

规则：

- 事件不绑定单个 `request_id`
- `event` 使用点分命名
- `payload` 为事件负载

## 3.5 视频通道格式

视频通道默认传输 JPEG 二进制帧。每帧对应的元数据可以通过以下两种方式提供：

- 方案 A：视频二进制帧前导头
- 方案 B：通过配套文本事件发送元数据

v1 推荐采用方案 B，对外统一事件名 `stream.frame_meta`，最小元数据字段如下：

- `device_id`
- `stream_id`
- `frame_seq`
- `timestamp_ms`
- `width`
- `height`
- `pixel_format`

示例：

```json
{
  "event": "stream.frame_meta",
  "payload": {
    "device_id": "czur-001",
    "stream_id": "main",
    "frame_seq": 1024,
    "timestamp_ms": 1710000000123,
    "width": 1920,
    "height": 1080,
    "pixel_format": "bgr24"
  },
  "ts": 1710000000
}
```

## 4. 能力授权模型概览

每个 method 都绑定两个边界：

- `auth_scope`：调用该能力所需的认证/授权级别
- `device_scope`：调用该能力时是否受 ApiKey 内设备白名单约束

### 4.1 auth_scope 定义

| auth_scope | 含义 |
|---|---|
| `anonymous` | 可匿名调用 |
| `authenticated` | 需要合法 ApiKey 或 session |
| `entitled` | 需要合法 ApiKey，且账号等级满足要求 |

### 4.2 device_scope 定义

| device_scope | 含义 |
|---|---|
| `none` | 不校验 CZUR 设备白名单 |
| `target_device` | 校验请求中的目标 CZUR 设备 `vid/pid` |
| `all_bound_devices` | 查询结果仅返回 ApiKey 允许访问的设备集合 |

统一授权细则、ApiKey 解析语义和错误码，见 `SDK_Auth_Error_Code.md`。

## 5. 能力状态定义

| 状态 | 含义 |
|---|---|
| `ga` | 对外协议已稳定，优先进入 v1 核心实现 |
| `beta` | 纳入 v1 范围，但允许实现阶段做小幅细化 |
| `planned` | 预留能力，当前版本只保留协议位或能力占位 |

说明：

- 本文档覆盖旧 2.0 能力全量去向
- 并非所有能力都要求在首个交付版本中完整实现
- `planned` 能力必须保留 method 名称与语义位置，避免后续再次破坏协议

## 6. 对外能力目录

## 6.1 系统与授权域

| 域 | method | 说明 | auth_scope | device_scope | 状态 |
|---|---|---|---|---|---|
| system | `system.ping` | 存活探测 | `anonymous` | `none` | `ga` |
| system | `system.info` | 返回版本、端口、provider 模式、运行状态 | `anonymous` | `none` | `ga` |
| system | `system.capabilities` | 返回当前开放 method 与能力域清单 | `anonymous` | `none` | `ga` |
| auth | `auth.validate` | 校验 ApiKey 并返回授权上下文 | `authenticated` | `none` | `ga` |
| auth | `auth.refresh` | 以 ApiKey 换取短期 session token | `authenticated` | `none` | `beta` |
| auth | `auth.get_context` | 查询当前连接的授权上下文与 capability 视图 | `authenticated` | `none` | `beta` |

## 6.2 设备域

承接旧 2.0 的初始化、设备发现、设备匹配、摄像头打开关闭、预览控制能力。

| 域 | method | 说明 | auth_scope | device_scope | 状态 |
|---|---|---|---|---|---|
| device | `device.list` | 枚举当前可访问 CZUR 设备 | `authenticated` | `all_bound_devices` | `ga` |
| device | `device.get` | 查询单个设备详情 | `authenticated` | `target_device` | `ga` |
| device | `device.open` | 打开设备与视频采集资源 | `authenticated` | `target_device` | `beta` |
| device | `device.close` | 关闭设备与视频采集资源 | `authenticated` | `target_device` | `beta` |
| device | `device.set_preview_resolution` | 设置预览分辨率 | `authenticated` | `target_device` | `beta` |
| device | `device.get_models` | 查询模型信息与设备规格 | `authenticated` | `all_bound_devices` | `beta` |
| device | `device.match_model` | 根据识别信息匹配机型 | `authenticated` | `target_device` | `planned` |

统一设备描述建议包含：

```json
{
  "device_id": "czur-001",
  "model": "ET24 Pro",
  "display_name": "CZUR ET24 Pro",
  "vid": 4660,
  "pid": 22136,
  "status": "online",
  "authorized": true,
  "cameras": [
    { "stream_id": "main", "role": "main" },
    { "stream_id": "aux", "role": "aux" }
  ]
}
```

## 6.3 视频与事件域

| 域 | method / event | 说明 | auth_scope | device_scope | 状态 |
|---|---|---|---|---|---|
| stream | `stream.subscribe` | 订阅设备视频流 | `authenticated` | `target_device` | `ga` |
| stream | `stream.unsubscribe` | 取消订阅视频流 | `authenticated` | `target_device` | `ga` |
| stream | `stream.set_options` | 设置流格式、帧率、质量等参数 | `authenticated` | `target_device` | `beta` |
| event | `device.online` | 设备上线事件 | `authenticated` | `all_bound_devices` | `ga` |
| event | `device.offline` | 设备下线事件 | `authenticated` | `all_bound_devices` | `ga` |
| event | `stream.frame_meta` | 视频帧元数据事件 | `authenticated` | `target_device` | `ga` |
| event | `capture.detected_rectangle` | 自动裁剪区域事件 | `authenticated` | `target_device` | `beta` |
| event | `capture.turn_detected` | 翻页/硬拍动作检测事件 | `authenticated` | `target_device` | `beta` |
| event | `recognition.barcode_detected` | 实时条码识别事件 | `authenticated` | `target_device` | `beta` |

## 6.4 采集与拍摄域

承接旧 2.0 的拍照、Base64 输出、自动裁剪、翻页检测、A4 幅面、特殊幅面、曲面展平、残缺补边、黑边修复能力。

| 域 | method | 说明 | auth_scope | device_scope | 状态 |
|---|---|---|---|---|---|
| capture | `capture.take` | 拍照并输出文件 | `authenticated` | `target_device` | `ga` |
| capture | `capture.take_base64` | 拍照并返回 Base64 | `authenticated` | `target_device` | `beta` |
| capture | `capture.set_turn_detect` | 设置翻页/硬拍检测 | `authenticated` | `target_device` | `beta` |
| capture | `capture.set_scan_area` | 设置 A4、A3、Shine 等扫描幅面规则 | `authenticated` | `target_device` | `beta` |
| capture | `capture.set_page_fill` | 设置残缺页面补全策略 | `entitled` | `target_device` | `beta` |
| capture | `capture.set_edge_repair` | 设置黑边修复策略 | `entitled` | `target_device` | `beta` |

`capture.take` 推荐参数：

```json
{
  "device_id": "czur-001",
  "output": {
    "mode": "file",
    "path": "/tmp/out.jpg"
  },
  "format": "jpg",
  "dpi": 300,
  "color_mode": "auto",
  "auto_crop": true,
  "auto_deskew": true,
  "curve_flatten": true,
  "detect_barcode": false,
  "detect_blank_page": false,
  "scan_wide_mode": "a4",
  "page_fill": false,
  "edge_repair": false
}
```

返回结果建议：

```json
{
  "file_path": "/tmp/out.jpg",
  "format": "jpg",
  "width": 2480,
  "height": 3508,
  "detected_rectangles": [],
  "barcodes": []
}
```

## 6.5 图像处理域

承接旧 2.0 的图像后处理、规格化、颜色填充、背景替换、属性调整、手写擦除、拼接、切分、红绿头优化、水印能力。

| 域 | method | 说明 | auth_scope | device_scope | 状态 |
|---|---|---|---|---|---|
| image | `image.process` | 统一图像处理入口，支持组合处理 | `authenticated` | `none` | `ga` |
| image | `image.normalize` | 图像规格化、尺寸边距统一 | `authenticated` | `none` | `beta` |
| image | `image.color_fill` | 指定区域颜色填充 | `authenticated` | `none` | `beta` |
| image | `image.replace_background` | 替换背景颜色 | `authenticated` | `none` | `beta` |
| image | `image.adjust` | 对比度、锐度、文字粗细调整 | `authenticated` | `none` | `beta` |
| image | `image.erase_handwriting` | 擦除手写笔迹 | `entitled` | `none` | `beta` |
| image | `image.merge` | 多图拼接 | `authenticated` | `none` | `beta` |
| image | `image.clip` | 单图切分 | `authenticated` | `none` | `beta` |
| image | `image.replace_text_color` | 红绿头等文字颜色优化 | `authenticated` | `none` | `beta` |
| image | `image.add_watermark` | 添加文字或图片水印 | `authenticated` | `none` | `beta` |

统一请求壳建议采用：

```json
{
  "input": {
    "files": ["/tmp/a.jpg"]
  },
  "output": {
    "dir": "/tmp/out"
  },
  "options": {
    "operations": [
      { "type": "normalize", "paper_size": "A4" },
      { "type": "replace_background", "color": "#FFFFFF" }
    ]
  }
}
```

## 6.6 OCR 与识别域

承接旧 2.0 的 OCR、条码识别、实时条码检测和区域坐标输出能力。

| 域 | method | 说明 | auth_scope | device_scope | 状态 |
|---|---|---|---|---|---|
| ocr | `ocr.recognize` | 单次 OCR 识别 | `authenticated` | `none` | `ga` |
| ocr | `ocr.recognize_batch` | 批量 OCR 识别 | `authenticated` | `none` | `beta` |
| recognition | `recognition.barcode` | 对图像做条码/二维码识别 | `authenticated` | `none` | `ga` |
| recognition | `recognition.set_realtime_barcode` | 设置视频流实时条码检测 | `authenticated` | `target_device` | `beta` |

OCR 返回结构建议：

```json
{
  "text": "example",
  "language": "zh-CN",
  "confidence": 0.98,
  "page_count": 1,
  "blocks": [
    {
      "text": "example",
      "bbox": [10, 10, 120, 40],
      "confidence": 0.98
    }
  ]
}
```

条码返回结构建议：

```json
{
  "items": [
    {
      "type": "qr_code",
      "text": "123456",
      "points": [[0, 0], [100, 0], [100, 100], [0, 100]]
    }
  ]
}
```

主要支持类型：

- `qr_code`
- `pdf417`
- `code128`
- `code39`
- `ean13`
- `upca`
- `upce`
- `ean8`
- `isbn13`
- `itf`

## 6.7 文件处理域

承接旧 2.0 的 PDF/OFD/TIFF 生成、文件夹操作、文件操作、文件属性、上传、打印能力。

| 域 | method | 说明 | auth_scope | device_scope | 状态 |
|---|---|---|---|---|---|
| file | `file.convert` | 统一文件转换入口 | `authenticated` | `none` | `ga` |
| file | `file.info` | 获取文件属性与元数据 | `authenticated` | `none` | `beta` |
| file | `file.set_metadata` | 设置文件属性 | `authenticated` | `none` | `planned` |
| file | `file.rename` | 重命名文件 | `authenticated` | `none` | `beta` |
| file | `file.delete` | 删除文件 | `authenticated` | `none` | `beta` |
| file | `file.mkdir` | 创建目录 | `authenticated` | `none` | `beta` |
| file | `file.upload` | HTTP 文件上传 | `entitled` | `none` | `planned` |
| file | `file.print` | 文件打印 | `authenticated` | `none` | `planned` |

`file.convert` 支持的典型转换：

- `image -> pdf`
- `base64 -> pdf`
- `images -> pdf`
- `image -> ofd`
- `image -> tiff`

示例：

```json
{
  "source": {
    "type": "images",
    "files": ["/tmp/1.jpg", "/tmp/2.jpg"]
  },
  "target": {
    "type": "pdf",
    "path": "/tmp/out.pdf"
  },
  "options": {
    "ofd_profile": "standard"
  }
}
```

## 6.8 SANE 域

承接旧 2.0 的第三方扫描仪能力。该能力与 CZUR 自有设备域独立。

| 域 | method / event | 说明 | auth_scope | device_scope | 状态 |
|---|---|---|---|---|---|
| sane | `sane.list` | 获取 SANE 扫描仪列表 | `authenticated` | `none` | `beta` |
| sane | `sane.get_options` | 获取设备参数项 | `authenticated` | `none` | `beta` |
| sane | `sane.set_option` | 设置 SANE 参数 | `authenticated` | `none` | `beta` |
| sane | `sane.scan` | 启动扫描 | `authenticated` | `none` | `beta` |
| sane | `sane.cancel` | 取消扫描 | `authenticated` | `none` | `beta` |
| event | `sane.scan_status` | 扫描状态事件 | `authenticated` | `none` | `beta` |
| event | `sane.scan_file` | 扫描结果文件事件 | `authenticated` | `none` | `beta` |

说明：

- `sane.*` 不受 ApiKey 的 CZUR `vid/pid` 白名单约束
- `sane.*` 仍受账号能力控制
- 依赖宿主机已安装对应驱动和运行环境

## 7. 统一类型定义建议

## 7.1 CapabilityDescriptor

```json
{
  "domain": "capture",
  "method": "capture.take",
  "status": "ga",
  "auth_scope": "authenticated",
  "device_scope": "target_device",
  "summary": "拍照并输出文件"
}
```

## 7.2 AuthContext 摘要

完整字段定义见 `SDK_Auth_Error_Code.md`，这里保留最小引用：

```json
{
  "is_valid": true,
  "account_type": "svip_plus",
  "device_scope": [
    { "vid": 4660, "pid": 22136 }
  ]
}
```

## 8. 兼容性约束

## 8.1 与当前 runtime stub 的关系

当前仓库中的 `sdk_open/runtime` 仅实现了极简命令集：

- `ping`
- `getStatus`
- `listCapabilities`

这些命令是协议雏形，不是最终对外规范。后续实现应迁移到本文档定义的正式 method：

| 当前 stub | 目标正式 method |
|---|---|
| `ping` | `system.ping` |
| `getStatus` | `system.info` |
| `listCapabilities` | `system.capabilities` |

过渡期可同时兼容旧命令和新命令，但文档、示例、SDK 接入说明一律以正式 method 为准。

## 8.2 新旧接口映射

下表给出旧 2.0 能力到新 method 的映射关系。

| 旧接口/能力 | 新 method |
|---|---|
| `CZUR_ID_Initialize` | `system.ping` / `auth.validate` |
| `CZUR_ID_Deinitialize` | 连接关闭，无独立 method |
| `CZUR_ID_QueryAdvanceState` | `auth.get_context` |
| `CZUR_ID_GetDeviceModel` | `device.get_models` |
| `CZUR_ID_QueryDeviceID` | `device.list` |
| `CZUR_ID_GetDeviceID` | `device.get` |
| `CZUR_ID_QueryMatchModel` | `device.match_model` |
| `CZUR_ID_OpenDevice` | `device.open` |
| `CZUR_ID_CloseDevice` | `device.close` |
| `CZUR_ID_Get_All_Camera` | `device.get` |
| `CZUR_ID_Open_3rdDevice` | `device.open` |
| `CZUR_ID_SetPreviewResolution` | `device.set_preview_resolution` |
| `CZUR_ID_GrabImage` | `capture.take` |
| `CZUR_ID_GrabBase64` | `capture.take_base64` |
| `CZUR_ID_RECTANGLE` | `capture.detected_rectangle` |
| `CZUR_ID_SetTurnDetect` | `capture.set_turn_detect` |
| `CZUR_ID_GRAB` | `capture.turn_detected` |
| `CZUR_ID_SetA4Wide` | `capture.set_scan_area` |
| `CZUR_ID_SetScanWide` | `capture.set_scan_area` |
| `CZUR_ID_SetPageFill` | `capture.set_page_fill` |
| `CZUR_ID_SetEdgeRepair` | `capture.set_edge_repair` |
| `CZUR_ID_Image_Process` | `image.process` |
| `CZUR_ID_Image_Normalization` | `image.normalize` |
| `CZUR_ID_Color_Fill` | `image.color_fill` |
| `CZUR_ID_Backcolor_Replace` | `image.replace_background` |
| `CZUR_ID_Property_Adjust` | `image.adjust` |
| `CZUR_ID_Handwritten_erase` | `image.erase_handwriting` |
| `CZUR_ID_Image_Merge` | `image.merge` |
| `CZUR_ID_Image_Clip` | `image.clip` |
| `CZUR_ID_Image_Color_Replace` | `image.replace_text_color` |
| `CZUR_ID_Watermark_Add` | `image.add_watermark` |
| `CZUR_ID_Set_Ofd_Type` | `file.convert` |
| `CZUR_ID_Start_OCR` | `ocr.recognize` |
| `CZUR_ID_SetBcrDetect` | `recognition.set_realtime_barcode` |
| `CZUR_ID_BCR_EDGE` | `recognition.barcode_detected` |
| `CZUR_ID_ImageToPDF` | `file.convert` |
| `CZUR_ID_Base64ToPDF` | `file.convert` |
| `CZUR_ID_ImageToPDFEx` | `file.convert` |
| `CZUR_ID_ImageToOFD` | `file.convert` |
| `CZUR_ID_ImageToTiff` | `file.convert` |
| `CZUR_ID_CreateFolder` | `file.mkdir` |
| `CZUR_ID_DeleteFile` | `file.delete` |
| `CZUR_ID_RenameFile` | `file.rename` |
| `CZUR_ID_Get_File_Info` | `file.info` |
| `CZUR_ID_Set_File_info` | `file.set_metadata` |
| `CZUR_ID_Http_Create` / `CZUR_ID_Http_Delete` | `file.upload` |
| `CZUR_ID_Print_File` | `file.print` |
| `CZUR_ID_Get_Scanner_List` | `sane.list` |
| `CZUR_ID_Set_Sane_Option` | `sane.get_options` / `sane.set_option` |
| `CZUR_ID_Scan_With_Sane` | `sane.scan` |
| `CZUR_ID_SCAN` | `sane.scan_status` |
| `CZUR_ID_SCAN_FILE` | `sane.scan_file` |

## 9. 实现约束

- 传输层不得直接暴露内部私有结构体
- 业务能力调用必须经过 Facade / Provider 接口
- 所有正式 method 都必须给出稳定错误码
- 所有受保护能力都必须统一走 `auth` 上下文校验
- 新能力只能在现有 domain 下扩展，避免重复发明平行接口
