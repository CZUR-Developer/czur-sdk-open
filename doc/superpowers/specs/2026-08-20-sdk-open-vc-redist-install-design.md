# SDK Open Windows VC++ 运行库安装设计

## 背景

SDK Open Windows 安装包当前直接部署 runtime、注册 `sdk-open` 服务并启动服务，但没有随包安装 Microsoft Visual C++ 运行库。仓库已经提供 Microsoft 签名有效的 `14.44.35211.0` 安装程序：

- `third_party_repo/VC_redist.x64 14.44.35211.exe`
- `third_party_repo/VC_redist.x86 14.44.35211.exe`

本改动让不同架构的 SDK Open 安装包携带并静默安装对应架构的运行库。

## 已确认行为

- x64 SDK Open 安装包只携带 x64 VC++ 运行库。
- x86 SDK Open 安装包只携带 x86 VC++ 运行库。
- 运行库安装程序使用仓库中的原文件名写入用户选择的 SDK Open 安装目录，并在安装结束后保留。
- SDK Open 安装器使用 `/install /quiet /norestart` 静默安装运行库。
- 运行库安装发生在 `sdk-open` 服务注册和启动之前。
- 卸载 SDK Open 时，随安装目录删除运行库安装程序，但不卸载系统中已安装的 VC++ 运行库，避免影响其他应用。
- CI 的 `GenerateUninstallerOnly` 临时安装流程不执行 VC++ 运行库安装，避免为提取卸载器修改构建机环境。

## 打包设计

`src/sdk_open/packaging/windows/build-sdk-open-nsis.ps1` 根据 `-Architecture` 选择唯一的依赖文件：

| SDK 安装包架构 | 运行库源文件 | 安装目录文件名 |
| --- | --- | --- |
| x64 | `third_party_repo/VC_redist.x64 14.44.35211.exe` | `VC_redist.x64 14.44.35211.exe` |
| x86 | `third_party_repo/VC_redist.x86 14.44.35211.exe` | `VC_redist.x86 14.44.35211.exe` |

打包开始时必须校验所选文件存在；缺失时中止打包。脚本通过 NSIS define 传递源路径和目标文件名，不把 Redistributable 混入 runtime staging，也不改变现有 PDB、TWAIN Helper 和签名处理逻辑。

## 安装设计

`src/sdk_open/packaging/windows/sdk-open-minimal.nsi` 将对应 Redistributable 写入 `$INSTDIR`，然后调用现有的 `sdk-open-install-helper.ps1` 执行静默安装。复用 PowerShell helper 可以统一处理进程退出码和安装日志，避免在 NSIS 中处理带空格路径与 HRESULT 表示差异。

helper 新增 VC++ 运行库安装 action，使用 `Start-Process -Wait -PassThru` 调用安装程序。以下结果视为可继续：

- `0`：安装成功。
- `3010`：安装成功但建议重启；由于使用 `/norestart`，SDK Open 安装继续，且安装器不主动重启系统。
- `1638` 或对应 HRESULT `0x80070666`：相同或更新的兼容运行库已经存在。

其他退出码写入 `C:\ProgramData\CZUR\sdk-open\install.log`，并令 SDK Open 安装器以非零状态中止。日志不得记录敏感信息。

## 安装顺序

最终安装顺序为：

1. 停止并删除已有 `sdk-open` 服务。
2. 将 SDK Open runtime、安装 helper 和对应架构 Redistributable 写入 `$INSTDIR`。
3. 静默安装 VC++ 运行库。
4. 配置 TLS、证书和 hosts。
5. 写入卸载信息。
6. 注册、配置并启动 `sdk-open` 服务。
7. 等待服务和 Quick Start 页面就绪。

## 验证设计

在现有 `src/sdk_open/ci/scripts/tests/test_sdk_open_windows_installer.py` 中增加静态结构检查，覆盖：

- 打包脚本按架构选择且只选择一个 Redistributable。
- 两个固定文件名和 `14.44.35211` 版本均被引用。
- NSIS 将 Redistributable 保留到 `$INSTDIR`。
- 静默参数完整，且安装发生在服务注册之前。
- helper 接受 `0`、`3010`、`1638`/`0x80070666`，其他返回码失败。
- `GenerateUninstallerOnly` 路径不执行 Redistributable 安装。

同时更新 Windows 安装包 README，说明架构映射、静默安装、保留文件、返回码和卸载边界。

根据当前任务的测试授权边界，实现阶段默认只做静态代码合法性审查；如需执行 Python 单测、构建安装包或实际安装验证，需由用户明确授权。

## 非目标

- 不在 x64 安装包中携带或安装 x86 Redistributable。
- 不在 x86 安装包中携带或安装 x64 Redistributable。
- 不修改 CapEditor 主客户端安装包。
- 不自动卸载或降级机器上已有的 VC++ 运行库。
- 不更改 Open SDK runtime、TWAIN Helper 或服务自身的编译方式。
