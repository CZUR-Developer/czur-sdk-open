# sdk-open Windows NSIS 打包说明

本目录用于创建最简单的 sdk-open Windows 安装包。当前方案使用仓库自带 NSIS，不再使用 7-Zip SFX 自解压流程。

## 输出目录

默认输出到：

```text
src\sdk_open\packaging\windows\output
```

该目录由打包脚本自动创建，默认包含：

- `sdk-open_Windows_x64_v<version>.<build>.<stage>.exe`：x64 NSIS 安装包。
- `sdk-open_Windows_x86_v<version>.<build>.<stage>.exe`：x86 NSIS 安装包。
- `staging\`：临时运行时拷贝目录，供 `makensis.exe` 读取打包内容；这是可删除的中间目录，重新打包时会再次生成。

如需临时输出到其他位置，可通过 `-OutputDir <path>` 覆盖；未指定时始终写入本目录下的 `output`。`output` 是打包产物目录，不作为源码提交内容。

## 打包命令

先完成 sdk-open Release 编译，再执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\src\sdk_open\packaging\windows\build-sdk-open-nsis.ps1 `
  -RuntimeDir .\cmake-build-open_sdk_release\Release `
  -Architecture x64
```

参数说明：

- `-RuntimeDir`：sdk-open 运行时目录，目录内必须有 `sdk_open_app.exe`。
- `-Architecture`：安装包架构，可选 `x64` 或 `x86`，默认 `x64`。
- `-OutputDir`：安装包输出目录，默认是 `src\sdk_open\packaging\windows\output`。
- `-MakeNsis`：`makensis.exe` 路径，默认使用 `packaging\windows\NSIS\makensis.exe`。
- `-PdbOutputDir`：可选。指定后会在打包前把 runtime 中的 `.pdb` 复制到该目录用于符号备份；安装包 staging 中的 `.pdb` 仍会被删除，最终安装包不包含 PDB。
- `-GenerateUninstallerOnly`：CI 签名专用。生成一个临时 NSIS 安装器并静默安装到临时目录，只用于提取 NSIS 生成的 `uninstall.exe`。
- `-SignedUninstallerPath`：CI 签名专用。配合 `-GenerateUninstallerOnly` 时作为导出的 `uninstall.exe` 路径；最终打包时作为已签名卸载器 payload 注入安装目录。

## Windows 签名流程

Jenkins 上的 sdk-open Windows 流水线复用客户端 Windows signing broker。`ENABLE_WINDOWS_CODE_SIGNING=true` 时会执行三段签名：

1. 在 NSIS 打包前签名运行时入口 `sdk_open_app.exe`，确保安装目录内的主程序已签名。
2. 先构建临时 NSIS 安装器，通过静默安装只生成 `uninstall.exe`，签名并校验该卸载器，再在最终 NSIS 构建中把它作为 payload 写入安装目录，避免 `WriteUninstaller` 在用户安装时重新生成未签名卸载器。
3. 最终安装包复制到 artifact 目录后、生成 `SHA256SUMS.txt` 前签名 `sdk-open_Windows_*.exe`。

安装验证脚本在签名开启时会检查最终安装包、安装后的 `sdk_open_app.exe` 和安装后的 `uninstall.exe` 的 Authenticode 状态均为 `Valid`。

## 安装包行为

这是最小安装包，行为如下：

1. 请求管理员权限。
2. 显示安装目录选择页，默认目录是 `$PROGRAMFILES64\CZUR\sdk-open`。
3. 将 sdk-open runtime 文件直接安装到用户选择的目录。
4. 将与安装包架构匹配的 Microsoft Visual C++ 2015-2022 Redistributable `14.44.35211` 写入安装目录，并使用 `/install /quiet /norestart` 静默安装。
5. 写入卸载程序 `uninstall.exe`。
6. 写入 Windows 卸载注册表项。
7. 调用已签名的 `sdk_open_app.exe --provision-tls` 在本机生成根 CA 私钥、服务证书和私钥，并写入 `C:\ProgramData\CZUR\sdk-open\tls`；已有匹配的客户证书和私钥不会被覆盖。
8. 托管模式下，将仅含公钥的根 CA 写入普通用户可读取的 `C:\Users\Public\Documents\CZUR\local-runtime-root-ca.cer`，便于使用独立 NSS 证书库的浏览器手工导入；修复安装会更新该文件，不会向此目录写入服务端证书或私钥。客户自定义证书模式跳过 SDK 根 CA 的导入和导出。
9. 托管模式下将本机生成的根 CA 加入 LocalMachine Root 信任库并核对指纹；安装器在 hosts 中将 `sdk-runtime.localhost` 映射到 `127.0.0.1`。如果系统 hosts 文件缺失，安装器会先创建一个 UTF-8（无 BOM）的空文件；创建或写入失败才会中止安装。
10. 写入 `C:\ProgramData\CZUR\sdk-open\runtime.env`，启用 HTTPS/WSS 本机端口和默认 asset URL。
11. 清理旧版用户计划任务，并使用 `sdk_open_app.exe --install-service` 注册自动启动的 `sdk-open` Windows 服务。
12. 运行时常驻于 `LocalSystem` / Session 0；当需要 TWAIN 时，服务会在当前活动用户会话启动无窗口的 `twain_helper`，通过受限 named pipe 回传扫描结果。
13. 为服务与 helper 配置共享的 `C:\ProgramData\CZUR\sdk-open\twain-runtime` 工作目录，管理员和 SYSTEM 完全控制，普通用户仅有修改扫描任务文件的权限。
14. 启动服务，并等待 `http://127.0.0.1:17081/quick-start` 返回 HTTP 200。
14. 安装完成页显示默认不勾选的 `Open Quick Start` checkbox，用户勾选后用默认浏览器打开 `http://127.0.0.1:17081/quick-start`。

VC++ 运行库严格按安装包架构选择：

- x64 安装包只携带 `VC_redist.x64 14.44.35211.exe`。
- x86 安装包只携带 `VC_redist.x86 14.44.35211.exe`。

Redistributable 安装程序会保留在用户选择的 SDK Open 安装目录中，并在启动
SDK Open 服务之前执行。退出码 `0`、`3010`（成功但建议重启）、`1638` 或对应的
`0x80070666`（相同或更新的兼容版本已经存在）均允许安装继续；其他退出码会中止安装。
SDK Open 卸载时只随安装目录删除 Redistributable 安装程序，不卸载系统中的 VC++
运行库，以免影响其他应用。CI 的 `GenerateUninstallerOnly` 临时流程不会执行运行库安装。

默认服务信息：

- 服务名：`sdk-open`
- 运行账户：`LocalSystem` / Session 0
- 安装目录：安装时选择的目录
- 服务程序：`sdk_open_app.exe --service --service-name sdk-open --config C:\ProgramData\CZUR\sdk-open\runtime.env`

默认 TLS 配置将 `sdk-runtime.localhost` 解析到 `127.0.0.1`，并使 HTTPS/WSS listener
只绑定回环地址；安装器已经信任本机生成的 Local Runtime Root CA，所以可以直接访问
`https://sdk-runtime.localhost:18082`。客户可以用自己的证书链和私钥成对替换
`C:\ProgramData\CZUR\sdk-open\tls` 中的默认文件，重启服务后生效；如调整路径、域名
或端口，还需要同步修改 `runtime.env`。升级安装不会覆盖已有的匹配证书和私钥。

叶证书有效期为 397 天，剩余 90 天时由 Windows 服务自动轮换并重新加载 TLS listener。
客户自定义证书后，SDK 会保留客户证书并停止自动轮换。根 CA 私钥仅供本机 runtime 使用，
不得用于互联网或跨客户部署。

升级时兼容旧版五年和 825 天两套默认叶证书。仅当证书与私钥都匹配已知默认值、
且没有托管状态或根私钥时，安装器才移除旧共享凭据；同时识别并移除旧包附带的
公共根证书，避免它因缺少根私钥而阻塞本机 CA 的生成。客户替换过的任一叶文件都
不会被当作默认凭据删除，完整客户证书对会记录为 `custom`。
迁移签发失败时会恢复原始文件；若恢复也失败，会保留受限 TLS 目录中的备份并记录位置。
迁移成功后会按固定 DER 指纹移除旧包的共享根证书信任，卸载时也会清理该已知旧证书；
其他同名根证书保持不变。

已有托管安装缺少根证书或根私钥时，安装器会报错并保留文件，要求恢复原 CA；
不会通过重新生成根 CA 使现有信任失效。未知根证书缺少私钥、或叶证书与私钥不完整
时也会中止安装，并将原因记录到 `install.log`。

说明：sdk-open-app 的 demo site 默认监听 `http://127.0.0.1:17081`，前端路由使用 history 模式，因此 Quick Start 页面地址是 `/quick-start`，不是本地 `index.html#/quick-start`。

安装结束前必须满足：

- `sdk-open` 服务处于 `RUNNING` 状态。
- demo site 的 Quick Start 页面可以访问。

升级安装和卸载时，安装器会停止并删除服务；同时会清理此前试验版本遗留的 `\CZUR\SDK Open Runtime` 计划任务，避免占用端口。

如果服务创建、启动或页面就绪检查失败，安装器会弹出错误并中止，不会进入完成页。静默安装
不会等待不可见的错误对话框，而是直接以非零状态退出。hosts 配置和 Quick Start 检查的诊断
信息写入 `C:\ProgramData\CZUR\sdk-open\install.log`；自动安装验证会在安装器失败或超过
10 分钟未退出时打印该日志，并终止超时进程。

## 卸载行为

卸载程序会：

1. 停止并删除 `sdk-open` 服务，同时清理此前试验版本遗留的 `\CZUR\SDK Open Runtime` 计划任务。
2. 按安装目录内根证书的 DER SHA-1 指纹，从 LocalMachine Root 信任库移除对应的 Local Runtime Root CA，并清理已知旧包共享根证书，保留其他同名证书；客户自定义模式仅清理该已知旧包根证书，保留客户信任设置。删除安装器添加的 hosts 映射；如果 hosts 文件已不存在，卸载仍会继续，且不会删除整个 hosts 文件。
3. 删除 `C:\ProgramData\CZUR\sdk-open` 下的 TLS 配置与私钥，以及安装器生成的 `C:\Users\Public\Documents\CZUR\local-runtime-root-ca.cer`；若 `CZUR` 公共文档目录不为空则保留该目录。
4. 删除卸载注册表项和安装目录。

## 验证

TLS 迁移回归使用临时目录调用实际构建的运行时，覆盖新装、两个旧版默认凭据、
重复安装、客户证书保护、不完整凭据失败及卸载指纹匹配。证书库边界使用 mock，
不会修改本机信任、hosts 或服务；Windows 构建启用安装验证时会先执行该回归：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\src\sdk_open\packaging\windows\test-runtime-tls.ps1 `
  -RuntimeExecutable .\cmake-build-open_sdk_release\Release\sdk_open_app.exe
```

`verify-sdk-open-install.ps1` 另行验证真实安装、信任导入、修复安装和卸载；
修复安装后必须保留同一根 CA，公共导出必须与当前根证书一致，卸载只检查本次安装的精确指纹。

安装后可手工检查：

```powershell
sc.exe query sdk-open
sc.exe qc sdk-open
Invoke-WebRequest http://127.0.0.1:17081/quick-start -UseBasicParsing
```

需要临时停止或重新启动服务时，可执行：

```powershell
sc.exe stop sdk-open
sc.exe start sdk-open
```

安装、升级与卸载也会自动管理该服务；服务运行在后台，不会显示终端窗口。

卸载后可检查：

```powershell
sc.exe query sdk-open
```

期望返回 `1060`，表示服务不存在。
