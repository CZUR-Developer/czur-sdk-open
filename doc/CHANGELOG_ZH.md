# 更新日志

[English version](./CHANGELOG.md)

本文档记录 CZUR Open SDK 项目的重要版本变更。

## [0.0.2] - 2026-06-26

### 新增

- 新增 `sdk_open_app.exe` 的 Windows Service 模式，同时保留控制台启动方式，便于调试。
- 新增采集流程中的设备动作与移除支持，包括 `capture.set_turn_detect`、`capture.turn_detected`、`capture.hardgrab_detected` 和 `device.removed`。
- 新增 OCR 可下载任务资产和运行时版本元信息。

### 变更

- `ocr.recognize` 在未传入 `output_path` / `output_dir` 时可使用 SDK 托管输出路径，支持 `output_format` 作为 `format` 别名，并返回可下载的 `task.assets`。
- `video.set_profile` 同时支持嵌套和内联 profile 参数；`capture.get` 返回标准化任务结构。
- 优化授权上下文响应，以及 Demo 站点 command/video WebSocket 端口配置能力。

### 修复

- 修复本地授权、离线激活、授权上下文序列化和会话缓存稳定性问题。
- 修复设备视频启动/停止、视频格式设置、设备关闭清理和采集任务异常处理问题。
- 修复 OCR 导出/下载响应、Demo 路由与运行事件展示、SANE 不可用响应，以及 Windows 构建/private Provider 兼容问题。

### 兼容性说明

- 新增 command 方法：`capture.set_turn_detect`。
- 新增 command 事件：`capture.turn_detected`、`capture.hardgrab_detected`、`device.removed`。
- 新增设备动作/事件 Provider 可选接口；默认实现保持旧 Provider 兼容。

## [0.0.1] - 2026-06-17

### 新增

- 引入 CZUR Open SDK 独立运行时入口 `sdk_open_app`。
- 建立 transport、application、facade、provider interface 四层运行时结构。
- 增加本地 admin、demo、asset HTTP 服务。
- 增加 command WebSocket，支持 system、auth、device、video、capture、image、file conversion 等指令。
- 增加 video WebSocket，用于预览帧流和相关视频事件输出。
- 增加基于 API Key 的会话创建、会话刷新、会话销毁、授权上下文查询和离线 API Key 解封流程。
- 增加带 quota 控制的拍摄、图像处理、页面处理、色彩模式应用和文件转换业务方法。
- 增加 device、graphic、image enhancement、OCR、OFD、recognition、SANE、authorization 等能力的公共 provider interface 和 DTO。
- 增加 mock provider 组合，使运行时可在不暴露私有能力库类型的前提下独立构建和体验。
- 增加内置 admin 和 demo Web 站点，用于本地运行时管理与 SDK 工作流验证。
- 增加运行时架构、指令通道流程和 SDK 错误码等公开文档。

### 平台

- Linux 是首个已实现平台。
- Windows 和 macOS 支持计划在后续版本中逐步补齐。
