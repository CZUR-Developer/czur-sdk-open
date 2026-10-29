# SDK Open Windows x86/x64、TWAIN 与临时目录清理交付说明

> 状态：截至 2026-07-24 的实现、打包和真实联调结论。
>
> 范围：SDK Open private 模式的 Windows x64/x86 构建与打包、TWAIN 双架构访问、`storage.cleanup_temp`、Linux/SANE 并发保护，以及由这些改动引出的部署注意事项。
>
> 不包含：非 `sdk_open` 的 CEF App 业务接入、Win32 Debug、修改 SDK Open 运行工作目录。

## 1. 当前版本与提交范围

本次功能分支的最终修复提交为 `c3bbd6e9f`，已合并到主仓库 `develop`。截至 2026-07-24，主仓库 `develop` 的 HEAD 为 `788766c7c`，其 `src/sdk_open` gitlink 固定指向 `c06970d4`。

本地 `src/sdk_open` 若检出 `develop` 分支的 `df00e35a`，其文件树与 `c06970d4` 完全一致，并且后者是前者的祖先；但为了让顶层仓库可复现，构建主仓库 `develop` 时仍应按 gitlink 检出 `c06970d4`。除非后续明确提交一次顶层 gitlink 更新，否则不要仅因子模块当前分支前进到 `df00e35a` 就修改顶层引用。

本次交付相关提交如下：

| 仓库 | 提交 | 内容 |
| --- | --- | --- |
| 主仓库 | `e41dac102` | SDK Open Win32 兼容、CEF/ABBYY 隔离、私有临时目录删除实现、TWAIN 基础兼容 |
| 主仓库 | `0f842c9a7` | Windows Jenkins 构建支持 `x64`、`x86`、`all` |
| 主仓库 | `2777b4c30` | 安装包仅保留隔离 helper，修复两组 helper 重复交付 |
| 主仓库 | `d49ea3be3` | 为 x86 helper 增加 TWAIN 文件 I/O 路径诊断与重定向基础 |
| 主仓库 | `c3bbd6e9f` | 用主进程架构约束 Sysnative，修复 Win32 扫描后的资产 `404` |
| SDK Open 子模块 | `d3a7cb4` | 新增 `storage.cleanup_temp`、独立 Demo 页面和安全的目录删除协议 |
| SDK Open 子模块 | `55382bc` | Demo 切页时自动关闭已打开的 TWAIN Source |
| SDK Open 子模块 | `d7f95fb` | Linux/SANE、capture、图像增强与清理并发保护，清理锁改为立即返回 busy |

工作区中目前存在的 `build_private*/`、安装包产物和未跟踪脚本均不是本文档记录的源码交付内容，不能作为发布依据。

## 2. 构建隔离边界

### 2.1 不需要新增用户可见开关

现有参数中的 `BUILD_SDK_OPEN=ON` 已经是隔离开关：根 `CMakeLists.txt` 将 `CZUR_SDK_OPEN_ISOLATED_BUILD` 置为 `ON`。因此下面这组 private SDK Open 参数无需再叠加 CEF/ABBYY 专用开关：

```powershell
-DBUILD_SDK_OPEN=ON
-DBUILD_SDK_WEB=ON
-DSDK_OPEN_ENABLE_HTTP_SERVER=ON
-DBUILD_SDK_PRIVATE=ON
-DSDK_OPEN_USE_PRIVATE_PROVIDER=ON
```

`SDK_OPEN_USE_PRIVATE_PROVIDER=ON` 仍依赖 `BUILD_SDK_PRIVATE=ON`，该约束由 CMake 显式校验。

### 2.2 CEF 与 ABBYY 的实际行为

当 `CZUR_SDK_OPEN_ISOLATED_BUILD=ON` 时：

- CEF 根目录校验、`src/framework` 和主 CEF App 目标不会进入 SDK Open 构建；这避免了仅构建 SDK Open 时调用 CEF 专用宏，例如早期遇到的 `SET_LIBRARY_TARGET_PROPERTIES` 未定义错误。
- `CZUR_ENABLE_ABBYY_OCR` 被强制为 `OFF`。`src/module/ocr/CMakeLists.txt` 因此不加入 ABBYY 源文件、头文件、链接输入或许可证资源，并向 C++ 目标传递 `CZUR_ENABLE_ABBYY_OCR=0`。
- Windows ABBYY 许可证文件和 ABBYY runtime 拷贝属于非隔离 App 的构建/打包路径；`abbyy/resources/lic_win/1344.lock` 不构成 SDK Open 运行依赖。
- 非 SDK Open 构建保持 `CZUR_SDK_OPEN_ISOLATED_BUILD=OFF` 与 `CZUR_ENABLE_ABBYY_OCR=ON` 的既有默认行为，CEF/ABBYY 流程不变。

## 3. Windows x64/x86 构建与依赖

### 3.1 本地 Release 命令

Visual Studio 是多配置生成器，`-DCMAKE_BUILD_TYPE=Release` 不决定实际配置；实际以 `cmake --build --config Release` 为准。为保证位数明确，推荐显式指定生成器和平台。

```powershell
# x64 SDK Open：同时交付 x86 和 x64 TWAIN helper
cmake -S . -B build_private_x64 -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_SDK_OPEN=ON `
  -DBUILD_SDK_WEB=ON `
  -DSDK_OPEN_ENABLE_HTTP_SERVER=ON `
  -DBUILD_SDK_PRIVATE=ON `
  -DSDK_OPEN_USE_PRIVATE_PROVIDER=ON

cmake --build build_private_x64 --config Release --target sdk_open_app sdk_auth_tool -- /m:4

# x86 SDK Open：只交付 x86 TWAIN helper
cmake -S . -B build_private_win32 -G "Visual Studio 17 2022" -A Win32 `
  -DBUILD_SDK_OPEN=ON `
  -DBUILD_SDK_WEB=ON `
  -DSDK_OPEN_ENABLE_HTTP_SERVER=ON `
  -DBUILD_SDK_PRIVATE=ON `
  -DSDK_OPEN_USE_PRIVATE_PROVIDER=ON

cmake --build build_private_win32 --config Release --target sdk_open_app sdk_auth_tool -- /m:4
```

Win32 Debug 不属于本次验证和交付范围。

### 3.2 x86 第三方输入

SDK Open Win32 隔离构建的特殊选择已固定为：

- `third_party_repo/czocr/include/window32`：如果存在 `czcv_ocr_capi.h`，优先使用该 x86 头文件，不能与 Windows x64 头文件混用。
- `third_party_repo/czocr/models/windows64`：按当前资源交付约定，Win32 SDK 仍复制这一份 czocr model；不要再按 `${LIB_ARCH}` 回退到不存在或不兼容的 x86 model 目录。
- `third_party_repo/czcv/models/x86-64`：SDK Open Windows 隔离构建使用现有兼容模型目录。
- OpenSSL、私有 Provider、`docx.dll`、`czcv_scan.dll`、`czcv_ocr_api.dll` 等运行时 DLL 必须和 `sdk_open_app.exe` 同位数。

### 3.3 helper 的自动构建与交付布局

`src/module/twain/CMakeLists.txt` 把 helper 目标命名为 `twain_helper_x86.exe` 或 `twain_helper_x64.exe`。x64 private SDK Open 构建会让 `sdk_open_app` 依赖 `twain_helper_x86_auto`，后者用一个内部 Win32 CMake 子构建生成 x86 helper；不需要再手工传入 `TWAIN_HELPER_X86_EXE`。

SDK Open 安装运行时只从下面目录启动 helper：

```text
twain_helper_runtime/
  twain_helper_x86.exe
  twain_helper_x64.exe   # 仅 x64 SDK Open 存在
```

交付规则：

| SDK Open 主程序 | 可枚举/使用的 DSM | 必须交付的 helper |
| --- | --- | --- |
| x64 | x64 DSM 与 x86 DSM | `twain_helper_x64.exe`、`twain_helper_x86.exe` |
| x86 | 仅 x86 DSM | `twain_helper_x86.exe` |

这条规则只针对 `CZUR_SDK_OPEN_ISOLATED_BUILD`。非 SDK Open App 的 helper 搜索路径和打包策略保持原有“主程序同目录”语义，不能据此推导或改变 App 的后续接入设计。

## 4. TWAIN 运行时修复

### 4.1 枚举策略

x64 SDK Open 需要同时通过 x64 与 x86 DSM 枚举 Source，不能按 Source 名称去重。相同名称的不同位数条目必须保留各自架构标识：如果 x86 打开失败，还要能继续尝试 x64，而不是因为过早去重丢失可用 Source。

x86 SDK Open 只启动/枚举 x86 DSM。实际设备列表受机器上安装的 DSM/Source 影响；本机验收基准是返回 6 条且包含 `CZUR Shine Series TWAIN`，其中 Shine 按 x86 驱动处理。该数字不是所有客户环境的硬编码期望。

### 4.2 打包后双 helper

构建目录根部可能同时出现 CMake 目标产物与隔离运行时副本。`src/sdk_open/packaging/windows/build-sdk-open-nsis.ps1` 在 NSIS staging 时主动排除根目录 `twain_helper_x86.exe` / `twain_helper_x64.exe`，只交付 `twain_helper_runtime` 内的一组。

安装校验脚本同时检查：

- `sdk_open_app.exe` 与 `sdk_private_providers.dll` 的 PE 架构；
- x64 包必须含 x86+x64 helper；
- x86 包必须含 x86 helper 且不得包含 x64 helper。

因此“安装后有两组 helper”的问题已在打包层消除。

### 4.3 Win32 扫描结果资产 404

问题根因不是 HTTP 路由或上传协议，而是 WOW64 文件系统重定向造成 helper 的实际写入路径与主进程的读取视图不一致。

- x64 SDK Open 调用 x86 helper 时，x86 helper 将逻辑 `System32` I/O 路径映射到 `Sysnative`，确保其写入真实 `System32`，随后由 x64 主进程从同一物理路径读取。
- x86 SDK Open 调用 x86 helper 时，二者都必须保留 WOW64 的 x86 文件视图；不能映射 `Sysnative`，否则 helper 写真实 `System32`、x86 主进程却从 `SysWOW64` 查询，最终任务存在但 asset 请求为 `404`。
- 启动 helper 时 SDK Open 追加 `--host-arch <x86|x64>`；helper 写入 `CZUR_TWAIN_HELPER_HOST_ARCH`，`ResolveTwainIoPath()` 只在“x86 helper + x64 host”的组合中使用 `Sysnative`。
- 对 native、memory、file transfer 的建目录、写文件、读文件大小都走同一 I/O 路径，并在错误日志同时保留 logical path、io path、`errno` 和 Win32 错误码。

这部分逻辑被 `CZUR_SDK_OPEN_ISOLATED_BUILD` 保护，不改变非 SDK Open 的 TWAIN I/O 行为。

### 4.4 Demo 生命周期

`TwainScanningPage.vue` 已在路由离开、页面卸载或切换 Source 时关闭已打开的 Source。此前“打开 Source 后切换其他页面，再回到页面无法重新打开”的 session 残留问题已修复。

## 5. 临时目录清理

### 5.1 对外协议与删除范围

新增 command WebSocket 方法：

```text
storage.cleanup_temp
```

它只要求有效会话，不消耗业务能力额度。请求不接收任意删除路径，运行时固定把 `GetSdkOpenWorkDir()` 传给 provider，真实删除逻辑只允许删除该目录的两个精确子目录：

```text
<work_dir>/capture
<work_dir>/tasks
```

路径会规范化、拒绝 `..` 等越界输入，再递归删除。清理成功后同时清理已终态的任务快照和资产索引；之后再查询旧任务或读取旧 asset 属于预期的 not found/`404` 测试场景。

### 5.2 分层与并发语义

分层没有让 SDK Open application 层直接删除文件：

```text
command_application_service
  -> StorageFacade
    -> ISdkStorageProvider
      -> Windows private C API / Linux private provider
        -> src/sdk_open/providers/private/private_storage_cleanup.cpp
```

真实删除与真实 OCR/TWAIN 任务检查在 private 层；Open 层同时检查 capture、图像增强、SANE、TWAIN 的公开任务状态，形成双层保护。Linux 通过 `CreateProviderBundle(true)` 显式挂载 storage provider。

清理采用 `std::try_to_lock` 获取 `storage_lifecycle_mu_`：

- 普通会写入 `capture/tasks` 的方法持有生命周期锁；当前覆盖 `capture.take`、`image.process*`、`image.enhance`、`ocr.recognize`、`file.convert`、`sane.scan`、`twain.scan`。
- 清理不能获得锁时立即返回 `StorageBusy`（错误码 `1800`），不等待、不取消任务，从而避免等待链和死锁。
- 即使已拿到锁，只要任一活跃任务仍可能读写临时目录，同样返回 `StorageBusy`，响应包含按 `capture`、`image_enhance`、`ocr`、`sane`、`twain` 分类的 active 计数。
- capture、图像增强和 SANE 的活跃计数以 worker 真正退出为边界，避免“任务已取消/显示终态但回调仍会写文件”时错误删除。

此前讨论过把所有临时存储读取都纳入 shared/exclusive lock；该扩大范围的方案已经撤回，当前交付仅保护写入生命周期。HTTP asset 在请求到达时才实际读文件，清理完成后的旧 URL 返回 `404` 是该维护命令的预期效果，不应把它理解为仍需持读锁的运行任务。

### 5.3 Demo 与异常模拟

对外 Demo 随 SDK Open 子模块交付独立 `/storage-management` 页面。页面展示完整 `storage.cleanup_temp` 请求/响应，并带可稳定复现的场景入口：

1. OCR 或其他异步处理进行中调用清理，预期 `StorageBusy`；
2. 清理成功后再执行任务查询和 asset 请求，预期旧任务/资产失效；
3. 清理后重新提交新任务，验证新的 `capture/tasks` 目录会按需重新创建。

OCR 耗时较长，适合作为稳定的“处理中清理”触发器；SANE/TWAIN、capture 和图像增强也已纳入同一 busy 判定。

### 5.4 当前未完成项：mock storage provider

当前 mock `ProviderBundle` 注册了 mock device/OCR/SANE/TWAIN 等 provider，但**没有**注册 `storage_provider`。因此 mock 模式调用 `storage.cleanup_temp` 会由 `StorageFacade` 返回 `ProviderNotReady`，不会执行任何文件删除。

这保证了 mock 不会误删真实目录，但并未完成“mock 模式下可成功执行安全清理”的需求。后续若需要该能力，应增加只操作 mock runtime work dir 的 `MockStorageProvider`，复用同样的固定目录白名单和 active-task 语义，并增加单测；不要让 mock 直接调用真实 private 删除实现。

## 6. Jenkins、安装与部署

### 6.1 Jenkins Windows 打包

仓库脚本已经支持 `WINDOWS_ARCH=x64|x86|all`：

- `src/sdk_open/ci/jenkins/sdk_open_pipeline_helpers.groovy` 读取并校验参数；未提供时保持 x64 默认。
- `src/sdk_open/ci/scripts/windows/remote_sdk_open_build.ps1` 为两种架构使用独立 build 目录、NSIS staging 目录和 PDB 目录；`all` 依次构建 x86、x64，最终安装包与符号文件不会互相覆盖。
- 脚本为 x64 传 `-A x64`，为 x86 传 `-A Win32`，并在打包前验证 PE 位数。
- 早期 x64 Jenkins 失败 `cmakePlatform` 未设置，是变量作用域错误；当前变量已定义在 `Build-SdkOpenWindowsArch` 函数内部，修复后 x64 打包已成功。

Jenkins 任务配置需要保留/新增 Choice 参数 `WINDOWS_ARCH`，候选值为 `x64`、`x86`、`all`，默认 `x64`。如果 Jenkins 使用的是仓库 Jenkinsfile 动态声明参数，还应把这个参数同步到对应 Jenkinsfile；当前 helper 支持手工配置页注入该参数。前端 Demo/Admin 不需要为打包架构增加配置。

源码修改只有被推送且 Jenkins checkout 到包含这些提交的主仓库 commit、以及对应 `src/sdk_open` 子模块 commit 后，远端 Jenkins 构建才会使用新逻辑。

### 6.2 安装目录与服务账户

Windows Service 在 `SYSTEM` 身份下启动时，默认 SDK 工作目录会解析为：

```text
C:\Windows\System32\config\systemprofile\AppData\Local\.czur\sdk
```

本次没有修改该路径。它是服务账户的 LocalAppData，不是安装目录；风险主要来自服务账户权限、WOW64 路径视图和排障可见性，而不是清理命令本身。`storage.cleanup_temp` 的删除范围已严格限制在该工作目录下的 `capture/tasks`，不会递归删除其他 systemprofile 文件。

TWAIN helper 继承服务进程上下文。此前“helper 没有管理员权限而无法写入”的现象，最终定位为 x86/x64 文件系统重定向导致写入/读取不在同一视图，而不是通过更改运行目录或强行提权解决。

不得将 x64 `sdk_open_app.exe` 单独覆盖到 x86 安装目录，或反向混用。一次真实排障中，x64 exe 加载 x86 `sdk_private_auth_c_api.dll` 返回 `ERROR_BAD_EXE_FORMAT (193)`，表现为授权失败。升级必须使用完整、同一架构的安装包，或同步替换 exe、private DLL、helper 与全部 runtime DLL。

## 7. 验证记录与后续检查

本轮已完成的验证包括：

- x64 SDK Open Jenkins 远程构建、签名和 NSIS 安装包验证成功；
- x86 helper 与 Win32 SDK Open 实机枚举/扫描验证正常；
- 修复后 Win32 TWAIN 扫描结果可以被主服务读取并返回 asset，不再复现上传/asset `404`；
- 安装包不再携带根目录第二组 helper；
- Demo 切页后可重新打开 TWAIN Source；
- `test/module/twain_helper_host_arch_static_test.ps1` 已通过，静态确认 `--host-arch`、`CZUR_TWAIN_HELPER_HOST_ARCH` 与 x64-only Sysnative 条件均存在；
- 本文档所列 CMake/PowerShell/C++ 改动均受 SDK Open 隔离条件或平台条件保护，未把 CEF、ABBYY、Windows TWAIN 引入 Linux 或非 SDK Open 构建。

仍应在发布前补齐或复测：

1. `WINDOWS_ARCH=all` 的一次完整 Jenkins 构建，确认两个安装包、PDB 和 checksum 同时上传；
2. x64 主程序同时安装 x86/x64 DSM 的最终枚举、打开和扫描回归；
3. Linux private 模式下 SANE 扫描中调用 `storage.cleanup_temp` 的 busy、扫描完成后的成功清理、清理后的 asset `404`；
4. mock `storage_provider` 的实现与自动化测试，若要把清理 Demo 标注为 mock 可用；
5. 将未跟踪的 `test/module/twain_helper_host_arch_static_test.ps1` 决定为正式归档测试或删除。它不是当前 Git 已提交文件，不能依赖其存在作为 CI 保障。

## 8. 关联文档

- `src/sdk_open/doc/sdk_open_twain_scanning_design_zh.md`：SDK Open `twain.*` 协议、Demo 和 provider 分层设计。
- `docs/twain/app_twain_module_integration_zh.md`：非 SDK Open App 后续直接使用 `module/twain` 的接入边界；本文件补充其中 SDK Open 隔离构建的实际 helper 布局。
- `src/sdk_open/doc/ocr_sdk_open_flow_and_linux_concurrency_2026-06.md`：Linux OCR 单槽位并发控制，与本文的临时目录生命周期锁是不同层次的保护。
