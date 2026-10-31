# sdk_open 鸿蒙编译、运行与调试适配计划

> 状态：正式形态已调整为 `com.czur.opensdk` 单 `sdkopen_tray` entry HAP；隐藏托盘 Ability 持有 runtime，不承诺系统级保活或开机自启
>
> 目的：基于仓库中已经完成的 HarmonyOS 构建历史和当前 `sdk_open` 实现，明确鸿蒙编译、运行、调试所需的适配修改、开发边界和验收顺序。
>
> GBrain：已查询，未命中本次单 HAP 生命周期迁移的直接记录；本方案以本地 Git 历史、当前代码和 HarmonyOS 文档为准。

## 1. 结论先行

当前仓库已新增独立的 `src/sdk_open/platform/harmony/` HarmonyOS 工程。它把
`sdk_open_runtime`、主仓库提供的真实 provider 组合和一个不含 `main()` 的 N-API host 编译进
`libsdkopen_harmony.so`。工程只交付 `sdkopen_tray` entry HAP：
`SdkOpenLauncherAbility` 注册托盘项，`ATTACH_TO_STATUS_BAR_ITEM` 模式的隐藏
`SdkOpenTrayAbility` 持有 runtime、devices 和 USB Host 生命周期。

该工程不加入仓库根 `build-profile.json5`，也不改变桌面端 `build.sh`、
`tools/harmony/build_sdk_open_app.sh` 和 `sdk_open_app` 的默认行为。

现有 CapEditor 鸿蒙链路是：

```text
DevEco/Hvigor
  -> entry HAP
  -> entry/src/main/cpp/libentry.so
  -> ArkTS/ArkWeb NativeAction
  -> app_lib / Harmony backend adapter
```

现有 `sdk_open` 链路是另一种模型：

```text
桌面 CMake
  -> sdk_open_runtime 静态库
  -> sdk_open_app 可执行文件（带 main）
  -> HTTP/WS listeners
  -> Linux/macOS/Windows provider
```

因此没有把 `sdk_open_app` 当作普通 HAP native target 直接链接。当前首期基线为：

1. 保留现有 command/video/HTTP/WS 协议和 application/facade 业务层。
2. 将 `sdk_open_runtime` 编译成 `arm64-v8a` Harmony native library。
3. 新增不含 `main()` 的 host，由隐藏托盘 Ability 的 N-API 调用 `start/stop`；状态通过 native 和 loopback HTTP 查询。
4. 首期协议沿用 mock bundle 作为容器，但主仓库 Harmony host 实际替换为真实
   device、auth、imageEnhance、OCR、OFD、graphic 和 recognition provider；device 使用
   USB Host/C++ UVC，桌面 SANE/TWAIN 不编入并明确返回 `ProviderUnavailable`。
5. 工作目录、web root 和日志目录由 ArkTS `Context.filesDir` 注入，禁止依赖 `$HOME/.czur/sdk` 或 `/var/lib/czur/sdk-open`。

不建议把“鸿蒙运行”定义成在 HAP 内启动独立 `sdk_open_app` 进程。当前形态由托盘
UIAbility 内嵌 runtime，复用桌面业务层和协议层，但不复用桌面 service/daemon 安装逻辑。
单 HAP 方案不声明 `AppServiceExtensionAbility`，不申请
`ohos.permission.SUPPORT_APP_SERVICE_EXTENSION`，也不依赖
`ohos.permission.START_INVISIBLE_ABILITY`。真机安装仍需匹配 `com.czur.opensdk` 的普通
签名/profile；旧 `com.czur.sdkopen` 的 profile 和应用沙箱不会迁移。

### 1.1 正式部署目标：托盘进程内嵌 runtime

正式产品定义调整为：sdk_open runtime 无普通窗口，由 2in1 托盘入口显式控制生命周期；
用户点击“启动服务”时运行，点击“关闭服务”时结束。不要求系统级永久驻留、安装即
启动或开机自动恢复：

```text
sdkopen_tray entry HAP
  -> SdkOpenLauncherAbility 注册托盘
  -> 隐藏 SdkOpenTrayAbility
  -> SdkOpenRuntime / sdk_open_harmony_host::Start()
  -> sdk_open_runtime / loopback HTTP/WS

sdkopen_tray “关闭服务”
  -> SdkOpenRuntime.stop()
  -> sdk_open_harmony_host::Stop()

系统“退出”
  -> stopForExit() 等待 runtime/devices/USB 清理
  -> 销毁托盘项和隐藏 Ability
```

托盘 ArkTS 只负责生命周期和状态展示；auth、图像增强、OCR、OFD 等业务仍在 native runtime。
`@kit.DeskTopExtensionKit.statusBarManager` 注册真实状态栏图标，
`StatusBarViewExtensionAbility` 承载左键弹窗，右键菜单提供启动、关闭和系统退出。
启动入口注册托盘后，以 `ATTACH_TO_STATUS_BAR_ITEM` 和 `STARTUP_HIDE` 创建隐藏的
控制 Ability，再结束启动入口。弹窗进程通过 Want 将命令交给隐藏 Ability，后者控制
`SdkOpenRuntime`；HTTP 查询只读取状态。不存在普通常驻控制窗口。

### 1.2 当前已交付的基线

| 项目 | 当前结果 |
| --- | --- |
| 独立工程 | `src/sdk_open/platform/harmony/`，bundle 为 `com.czur.opensdk`。 |
| Native target | `sdkopen_harmony` / `sdk_open_harmony_core`，OHOS arm64 Debug/Release 已编译通过。 |
| 生命周期 | 隐藏 `SdkOpenTrayAbility` 通过 `SdkOpenRuntime` 调用 N-API `start/stop`；状态通过 native/HTTP 查询。 |
| 服务接口 | HAP rawfile 复制到沙箱后，由 runtime 在 loopback 提供 HTTP/WS；服务自身不打开页面。 |
| Provider | device/auth/imageEnhance/OCR/OFD/graphic/recognition 使用真实 private provider；device 接入 USB Host/C++ UVC，桌面 SANE/TWAIN 不可用。 |
| 传输安全 | 首期 loopback HTTP/WS，TLS 关闭。 |
| 安装包 | 单 HAP Release 整包已构建成功；tray HAP 的 30 个 native 库、包名、权限和 rawfile 资源检查通过。 |
| 设备验证 | 旧双 HAP/AppService 架构曾在 HarmonyOS 6.0.0(20) arm64 模拟器验收；不能作为当前单 HAP 或正式签名真机结果。 |

## 2. 历史构建流程复盘

### 2.1 sdk_open 构建和发布历史

| 时间 | 提交 | 已形成的能力 |
| --- | --- | --- |
| 2026-05-27 | `c00c143bc` | 新增 `tools/harmony/build_sdk_open_app.sh`，形成 Linux Debian 打包入口。 |
| 2026-05-27 | `6c5e12463` | 在 README 中固定 `build.sh --target sdk_open_app` 和 `tools/harmony/build_sdk_open_app.sh` 用法。 |
| 2026-05-27 | `873b3b796`、`99672787f` | sdk_open 默认使用 OpenSSL 3，并通过 CMake 参数切换 TLS。 |
| 2026-06-16～06-27 | `fd65ca23c` 等 | 形成 SDK Open package pipeline、版本文件、发布和 Linux arm64 打包流程。 |
| 2026-06-18 | `d630d80a6` | 增加 Windows NSIS/service 打包流程。 |
| 2026-07-09～08-25 | 多个 `sdk-open` 提交 | 补齐 Windows service、TWAIN、运行库、签名、版本和发布细节。 |

当前桌面构建命令仍是：

```bash
./build.sh --target sdk_open_app -- \
  -DBUILD_SDK_OPEN=ON \
  -DBUILD_SDK_WEB=ON \
  -DSDK_OPEN_ENABLE_HTTP_SERVER=ON \
  -DBUILD_SDK_PRIVATE=ON \
  -DSDK_OPEN_USE_PRIVATE_PROVIDER=ON

./package-sdkopen.sh --distro uos
```

桌面 SDK Open 的打包和发布入口由主仓库维护。Harmony HAP/App 的正式入口是主仓库
`tools/harmony/build_sdk_open_app.sh`；子仓库中的 `scripts/harmony/` 只提供 unsigned
开发构建辅助，不能替代主仓库的签名、验签和归档流程。

### 2.2 鸿蒙工程和调试历史

鸿蒙工程在 2026-06-29～07-10 期间逐步形成：

| 提交 | 内容 | 对 sdk_open 的启示 |
| --- | --- | --- |
| `d27e0ff14` | 添加 DevEco 工程骨架、`build-profile.json5`、`entry` HAP | 鸿蒙工程入口是仓库根工程 + `entry`，不是把既有 `src/` 当 HAP 根目录。 |
| `2b69213b0`、`8c414efb1` | 对齐模块层级和 compile/target SDK | native CMake 必须挂在 `entry/build-profile.json5` 的 `externalNativeOptions`。 |
| `6a141f3c1`、`42a5157fb` | 配置 entry Native target，触发 DevEco CMake | 当前根 CMake 的 Harmony 分支仍是迁移 target，尚非完整 sdk_open 目标。 |
| `730af5325` | 接入 ArkWeb、N-API、NativeAction dispatcher | 可复用的入口是 `entry` native library 和 ArkTS bridge，不是桌面 `main()`。 |
| `47441af00`、`cc6fbcaa7` | 接入后端 bridge、编辑保存和图像/服务适配 | 复杂业务应进入 adapter/backend，不应在 ArkTS 复制业务逻辑。 |
| `0e79cc1ea` | 新增 `tools/harmony/build_app.sh` | 形成前端构建、rawfile 同步、Hvigor assembleApp、产物收集流程。 |
| `40aff6714` | 工程化签名配置、通用 build helper、回归脚本 | 形成 HAP/app 编译、签名、bridge smoke 和 UI smoke 的验证入口。 |

现有鸿蒙应用构建流程为：

```text
views pnpm build:all
  -> views.build
  -> entry/src/main/resources/rawfile/web
  -> hvigorw assembleApp
  -> HAP/.app
  -> hdc install/start
  -> hilog / bridge smoke / UI smoke
```

对应脚本：

```text
tools/harmony/build_app.sh
tools/harmony/run_harmony_bridge_regression.sh
tools/harmony/run_bridge_smoke.sh
tools/harmony/run_ui_smoke.sh
tools/harmony/run_arkweb_hypium.sh
```

## 3. 当前编译和调试环境

### 3.1 本机已确认的工具

本次检查结果：

| 工具 | 路径 | 结果 |
| --- | --- | --- |
| Hvigor | `/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw` | 存在，版本 6.26.1。 |
| Node | `/Applications/DevEco-Studio.app/Contents/tools/node/bin/node` | 存在，版本 v24.14.1。 |
| CMake | `/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/native/build-tools/cmake/bin/cmake` | 存在，版本 3.28.2。 |
| Ninja | `/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/native/build-tools/cmake/bin/ninja` | 存在，版本 1.12.0。 |
| hdc | `/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/hdc` | 已通过本地 HarmonyOS 6.0.0(20) 模拟器验证。 |

文档中还记录了 `/Users/apple/Library/OpenHarmony/Sdk/26.0.0` 的 OpenHarmony Native SDK 路径。当前机器实际可用的是 DevEco SDK 下的：

```text
/Applications/DevEco-Studio.app/Contents/sdk/default
```

因此脚本和文档应优先通过 `DEVECO_SDK_HOME`/`OHOS_SDK` 配置，不应把某一个机器路径写死。

### 3.2 独立 sdk_open 托盘整包编译

推荐命令：

```bash
cd /Users/apple/czur-capeditor
src/sdk_open/scripts/harmony/build_sdk_open_app.sh --clean
```

需要重建 demo/admin 前端时追加 `--build-web`；Release 编译追加 `--release`。
输出为：

```text
src/sdk_open/platform/harmony/build/outputs/default/harmony-default-unsigned.app
src/sdk_open/platform/harmony/sdkopen_tray/build/default/outputs/default/sdkopen_tray-default-unsigned.hap
```

脚本调用独立工程的整包构建：

```bash
hvigorw --mode project \
  -p product=default \
  -p buildMode=debug \
  assembleApp --no-daemon
```

默认 `.app` 和 HAP 未签名；真机安装前需要为独立工程配置签名。该设置不应写入或覆盖
CapEditor 主工程的签名配置。

### 3.3 CapEditor 主 HAP 编译（对照）

只使用已有前端和 rawfile 产物时：

```bash
cd /Users/apple/czur-capeditor
DEVECO_SDK_HOME=/Applications/DevEco-Studio.app/Contents/sdk \
JAVA_HOME=/Applications/DevEco-Studio.app/Contents/jbr/Contents/Home \
HVIGOR=/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw \
tools/harmony/build_app.sh --skip-frontend --skip-rawfile-sync
```

完整构建：

```bash
tools/harmony/build_app.sh --clean
```

脚本会执行 `views pnpm run build:all`、同步 `entry/src/main/resources/rawfile/web`，再调用：

```bash
hvigorw --mode project -p product=default assembleApp --no-daemon
```

独立 sdk_open 工程不修改 `entry/build-profile.json5`，主 HAP 构建行为保持不变。

### 3.4 独立 OHOS CMake 验证

适合验证 sdk_open 核心库和三方依赖，不产生 HAP：

```bash
SDK=/Applications/DevEco-Studio.app/Contents/sdk/default
CMAKE="$SDK/openharmony/native/build-tools/cmake/bin/cmake"
NINJA="$SDK/openharmony/native/build-tools/cmake/bin/ninja"

"$CMAKE" -S sdk_open_harmony -B build/sdk-open-harmony-native -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_TOOLCHAIN_FILE="$SDK/openharmony/native/build/cmake/ohos.toolchain.cmake" \
  -DOHOS_ARCH=arm64-v8a \
  -DOHOS_PLATFORM=OHOS \
  -DOHOS_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Debug

"$CMAKE" --build build/sdk-open-harmony-native --target sdk_open_harmony_core -j 4
```

该命令已在本机通过，生成 AArch64 `libsdkopen_harmony.so`。独立工程直接
`add_subdirectory(src/sdk_open)`，但只构建 host 依赖，不构建或运行桌面
`sdk_open_app`。

现有三方库独立构建入口仍然可以先用于准备 OHOS 依赖：

```bash
cd /Users/apple/czur-capeditor/third_party_repo
export OHOS_SDK=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony
python3 src/all_build_bat/generate_ohos_scripts.py
cd src
./build_all_ohos.sh
```

它只负责 `third_party_repo` 的 OHOS `arm64-v8a` 库和头文件，不会自动把 `sdk_open_runtime` 接入 HAP；两者的产物验证和 CMake target 仍需分开完成。

### 3.5 设备运行和调试

安装和日志检查：

```bash
HDC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/hdc
"$HDC" list targets -v
src/sdk_open/scripts/harmony/run_sdk_open_app.sh --app <signed-com.czur.opensdk.app> --target <target>
"$HDC" -t <target> shell \
  aa start -b com.czur.opensdk -m sdkopen_tray -a SdkOpenLauncherAbility
"$HDC" -t <target> shell hilog -x | rg -i \
  'sdk_open|SdkOpenRuntime|TypeError|ReferenceError'
```

也可使用独立脚本完成 target 检查和安装：

```bash
src/sdk_open/scripts/harmony/run_sdk_open_app.sh --app <signed-com.czur.opensdk.app> --target <target>
```

在托盘入口点击“启动服务”后，再执行协议 smoke：

```bash
src/sdk_open/scripts/harmony/run_sdk_open_app.sh --target <target> --skip-install --smoke
```

端口和协议 smoke：

```bash
curl http://127.0.0.1:17080/healthz
curl http://127.0.0.1:17080/api/status

# command/video WS 的默认端口分别为 17090/17091；TLS 端口为 18090/18091。
```

若宿主机不能直接访问设备回环地址，应使用 `hdc fport` 做端口转发，具体端口以设备和 hdc 版本支持为准。`hdc` 当前不在 PATH，但 DevEco SDK 内已有可执行文件；可使用上述绝对路径或把其目录加入 PATH。

当前调用链为：

```text
SdkOpenLauncherAbility
  -> ATTACH_TO_STATUS_BAR_ITEM 的隐藏 SdkOpenTrayAbility
  -> 用户点击“启动服务”
  -> SdkOpenRuntime.start()
  -> N-API host start()
  -> loopback HTTP/WebSocket runtime
```

`aa start` 只注册并打开托盘，不自动启动 runtime；仍需从托盘菜单或弹窗点击“启动服务”。
`run_sdk_open_app.sh --smoke` 只检查已经由托盘启动的 runtime。
旧 `com.czur.sdkopen` 可与新包并存，但使用相同 loopback 端口；启动新 runtime 前必须先从
旧托盘关闭旧 runtime，不需要卸载旧包或删除旧沙箱数据。

以下为迁移前双 HAP/AppService 架构在本机 HarmonyOS 6.0.0(20)、arm64 模拟器的历史结果，
不能作为当前单 HAP 架构验收：

- `sdkopen_tray` entry HAP 与 `sdkopen` feature HAP 已通过 `assembleApp` 整包校验；服务
  feature 只声明 `SdkOpenServiceExtension`，托盘 entry 提供 UIAbility 控制入口。
- shell 直接启动返回 `10107101 Permission check failed`，符合 AppService ACL 规则。
- 当前没有修改 CapEditor 或新增 sdk_open 可执行宿主；服务由同一 bundle 的托盘模块控制。
- 2026-10-29：保留用户数据迁移双模块后，控制窗口已在模拟器打开。服务 native 加载
  报缺少 `libfreeimage.so.3`；进一步检查发现旧包误带 Linux OpenSSL/ICU。已修正独立
  工程的依赖选择、SONAME 别名；OpenSSL 3 从仓库 LFS 源码补齐到 `lib/ohos`。
  HAP 内 29 个库通过 ABI/NEEDED 检查。控制页启动、停止、重启及 HTTP smoke 通过；
  命令 WS 的 `system.ping` 返回 code 0，停止服务后连接断开、三个 HTTP 端口关闭，
  刷新状态不会启动服务；Demo 网页已在模拟器浏览器打开。
- 托盘已接入真实 DeskTopExtensionKit API，初始化窗口结束后图标保留在状态栏；
  左键弹窗与右键菜单均通过隐藏的控制 Ability 调用 AppService 启停。
  已验证右键菜单启动、关闭，系统“退出”先停止 AppService 再结束托盘，三个 HTTP
  端口全部关闭；重复打开启动入口和刷新状态不会启动服务。退出回调通过
  `TrayAbilityStage.onPrepareTerminationAsync()` 实现，声明 `PREPARE_APP_TERMINATE`。
- 视频 WS 的认证/帧传输、真实能力完整回归和正式签名仍需在 2in1
  设备单独验收。
- 不能把先前 UIAbility/ArkWeb 形态的端口验证当作最终服务形态验收证据。

## 4. 已完成基线与剩余缺口

本节保留原双 HAP/AppService 迁移前基线。当前单 `sdkopen_tray` HAP 已重新完成 Release
整包与静态检查；生命周期、托盘交互和设备能力仍需正式签名真机验证。

### 4.1 构建层缺口

1. 根 `CMakeLists.txt` 的 Harmony 迁移分支保持原样；sdk_open 使用独立 CMake/Hvigor 工程，避免与 CapEditor 主 HAP 和桌面构建耦合。
2. `sdk_open_runtime`、CGraph、mock bundle 容器、主程序 auth/imageEnhance/OCR/OFD/
   graphic/recognition provider、N-API host 已在 OHOS arm64 Debug/Release 下编译链接通过。
3. CGraph 已增加精确的 `__OHOS__` 条件，跳过 OHOS 不提供的 `pthread_setaffinity_np`，不影响 Linux/Android 分支。
4. tray HAP 已验证包含 `libsdkopen_harmony.so` 和 `libc++_shared.so`；30 个 native 库通过 AArch64/`NEEDED` 检查。
5. 当前单 HAP Release 已完成全量复建；正式签名、真机和 CI 尚需验证。Harmony target 选择性
   复用 `PrivateGraphicProvider` 及其处理器，完整 `sdk_open_private` 和
   TWAIN/SANE/桌面动态库仍不进入首期 HAP。

### 4.2 运行时缺口

1. 首期 host 已把 `Context.filesDir` 下的工作目录和日志目录注入 runtime，不再使用桌面默认路径；后续仍需把 cache/temp/授权路径改为结构化参数。
2. `sdk_open_main.cpp` 未链接进 HAP；生命周期由隐藏 `SdkOpenTrayAbility`、`SdkOpenRuntime` 和 N-API host 管理。
3. admin/demo/asset HTTP 与 command/video WebSocket 已配置为 `127.0.0.1`；尚需在真机验证前后台切换、端口重绑和进程回收。
4. TLS 当前明确关闭；证书沙箱、密钥保护和 WSS 属于后续生产适配。
5. demo/admin 已由 Hvigor 同步进 tray rawfile，并在 runtime 启动时复制到 `filesDir` 后作为普通文件系统 web root 使用；它们只由 HTTP 提供，不创建原生页面。

### 4.3 能力和 provider 缺口

1. device/auth/imageEnhance/OCR/OFD/graphic/recognition 已切换为真实 provider；mock 仅作为
   bundle 容器和未接入能力的协议兜底，不用于这些能力的成功响应。
2. TWAIN 不适用于 HarmonyOS；SANE/libusb/桌面扫描 helper 也不能直接假设存在，当前
   Harmony bundle 不创建这两个 provider，调用返回 `ProviderUnavailable`。
3. 设备控制复用主仓 devices 与 Harmony USB Host/C++ UVC adapter；控制面走既有命令协议，
   预览走原始 MJPEG video WS，拍照和 still pair 走可靠任务结果，不把大图塞入 JSON。
4. graphic 已直接复用 `src/sdk_private/providers/private_graphic_provider.cpp` 及其
   page/color/format/thumbnail processor；Harmony 适配层仅提供沙箱路径、日志和图像
   编解码兼容，不在 bridge 中重写算法。
5. recognition 已复用主程序 ZXing provider；OCR/PDF/OFD 已按相同方式复用现有
   provider/module，CZOCR 模型和 MuPDF/OFD 运行库随 HAP 打包。

## 5. 推荐目标架构

```text
sdk_open_harmony .app
  └─ sdkopen_tray entry HAP（2in1 托盘宿主）
       ├─ SdkOpenLauncherAbility / SdkOpenTrayAbility
       ├─ SdkOpenRuntime：start / stop / stopForExit
       ├─ Context directories / rawfile
       ├─ libsdkopen_harmony.so
       │    ├─ N-API: start / stop / status
       │    ├─ sdk_open_harmony_host
       │    └─ Context sandbox path injection
       └─ sdk_open_runtime.a / provider modules
            ├─ transport: HTTP/WS
            ├─ application: auth/task/quota
            ├─ facade: device/graphic/ocr/ofd/storage
            └─ provider: Harmony adapters -> private capability
```

协议层建议保持不变：

- command WS：匿名建连，再以 `auth.create_session` 绑定 session。
- video WS：沿用现有 stream/session 语义。
- HTTP：保留 `/healthz`、`/api/status` 和可选静态 admin/demo；这些资源由服务提供，不在 HAP 内打开窗口。生产裁剪时可关闭 admin/demo，只保留 command/video。
- 错误码、request_id、事件格式和 capability 语义不因平台而改变。

### 运行模式

| 模式 | 用途 | 首期建议 |
| --- | --- | --- |
| 单 HAP 托盘 UIAbility | 隐藏托盘 Ability 显式启动/停止 `libsdkopen_harmony.so` 内的 `SdkApp` | Release 整包已通过；待正式签名 2in1 真机验收。 |
| System Ability / 厂商 system service | 系统镜像启动 native service并托管 runtime | 当前不采用；只有未来恢复开机自启和系统保活要求时再评估。 |
| 普通窗口 UIAbility | 由可视应用窗口托管 runtime | 不采用；runtime 由附着状态栏的隐藏 Ability 持有。 |
| Standalone executable | 类似 Linux/macOS 的独立 `sdk_open_app` | 不作为首期 HAP 方案；需单独确认 HarmonyOS 设备权限和分发模型。 |

## 6. 适配修改和开发清单

### P0：让 sdk_open 能被 OHOS CMake 编译（Debug/Release 基线已完成）

#### P0-1 独立工程和 target 拆分（已完成）

修改范围：

- `src/sdk_open/platform/harmony/CMakeLists.txt`
- `src/sdk_open/platform/harmony/sdk_open_harmony_host.{h,cpp}`
- `src/sdk_open/platform/harmony/sdk_open_harmony_napi.cpp`
- `src/sdk_open/platform/harmony/sdkopen_tray/build-profile.json5`

开发内容：

- 保留根 Harmony 工程和原桌面 `sdk_open_app`，独立新增不含 `main()` 的 `sdkopen_harmony`/`sdk_open_harmony_core` target。
- 让独立 HAP 只链接 host/runtime，不能链接桌面安装、service 或 `sdk_open_main.cpp`。
- 默认 `BUILD_SDK_PRIVATE=OFF`，先接 `sdk_mock_providers`；真实 provider 通过独立开关接入。
- 确认静态库使用 `POSITION_INDEPENDENT_CODE ON`，最终由 `libsdkopen_harmony.so` 统一链接。

完成标准：

- `cmake --build ... --target sdk_open_harmony_core` 成功。
- 独立 HAP CMake 阶段成功，生成 `libsdkopen_harmony.so`。
- Linux/macOS/Windows 的 `sdk_open_app` 构建命令行为不变。

#### P0-2 OHOS toolchain 和第三方依赖（首期范围已完成）

修改范围：

- `src/sdk_open/platform/harmony/CMakeLists.txt`
- `src/sdk_open/third_party/CGraph/`

开发内容：

- 固定 `OHOS_ARCH=arm64-v8a`、`LIB_ARCH=ohos-arm64-v8a`、`THIRD_PARTY_SYSTEM=ohos`。
- 首期关闭 TLS，不链接 OpenSSL；生产阶段再确定交叉编译 OpenSSL 3 或使用 Harmony crypto API，不能把 Linux `.so.3` 直接复制进 HAP。
- 验证 websocketpp/Asio/cpp-httplib 在 OHOS libc++、socket、pthread 下的编译和关闭 TLS 模式。
- 为 CGraph 增加 OHOS 编译分支；禁用 example/test/桌面工具。
- 不让头文件同步脚本误取 Linux 配置头；`opencv`、`libpng`、`libjpeg-turbo`、`freetype` 的 OHOS generated headers 必须来自 OHOS 构建。
- 产物统一输出 `third_party_repo/<module>/lib/ohos-arm64-v8a` 和 `include/ohos`，禁止复用 Linux arm64。

完成标准：

- Debug/Release 至少各有一次纯 CMake 交叉编译记录。
- `llvm-readelf -h/-d` 验证 ELF machine 为 AArch64、NEEDED 库均来自 HAP/SDK 可提供范围。
- 不依赖 `libcef`、TWAIN、systemd、桌面 OpenSSL 路径。

### P1：让 sdk_open 在 HAP 中可启动和停止（离线构建基线已完成）

#### P1-1 生命周期 host（基础 start/stop/status 已完成）

修改范围：

- `src/sdk_open/platform/harmony/sdk_open_harmony_host.{h,cpp}`
- `src/sdk_open/platform/harmony/sdk_open_harmony_napi.cpp`
- `src/sdk_open/platform/harmony/sdkopen_tray/src/main/ets/services/SdkOpenRuntime.ets`

开发内容：

- 将 `CreateSdkOpenApp()` 中 provider/config 初始化抽成可调用 host。
- 当前提供幂等的 `start(workDir, webRoot)`、`stop()`、`status()`；直接 dispatch 和异步 callback 属于后续扩展。
- `start/stop` 必须序列化，避免重复点击或失败重试造成重复监听端口。
- `SdkOpenRuntime.start/stopForExit` 映射到 host；停止时等待所有 listener/thread 和 USB 资源退出。
- 异步事件通过现有事件模型或 ArkTS callback 投递，不在 UI 线程阻塞设备/图像任务。

完成标准：

- 首次启动、重复启动、后台/前台切换、销毁重建都不产生端口泄漏或重复线程。
- `stop()` 返回后无 sdk_open worker 仍在访问已释放的 provider/path 对象。

#### P1-2 路径、配置和日志（filesDir 基线已完成）

修改范围：

- `src/sdk_open/runtime/sdk_runtime_paths.{h,cpp}`
- `src/sdk_open/runtime/sdk_config.{h,cpp}`
- `src/sdk_open/runtime/sdk_logger.cpp`
- `src/sdk_open/platform/harmony/sdkopen_tray/src/main/ets/services/SdkOpenRuntime.ets`
- `src/sdk_open/platform/harmony/sdk_open_harmony_host.*`

开发内容：

- 新增 `SdkRuntimePaths` 结构体，接收 `filesDir/cacheDir/tempDir/logFileDir`，不要由 C++ 猜 `$HOME`。
- 映射建议：
  - 永久配置/授权：`<context.filesDir>/sdk-open` 或 `preferencesDir/sdk-open`。
  - task/capture 结果：`<context.filesDir>/sdk-open/tasks`。
  - 缩略图、预览、中间文件：`<context.cacheDir>/sdk-open` 或 `tempDir/sdk-open`。
  - 日志：`<context.logFileDir>/sdk-open`。
- 保留 `SDK_OPEN_WORK_DIR`/`SDK_OPEN_LOG_DIR` 作为测试覆盖，但鸿蒙正式入口优先使用结构化 JSON 注入。
- 将 `SDK_AUTH_TOKEN`、TLS key 等敏感配置从环境变量迁移到受控配置输入，日志禁止打印 token/私钥。

完成标准：

- 非 root、无 `HOME` 环境下仍能启动。
- 所有任务、日志、授权数据落在应用沙箱；清理 cache 不会误删授权和用户结果。
- `system.info` 返回真实 OHOS/ABI/path 信息。

#### P1-3 web 资源和端口（模拟器已完成，真机待验）

修改范围：

- `src/sdk_open/runtime/sdk_app.*`
- `src/sdk_open/transport/sdk_http_server.*`
- `src/sdk_open/runtime/sdk_config.*`
- `src/sdk_open/platform/harmony/sdkopen_tray/src/main/resources/rawfile/`
- `src/sdk_open/platform/harmony/sdkopen_tray/hvigorfile.ts`

开发内容：

- 将 `src/sdk_open/frontend/admin-site/dist` 和 `demo-site/dist` 纳入 HAP rawfile 或启动时复制到 cache 的可读目录。
- 为 `web_root` 增加 Harmony 资源解析器；不能把 `resources/rawfile` 当成普通 POSIX 目录直接传给 cpp-httplib。
- 端口默认值沿用协议，但允许通过 `SdkConfig`/构建 flavor 配置；冲突时返回明确错误。
- 首期默认 bind `127.0.0.1`，禁止无意暴露到局域网；需要跨设备访问时另立权限和安全设计。
- 首期可关闭 TLS listener，待 OpenSSL/证书沙箱适配后再开启 WSS。

完成标准：

- `/healthz`、`/api/status` 和 command WS 在 HAP 内可访问。
- admin/demo 静态资源完整且不出现 404/绝对桌面路径；服务自身不加载这些页面。
- runtime 停止并重新启动后端口可重新绑定。

### P2：真实能力和 provider 适配

#### P2-1 auth/imageEnhance/OCR/OFD/graphic/recognition（已接入）

- 复用 `src/app`、`src/module` 中已有业务实现和 OHOS 三方库产物。
- Harmony target 已直接编译并使用主程序 `PrivateGraphicProvider`，覆盖
  `keep_original`、`selected_area`、`single_page`、`curved_book`、色彩模式、
  JPG/PNG 格式转换、缩放、缩略图和组合流水线。模型首次启动时从 HAP rawfile
  写入服务沙箱。
- 设备采集链路已接入真实 `PrivateDeviceProvider`；不能在 sdk_open dispatcher 中复制
  设备协议或图像算法。
- `recognition.barcode_detect` 已直接复用主程序 `PrivateRecognitionProvider` 和 OHOS
  ZXing 动态库，二维码实图识别已在模拟器验证。
- `auth.create_session`、会话校验、离线激活和 quota 复用 `PrivateAuthProvider`；Harmony
  初始化 USB Host devices 后由真实 provider 返回设备元数据。
- `image.enhance.capabilities` 和 `image.enhance.run_step` 复用 `PrivateImageEnhanceProvider`
  及 CZCV/OpenCV 图像处理库；本地增强不再返回 mock 成功结果。
- `ocr.recognize`、`ocr.get`、`ocr.extract_text` 复用 `PrivateOcrProvider`、CZOCR
  arm64 模型和任务生命周期；模型从 HAP rawfile 首次复制到服务沙箱。
- `file.convert` 的 OFD/PDF 路径复用 `PrivateOfdProvider` 和 MuPDF/libxml2/OFD 模块，
  结果写入服务工作目录。
- OCR、导出和长任务采用 task id + progress/completed 事件；不阻塞 ArkTS RPC。
- PDF/OFD/TIFF 读写使用现有 processor adapter，输出文件放应用沙箱后只返回 URI/path reference。

#### P2-2 设备和扫描

- `device.list/open/close/capture.take/video.start` 已映射到真实 Harmony
  `PrivateDeviceProvider` 和主仓 devices。
- USB 授权与控制使用 API 18+ USB Host，ISO 数据面使用 C++ USBFS reader；不把 DDK
  bulk/interrupt 契约当作 ISO 保证。
- TWAIN helper 和 SANE 桌面路径不纳入 OHOS；对应能力明确返回不可用。
- 视频帧走独立 data plane，不能复用同步 JSON command。

#### P2-3 授权和 TLS

- 复用现有 `auth.create_session`、offline activation、quota 和错误码。
- 确认 Harmony 设备唯一标识、离线授权缓存位置和密钥保护策略。
- OpenSSL 3 交叉编译或 Harmony crypto API 二选一后，再启用 WSS 和证书轮换。

### P3：打包、安装、调试和 CI

修改范围：

- `src/sdk_open/platform/harmony/sdkopen_tray/build-profile.json5`
- `src/sdk_open/platform/harmony/sdkopen_tray/hvigorfile.ts`
- `src/sdk_open/scripts/harmony/build_sdk_open_app.sh`（已完成）
- `src/sdk_open/scripts/harmony/run_sdk_open_app.sh`（已完成安装和“已启动服务”的 `--smoke` 检查；外部宿主不在本阶段范围）
- CI/Harmony workflow

开发内容：

- 将 sdk_open native target、runtime `.so`、models、fonts 和 rawfile 资源加入 HAP 产物清单。
- 对所有 native `.so` 执行 ABI/NEEDED 检查；不要依赖宿主机 `/usr/lib`。
- 新增 smoke 脚本：构建、安装、启动、healthz、command WS auth/system.ping、stop/restart、日志拉取。
- 设备调试脚本统一使用 `SDK_OPEN_HARMONY_HDC_TIMEOUT`，hdc 异常必须超时退出。
- CI 至少保留：离线静态 CMake、HAP assemble、无设备 bridge contract；有设备时再执行真实 WS/device/UI smoke。

## 7. 建议新增的测试矩阵

| 层级 | 用例 | 通过条件 |
| --- | --- | --- |
| CMake | `sdk_open_harmony_core` Debug/Release | arm64-v8a 编译通过，无桌面库依赖。 |
| ELF | `readelf -h/-d` | AArch64，依赖均已打包或由系统提供。 |
| Host | start/stop/restart | 无重复 listener、无线程泄漏。 |
| Path | 无 HOME、沙箱目录注入 | 配置、日志、task、cache 路径正确。 |
| Protocol | `/healthz`、`system.ping`、`auth.create_session` | response/error code 与桌面协议一致。 |
| HTTP/WS | admin/demo、command/video WS | 默认回环端口可访问，断线可恢复。 |
| Assets | rawfile web/model/font | HTTP 资源和模型不出现 404/路径越界。 |
| Capability | 真实 device/auth/imageEnhance/OCR/OFD/graphic/recognition；SANE/TWAIN 不可用 | 真实能力产生结果；SANE/TWAIN 返回明确不可用错误。 |
| Backend | graphic | 主程序 provider；页面裁切、曲面展平、色彩、转换和组合处理均产生真实输出。 |
| Backend | recognition | 主程序 ZXing provider；二维码实图识别返回真实格式、文本和定位点。 |
| Backend | OCR/PDF/OFD | 调用 CZOCR、MuPDF/OFD 真实 adapter，模型/依赖来自 HAP。 |
| Device | device.list/open/capture/video | 真实 Harmony provider 或明确 `requiresHarmonyCapability`。 |
| Lifecycle | ServiceExtension 重建 | session、端口和 task 状态符合设计。 |
| Security | bind、TLS、token、日志 | 不外网暴露，不泄漏敏感信息。 |

## 8. 不应采用的捷径

- 不把 `sdk_open_app` 的 `main()`、Linux signal wait、Windows service、macOS LaunchAgent 直接塞进 `libsdkopen_harmony.so`。
- 不把 Linux `libssl.so.3`、Linux arm64 三方库或桌面 `czcv/czocr` 产物复制进 HAP。
- 不在 ArkTS 中重写 capture、image.process、OCR、PDF/OFD writer。
- 不用空文件、假成功或固定 mock 数据掩盖未接入的 provider；应返回明确错误和 capability 标记。
- 不把 `/data/local/tmp` 当用户可选择目录；调试样张应通过 hdc 推送后由应用复制到自身沙箱。
- 不为了让 UI 通过而修改共享 `views/src` 业务流程；优先修复 native bridge、路径或 adapter。

## 9. 首批开发顺序和交付物

建议按以下顺序拆分任务：

1. **OHOS core compile**：新增 Harmony sdk_open target、关闭 private provider/TLS，产出 `sdk_open_harmony_core`。
2. **Embedded host**：抽离 `SdkRuntimeHost`，让 `libsdkopen_harmony.so` 可 start/stop/status。
3. **Sandbox paths**：完成 Context 路径注入和 sdk_open 日志/task 目录迁移。
4. **Loopback protocol smoke**：healthz、status、command WS、auth/session、system methods。
5. **Static assets**：admin/demo rawfile/cache 资源和 `web_root` 适配。
6. **Graphic regression**：`image.process_page`、色彩、单图转换、缩略图和组合处理真实输出。
7. **Real backend adapters**：device、OCR、PDF/OFD 已接入；正式真机链路待验收。
8. **TLS and production security**：证书、密钥、权限、端口暴露策略。
9. **HAP/设备 CI**：签名、安装、hdc smoke、日志归档和回归矩阵。

每一步都要保留 Linux/macOS/Windows 的原有构建和测试结果，Harmony 适配不得改变桌面协议或 provider 行为。

## 10. Definition of Done

只有同时满足以下条件，才能认为“sdk_open 鸿蒙编译和运行适配完成”：

- OHOS `arm64-v8a` Debug/Release native target 可重复构建。
- `.app` 只包含 `sdkopen_tray` entry HAP，bundle 为 `com.czur.opensdk`。
- 控制入口“启动服务”调用 start API 并等待 runtime status 为 running；“关闭服务”调用
  stop API，并确认 HTTP 端口不再可访问。打开控制页或刷新状态不会隐式启动服务。
- 所有 sdk_open 数据、日志、临时文件和模型访问遵守 Harmony 沙箱路径。
- `/healthz`、`/api/status`、command WS、video WS 的协议和错误码保持兼容。
- mock provider 回归通过，真实接入的 graphic/OCR/document/device 能力均有对应 adapter 和测试。
- 未接入能力明确返回错误/capability，不存在假成功、空文件或 UI 卡死。
- native 依赖通过 ABI/NEEDED 检查，HAP 不依赖宿主 Linux/macOS/Windows 文件。
- 有无设备两套验证都可执行：离线静态/bridge regression，以及 hdc 真机/模拟器 smoke。
- 文档、构建脚本、签名配置和 CI 产物路径已同步，后续开发者无需猜测 SDK、CMake、hdc 或日志位置。
- 正式签名/profile 与 `com.czur.opensdk` 匹配，并在目标 2in1 真机验证
  DeskTopExtensionKit 托盘图标、弹窗、菜单、后台运行及退出行为；不依赖 AppService ACL。
