# 更新日志

[English version](./CHANGELOG.md)

本文档记录 CZUR Open SDK 项目的重要版本变更。

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
