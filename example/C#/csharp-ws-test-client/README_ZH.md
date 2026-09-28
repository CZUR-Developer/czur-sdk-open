# SDK Open C# WebSocket 测试客户端

这是一个独立的 .NET 8 WinForms 测试台，只连接已经启动的 `sdk_open_app`；它不启动、停止或安装 C++ runtime。

## 启动

```powershell
# 在本 README 所在的项目根目录执行（无需写死本机路径）
dotnet restore SdkOpen.CSharpTestClient.sln --configfile NuGet.Config
dotnet run --project src\SdkOpen.TestClient.WinForms\SdkOpen.TestClient.WinForms.csproj --no-restore
```

## 基础编译环境

- Windows 10/11 x64，建议使用与运行时一致的 Windows 用户运行 Demo。
- Visual Studio 2022 17.x，安装“.NET 桌面开发”工作负载。
- .NET 8 SDK（建议安装最新 8.0.x SDK）以及 .NET 8 Windows Desktop Runtime。
- 可访问 NuGet 源；首次构建需要执行 `dotnet restore` 下载 `System.Security.Cryptography.ProtectedData` 等运行依赖。
- 已启动的 `sdk_open_app` 运行时，并确认 health、command、video、asset 端点可访问。
- 本项目使用 `net8.0` Core 类库和 `net8.0-windows` WinForms；仅支持 Windows，不需要 C++ 编译器即可构建 C# 测试台。

在 VS2022 中打开 `SdkOpen.CSharpTestClient.sln`，将 `SdkOpen.TestClient.WinForms` 设置为启动项目。

默认端点为 health `http://127.0.0.1:17080/healthz`、command `ws://127.0.0.1:17090`、video `ws://127.0.0.1:17091`、asset `http://127.0.0.1:17082`；TLS 默认端口对应 `18090`、`18091` 和 `18082`。

## 授权信息与本地配置位置

- 点击“保存并连接”后，API Token 使用 Windows DPAPI 的 `CurrentUser` 作用域加密，保存在当前 Windows 用户的 `%LOCALAPPDATA%\CZUR\SdkOpenTestClient\secrets\<别名>.bin`。文件内容不是明文 Token，只有同一 Windows 用户能够解密；更换用户、Windows 用户配置文件或运行环境后不能直接复用该密文。
- 端点、配置名称和 API Token 别名保存在 `%LOCALAPPDATA%\CZUR\SdkOpenTestClient\profile.json`。该文件只保存连接配置和别名，不保存 API Token 明文；如果需要重置本地配置，可在程序退出后删除该目录，下一次启动会恢复默认端点。
- `session_token` 是连接 `sdk_open` 后由 `auth.create_session` 返回的短期会话凭据，仅保存在当前进程内存中，断开连接或退出程序后不会写入磁盘。
- 请求、响应和事件只在当前运行期间显示在时间线中；界面复制和详情查看会递归脱敏 Token 等敏感字段，不会把会话凭据保存到项目目录。
- 以上配置位于用户数据目录，不会写入本项目目录；项目目录只包含源码、解决方案和构建输出。命令行示例均以本 README 所在目录为项目根目录，使用相对路径。

## 使用顺序

1. 启动外部 `sdk_open_app`。
2. 在“连接与认证”填写 SDK API Token 的别名和 Token，点击“保存并连接”。客户端会以 `auth.create_session` 的 `params.token` 发送 Token；Token 仅以 Windows DPAPI `CurrentUser` 加密保存，session token 仅在内存中。
3. 常规设备操作使用“设备与视频”：刷新设备、选择设备与运行时返回的分辨率、打开设备，再启动/停止预览。按钮会依据状态机启用，不会在未选设备时发送无效的 `video.start`。
4. 常规采集使用“采集”表单：选择页面处理、颜色、输出格式、四类缩略图和可选增强工作流后点击“采集一页”。客户端把同一份 `capture.profile.v1` 和增强 `pipeline` 用于 `video.start`、`video.set_profile` 与 `capture.take`，跟踪 `capture.completed`/`capture.failed` 事件，并以 `capture.get` 受限轮询补齐结果。自动裁切由 `page_processing=single_page` 表达；裁边宽高只是额外边缘余量；翻页检测通过独立的 `capture.set_turn_detect` 命令设置。
5. “图像处理”支持选择本地图片、原图/结果对比、矩形或四点区域绘制以及多结果预览。客户端通过 Asset Endpoint 的 `POST /api/uploads/images` 上传图片并携带当前内存会话的 Bearer Token，然后按表单执行 `image.process`、`image.process_page` 或 `image.apply_color_mode`。综合处理可选 JPG/PNG/TIFF；独立纸张处理和色彩模式保持源图片格式，格式转换应使用 `file.convert`。双击结果行会重新显示完整请求/响应，便于定位错误。
6. “图像增强”已经拆分为独立的结构化工作流页面，代码保留了图片上传、能力加载、工作流编辑、异步任务和结果预览逻辑；当前 Windows UI 暂时禁用“图像增强”及其后的页签，待 runtime 能力验证完成后再开放。
7. “原始 JSON 命令”页面当前随图像增强及后续页签一起禁用；相关实现仅保留在代码中，重新开放后只用于尚未预置的公开 SDK 方法。
8. 视频连接由 `video.start` 返回的 stream id 与内存 session token 建立；本程序只连接已启动的 runtime，绝不启动、停止或修改 C++ runtime。
9. Windows UI 不显示 SANE 或预置场景入口；SANE 依赖 Linux 扫描器，当前运行版不包含场景 JSON 和场景执行器。
10. 使用工具栏的“English/中文”按钮切换主窗口和已开放页面的语言，无需重启程序。
11. 时间线中的请求、响应和事件行支持双击或选中后按 Enter 查看完整脱敏详情，可滚动、全选和复制长响应。

请求、响应、事件、时间线和复制内容会递归隐藏 `token`、`session_token`、`api_key`、`authorization` 和 `secret` 字段。

时间线中的请求、响应和事件行支持双击或选中后按 Enter 查看详情。详情窗口会显示完整的时间、类型、摘要和已脱敏 Payload，支持滚动、全选和复制，便于定位设备或 runtime 返回的错误。

采集任务表会保留 runtime 返回的完整 `assets`、`stages` 和 `warnings`。结果预览按 `final_thumbnail`、`color_processed_thumbnail`、`page_processed_thumbnail`、`original_thumbnail`、`final`、`color_processed`、`page_processed`、`original` 的顺序选取，避免处理已经执行却仍显示原图。双击任务或预览记录可把完整任务快照显示到右侧响应区，用于判断处理阶段是否 fallback。

## 验证

运行版验证只需要构建唯一的运行版解决方案：

```powershell
dotnet build SdkOpen.CSharpTestClient.sln --configuration Release --no-restore --nologo
```

## 运行版结构与发布

`SdkOpen.CSharpTestClient.sln` 是唯一的运行版解决方案，包含 `SdkOpen.TestClient.Core` 和 WinForms 应用 `SdkOpen.TestClient.WinForms`。在 VS2022 中打开此文件，并将 WinForms 项目作为启动项目。

对外发布时只需要发布 WinForms 应用、依赖的 Core 程序集和 .NET 8 Windows Desktop 运行时。SANE、预置场景以及当前禁用的图像增强及后续页签不属于 Windows 运行版内容。