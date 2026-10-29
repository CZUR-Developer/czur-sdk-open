# SDK Open HarmonyOS Service

该目录是 `sdk_open` 的独立 HarmonyOS 工程，正式运行形态是“托盘控制无 UI 服务”：

- `sdkopen_tray` 是 2in1 唯一 entry HAP，提供启动、停止和状态显示入口；
- `sdkopen` 是随安装交付的 feature HAP，只声明 `AppServiceExtensionAbility`，不注册
  页面或 ArkWeb 窗口；
- 用户在托盘入口中启动服务时创建 runtime，在托盘入口中关闭服务时销毁 runtime；
- 不承诺安装即启动、开机恢复或系统级永久保活。

控制入口使用 `@kit.DeskTopExtensionKit` 的真实系统托盘：

- 启动 `SdkOpenLauncherAbility` 注册状态栏图标，创建依附托盘的隐藏
  `SdkOpenTrayAbility`，随后结束启动入口，不保留普通控制窗口或 Dock 任务。
- 在右下角状态栏的折叠区域找到 SDK Open 的 S 图标；左键打开
  `StatusBarViewExtensionAbility` 弹窗，右键提供“启动服务／关闭服务”和系统“退出”。
- 弹窗在独立进程运行，通过 Want 将启停命令交给隐藏的 UIAbility；状态通过本机
  HTTP 查询。服务仍由 `AppServiceExtensionAbility` 托管。
- 打开托盘、打开弹窗或刷新状态均不自动启动服务。
- 系统“退出”经 `AbilityStage.onPrepareTerminationAsync()` 先向 AppService 发送私有
  prepare-stop RPC，等待设备和 USB 清理完成，再停止 AppService 并结束托盘；声明
  `PREPARE_APP_TERMINATE` 以接收正常退出回调。

2026-10-29 模拟器实测：双模块整包已安装，系统托盘弹窗和 Demo 网页可打开，服务启动、
停止、重启及 HTTP/命令 WS 检查通过。已修复旧包缺少 FreeImage/FreeType/libzip
SONAME 别名、误打包 Linux ARM64 OpenSSL/ICU 的问题；29 个 native 库通过 HAP
ABI/NEEDED 检查。OpenSSL 3 使用仓库 LFS 源码补齐到 `lib/ohos`，其他已编译的
OHOS 图像、OCR、文档三方库继续复用。视频 WS 的认证会话及帧传输、真实能力完整
回归和正式签名真机验证仍需单独验收。

该工程不属于仓库根部 CapEditor Harmony 工程，也不会参与桌面端 `build.sh`、
`src/sdk_open/scripts/package-sdkopen.sh` 或根 Hvigor 工程的默认构建。

当前基线用于验证：

- `sdk_open_runtime` 在 OHOS `arm64-v8a` 上编译、链接和运行。
- AppServiceExtension 生命周期通过 N-API 启停内嵌 runtime。
- runtime 在后台提供 admin、demo/asset HTTP 和 command/video WebSocket 服务。
- demo、admin、HTTP 和 WebSocket 服务可完成协议联调。
- graphic 直接复用主程序 `PrivateGraphicProvider`，已接入页面处理、曲面展平、
  四点裁切、色彩模式、JPG/PNG 转换与缩放、缩略图和组合 `image.process` 流水线。
- recognition 直接复用主程序 ZXing Provider，已接入真实条码/二维码识别。
- device、auth、imageEnhance、OCR、OFD 复用主程序 private provider 和对应 OHOS 底层库；
  graphic、recognition 也继续复用真实 provider。device 使用主仓 USB Host/C++ UVC 后端，
  Harmony 不编入桌面设备、SANE、TWAIN 后端。

当前真实 graphic provider 覆盖 `image.process_page`、`image.apply_color_mode`、
`image.process`、单图 `file.convert` 和 capture 流水线内部的缩略图生成。USB UVC 设备
已经接入真实 provider，保留原始 MJPEG，仅在寻边、翻页检测或显式 BGR 输出时解码。
桌面 SANE/TWAIN 扫描和 TLS 尚未接入；本地 auth 会话、图像增强、OCR 识别/文本提取
和 OFD/PDF 文档处理已经接入实际 provider。在线增强是否可用取决于请求是否配置可访问
的服务端点。正式托盘设备采集的真机验收状态见
[`摄像头集成记录`](../../doc/sdk_open_harmony_camera_integration.md)。

`AppServiceExtensionAbility` 不能由 `hdc shell aa start` 直接启动。`sdkopen_tray`
通过 `UIAbilityContext.startAppServiceExtensionAbility()`、
`stopAppServiceExtensionAbility()` 和本机 HTTP 状态查询控制服务生命周期；签名/profile 必须
允许 `START_INVISIBLE_ABILITY`。如果以后拆成不同 bundle，还必须在服务端按签名
profile 的 `app-identifier` 配置 `appIdentifierAllowList`，不能填写 bundle name。

## 环境

默认使用 DevEco Studio 自带的 JDK、Node、Hvigor 和 SDK：

```text
/Applications/DevEco-Studio.app/Contents/jbr/Contents/Home
/Applications/DevEco-Studio.app/Contents/tools/node/bin
/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw
/Applications/DevEco-Studio.app/Contents/sdk
```

如果安装位置不同，可通过 `JAVA_HOME`、`NODE_BIN`、`HVIGOR` 和
`DEVECO_SDK_HOME` 覆盖。

当前工程使用 Hvigor modelVersion `6.1.1`、compile/target SDK `6.1.1(24)`，
最低兼容 `6.0.2(22)`；native 显式传入 `OHOS_COMPATIBLE_SDK_VERSION=22`。

## 构建

在仓库根目录执行：

```bash
# 仅当 OpenSSL 3 的 lib/ohos 产物缺失时补齐；已存在时自动复用。
# 若源码归档尚为 LFS 指针，先在 third_party_repo 中执行：
# git lfs pull --include=src/openssl-OpenSSL_3_0_16.zip --exclude=''
src/sdk_open/scripts/harmony/build_sdk_open_openssl.sh
src/sdk_open/scripts/harmony/build_sdk_open_app.sh --clean
```

OpenSSL 输出为 `third_party_repo/openssl3/lib/ohos/libssl.so.3`、`libcrypto.so.3`，
头文件位于 `openssl3/include/ohos`。该补齐脚本只使用仓库源码归档，不全量重编其他三方库。
应用构建完成后自动检查 HAP 内 ELF 架构和全部 NEEDED 文件名；缺库、SONAME 别名缺失
或误链接 Linux ABI 都会使检查失败。

默认直接使用 `src/sdk_open/web` 中已经生成的 demo/admin 资源。需要重新构建网页时：

```bash
src/sdk_open/scripts/harmony/build_sdk_open_app.sh --clean --build-web
```

Release 编译：

```bash
src/sdk_open/scripts/harmony/build_sdk_open_app.sh --clean --release
```

应用包和模块输出位置：

```text
src/sdk_open/platform/harmony/build/outputs/default/harmony-default-unsigned.app
src/sdk_open/platform/harmony/sdkopen/build/default/outputs/default/sdkopen-default-unsigned.hap
src/sdk_open/platform/harmony/sdkopen_tray/build/default/outputs/default/sdkopen_tray-default-unsigned.hap
```

该命令默认生成未签名 `.app` 和 HAP。真机安装前应在 DevEco Studio 中为独立工程配置
`signingConfigs`，不要复用或改写 CapEditor 主工程的签名配置。

## 运行

连接设备后可使用 DevEco SDK 自带的 `hdc`：

```bash
HDC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/hdc
"$HDC" list targets -v
src/sdk_open/scripts/harmony/run_sdk_open_app.sh --app <signed-sdkopen.app> --target <target>

# 在打开的控制入口点击“启动服务”后验证 health/status/demo/asset：
src/sdk_open/scripts/harmony/run_sdk_open_app.sh --app <signed-sdkopen.app> --target <target> \
  --skip-install --smoke

# 安装脚本解压 .app 后，使用 bm 对 entry 和 feature HAP 进行整包安装。
# 打开控制入口并查看服务日志：
"$HDC" -t <target> shell aa start -b com.czur.sdkopen -m sdkopen_tray -a SdkOpenLauncherAbility
"$HDC" -t <target> shell hilog -x | rg -i 'SdkOpenService|sdk_open'
```

不带 `--smoke` 时，脚本安装整包并打开托盘控制入口，但不会自动启动服务。`--smoke`
只检查已经由用户在托盘入口启动的服务。托盘和服务都属于独立 sdk_open 工程，不修改
CapEditor 主工程入口。

若旧版 `sdkopen` entry 已安装，系统不允许直接覆盖为 feature（报 `install entry already
exist`）。可先执行 `hdc -t <target> shell bm uninstall -n com.czur.sdkopen -k` 保留用户数据，
再运行安装脚本；不要省略 `-k`，以免清除鉴权配置。常规升级不需要卸载。
控制页通过本机 HTTP 查询已有 runtime；查询状态不会连接并隐式启动服务。
冒烟检查不要求 Demo 浏览器已建立 WebSocket，WebSocket 交互需要另行在 Demo 中验证。

服务启动后会把 `resources/rawfile/sdkopen_web` 复制到服务沙箱，并在首次启动时把
`resources/rawfile/sdkopen_czcv_model` 写入 `runtime/czcv_model`、把
`resources/rawfile/sdkopen_czocr_model` 写入 `runtime/models`，然后启动内嵌 runtime。
每次启动同时把 `sdkopen_device_config.conf` 写入 `runtime/configs/device_config_compact.conf`，
并在异步启动 native runtime 前安装 USB Host 适配器。Harmony 服务实际启用
`device`、`auth`、`imageEnhance`、`ocr`、`ofd` provider；OCR 的通用模型文件
来自 `third_party_repo/czocr/models/x86-64`，其中 `libczcv_ocr.so` 替换为
`third_party_repo/czocr/lib/ohos/libczcv_ocr.so`，不打包 `libczcv_ocr_naive.so`。
CMake 原生输出与 HAP rawfile 使用相同布局；模型版本更新后，服务会重新写入沙箱模型并删除旧 naive 库。
图像增强使用 arm64 CZCV 库。Harmony 不提供桌面 SANE/TWAIN 后端，
对应请求会返回 `ProviderUnavailable`，不会伪装成扫描成功。
服务自身不会打开页面；demo 静态资源只通过以下 HTTP 地址提供，可由独立客户端或
浏览器在端口转发后访问：

```text
http://127.0.0.1:17081/
```

当前端口：

| 服务 | 地址 |
| --- | --- |
| admin HTTP | `http://127.0.0.1:17080/` |
| demo HTTP | `http://127.0.0.1:17081/` |
| asset HTTP | `http://127.0.0.1:17082/` |
| command WebSocket | `ws://127.0.0.1:17090/` |
| video WebSocket | `ws://127.0.0.1:17091/` |

工作目录和日志分别位于应用 `filesDir` 下的：

```text
sdk-open/runtime
sdk-open/runtime/logs
sdk-open/runtime/czcv_model
```

## 工程边界

- 独立工程不加入仓库根 `build-profile.json5`。
- 默认关闭完整 `BUILD_SDK_PRIVATE`、`SDK_OPEN_USE_PRIVATE_PROVIDER` 和 TLS；Harmony
  target 只选择性编入主程序真实 provider 及其处理器，不带桌面设备/扫描模块。
- HAP 链接 `sdk_open_runtime`、mock bundle 容器、主程序 auth/imageEnhance/OCR/OFD/
  device/graphic/recognition provider，以及对应的 OpenCV/CZCV/CZOCR/MuPDF/OFD OHOS 运行库。
- `sdkopen` feature 模块只声明 `SdkOpenServiceExtension`，不得添加页面或 ArkWeb；
  `sdkopen_tray` 是唯一 entry 模块，只负责服务控制和状态展示。
- 网页资源和 CZCV 模型由模块 `hvigorfile.ts` 在构建时同步，生成目录不提交版本库。
- CGraph 仅在 `__OHOS__` 下跳过 OHOS 不提供的 `pthread_setaffinity_np`。

完整的历史、设计取舍、剩余适配项和验收矩阵见
[`src/sdk_open/doc/sdk_open_harmony_adaptation_plan.md`](../../doc/sdk_open_harmony_adaptation_plan.md)。
