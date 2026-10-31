# SDK Open HarmonyOS 托盘应用

该目录是 `sdk_open` 的独立 HarmonyOS 2in1 工程。当前正式架构只有一个
`sdkopen_tray` entry HAP，bundle name 为 `com.czur.opensdk`：

- `SdkOpenLauncherAbility` 是外部启动入口，负责注册桌面状态栏图标；
- `SdkOpenTrayAbility` 以 `ATTACH_TO_STATUS_BAR_ITEM` 隐藏运行，持有 native runtime、
  USB Host 和设备生命周期；
- `SdkOpenStatusBarAbility`（`statusBarView`）提供托盘弹窗，支持打开本机管理站点和 HTTP Demo；
- 退出托盘时先停止 runtime 和设备，再释放 USB Host；
- 不再包含 `sdkopen` feature HAP 或 `AppServiceExtensionAbility`。

因此该形态不申请 `ohos.permission.SUPPORT_APP_SERVICE_EXTENSION`，也不依赖
`ohos.permission.START_INVISIBLE_ABILITY`。真机安装仍需要与 `com.czur.opensdk` 匹配的
证书和 profile。旧 `com.czur.sdkopen` 的 profile、应用沙箱和本地数据不会随包名迁移。

该工程不参与 CapEditor 根 Hvigor 工程、桌面端 `build.sh` 或主仓库桌面 SDK Open
打包流程的默认构建。主仓库正式入口为 `tools/harmony/build_sdk_open_app.sh`。

## 能力边界

native runtime 在托盘进程内提供 admin、demo/asset HTTP 与 command/video WebSocket。
Harmony 复用主仓库 `src/sdk_private` 中的闭源 provider 和 OHOS 底层库：

- device 使用主仓 USB Host/C++ UVC 后端，预览保留原始 MJPEG；
- graphic、recognition、auth、imageEnhance、OCR、OFD 使用现有真实 provider；
- Harmony 不编入桌面 SANE/TWAIN 后端；
- Harmony 默认启用 TLS，同时保留 HTTP/WS，服务只监听 `127.0.0.1`。

runtime 由托盘主体直接持有，弹窗只转发命令和查询状态，不加载 native addon。资源和模型
通过异步文件 API 分块复制；启动、停止和退出串行执行，退出会取消尚未进入 native 的
资源准备。冷启动命令和同进程 Ability 重建均保留启停能力。

USB Host adapter 在 devices 初始化前安装。托盘退出时按 runtime/providers、devices、
USB pipe/bridge 的顺序清理。USB Host bridge 发生终止性超时后会封闭当前 N-API env，
需要完整退出并重新启动托盘进程。

## 环境

默认使用 DevEco Studio 自带工具：

```text
/Applications/DevEco-Studio.app/Contents/jbr/Contents/Home
/Applications/DevEco-Studio.app/Contents/tools/node/bin
/Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw
/Applications/DevEco-Studio.app/Contents/sdk
```

安装位置不同时可通过 `JAVA_HOME`、`NODE_BIN`、`HVIGOR`、`OHPM` 和
`DEVECO_SDK_HOME` 覆盖。工程 compile/target SDK 为 `6.1.1(24)`，最低兼容
`6.0.2(22)`；native 显式传入 `OHOS_COMPATIBLE_SDK_VERSION=22`。

## 构建

在主仓根目录执行：

```bash
# 仅当 OpenSSL 3 的 OHOS 产物缺失时执行
src/sdk_open/scripts/harmony/build_sdk_open_openssl.sh

# 构建单 HAP 的完整 .app
src/sdk_open/scripts/harmony/build_sdk_open_app.sh --clean --release
```

默认复用 `src/sdk_open/web` 已生成的 Demo/Admin。需要重建网页时：

```bash
src/sdk_open/scripts/harmony/build_sdk_open_app.sh --clean --build-web --release
```

只构建 tray HAP：

```bash
src/sdk_open/scripts/harmony/build_sdk_open_tray.sh --clean --release
```

产物路径：

```text
src/sdk_open/platform/harmony/build/outputs/default/harmony-default-unsigned.app
src/sdk_open/platform/harmony/sdkopen_tray/build/default/outputs/default/sdkopen_tray-default-unsigned.hap
```

构建脚本会对 tray HAP 内 native 库执行 AArch64、ELF NEEDED 和 JPEG 符号隔离检查。
网页、CZCV/CZOCR 模型和设备配置由 tray 模块的 Hvigor 任务同步到
`sdkopen_tray/src/main/resources/rawfile`；这些生成目录不提交版本库。

默认产物未签名。真机安装前，需要在这个独立工程中为 `com.czur.opensdk` 配置
`signingConfigs`。不要复用旧包名 `com.czur.sdkopen` 的 profile，也不要提交 `.p12`、
密码或本机签名路径。配置签名后，构建脚本校验模块 HAP 的签名，将已签名 HAP
放入 `.app`，并再次提取验签；未配置签名时只保留 unsigned 产物。

## 安装与运行

```bash
HDC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/hdc
"$HDC" list targets -v

src/sdk_open/scripts/harmony/run_sdk_open_app.sh \
  --app <signed-com.czur.opensdk.app> \
  --target <target>
```

脚本安装 `.app` 后启动主入口：

```bash
"$HDC" -t <target> shell \
  aa start -b com.czur.opensdk -m sdkopen_tray -a SdkOpenLauncherAbility
```

启动入口只注册托盘项，不会自动启动 runtime。请在托盘右键菜单或左键弹窗中点击
“启动服务”；隐藏的 `SdkOpenTrayAbility` 随后持有 runtime。运行日志应包含：


```text
SdkOpenRuntime ... SDK Open tray runtime started
```

协议冒烟：

```bash
src/sdk_open/scripts/harmony/run_sdk_open_app.sh \
  --app <signed-com.czur.opensdk.app> \
  --target <target> \
  --skip-install --smoke
```

`--smoke` 只检查已由托盘手动启动的 runtime，检查主进程 `com.czur.opensdk`，不查找
`:appService` 子进程，并验证 health/status/demo/asset。常用日志过滤：

```bash
"$HDC" -t <target> shell hilog -x | rg -i 'SdkOpenRuntime|sdk_open'
```

当前本机端口：

| 服务 | 地址 |
| --- | --- |
| admin HTTP | `http://127.0.0.1:17080/` |
| demo HTTP | `http://127.0.0.1:17081/` |
| asset HTTP | `http://127.0.0.1:17082/` |
| command WebSocket | `ws://127.0.0.1:17090/` |
| video WebSocket | `ws://127.0.0.1:17091/` |
| asset HTTPS | `https://sdk-runtime.localhost:18082/` |
| command WSS | `wss://sdk-runtime.localhost:18090/` |
| video WSS | `wss://sdk-runtime.localhost:18091/` |

HTTP Demo 返回的采集图片、缩略图和文档资源统一使用 `http://127.0.0.1:17082`，与
`v0.1.0` 的前端访问方式一致；HTTPS Asset/WSS listener 仅作为可选的外部协议入口，不会被
HTTP Demo 的资源响应自动切换使用。

首次启动时 runtime 在应用沙箱生成本机根 CA 与服务证书，私钥留在沙箱。先点击托盘
“安装本机 HTTPS 证书”，在系统窗口确认当前用户的 CA 安装。Demo 统一从“打开 HTTP Demo（调试）”进入，
不再提供 HTTPS Demo 站点入口。证书安装由单独的可见 UIAbility 发起，popup 只启动该窗口；只需普通
`ohos.permission.ACCESS_CERT_MANAGER`，不调用需要额外 ACL 的直接安装接口。服务证书 SAN
包含 `sdk-runtime.localhost`、`localhost` 和 `127.0.0.1`，不覆盖局域网地址。
HTTP Demo 使用 WS。卸载或清空应用数据会重新生成根 CA，需重新
安装新证书并在系统中清理旧证书。浏览器信任必须实测，`curl -k` 成功仅证明 TLS listener 可用。

工作目录和日志位于应用 `filesDir` / `logFileDir` 对应的 SDK Open 沙箱目录。模型和
`sdkopen_device_config.conf` 在首次启动或版本变化时从 rawfile 写入运行目录。

## OCR 模型和原生库

runtime 启动后会把 `resources/rawfile/sdkopen_web` 复制到托盘应用沙箱，并在首次启动时把
`resources/rawfile/sdkopen_czcv_model` 写入 `runtime/czcv_model`、把
`resources/rawfile/sdkopen_czocr_model` 写入 `runtime/models`，然后启动内嵌 runtime。
Harmony 服务实际启用 `auth`、`imageEnhance`、`ocr`、`ofd` provider；OCR 的通用模型文件
优先来自 `third_party_repo/czocr/models/x86-64` 下全部 `.model`、`.dat` 和
`ppocr_keys_v1.txt`（如果存在），缺少的文件由 `models/windows64` 的完整集补齐；
授权文件沿用 `models/loongarch64/license.lic`。旧双 HAP 宿主在 2026-10-30 的最近一次验证覆盖 x86-64 目录的
40 个模型、13 个 dat 字典及 keys 文件，安装包和设备沙箱的 54 个文件均逐一校验 SHA-256 一致。
`3_1_1.model` 同样明确使用用户提供的 x86-64 文件。
这些是模型数据，运行库仍然只使用 `lib/ohos`。旧 loongarch64 模型集的英文字典和
模型版本较旧，不能直接替代完整模型集。
`resources/ocr/fonts/SourceHanSansCN-Normal.otf` 单独作为 rawfile
释放到 `runtime/ocr_fonts`，通过 `SDK_OCR_FONT_DIR` 供 PDF 文字层使用。
`third_party_repo/czocr/lib/ohos/libczcv_ocr.so` 与 `libz.so` 放在 HAP 标准 native
目录，不向应用数据目录复制可执行库。鸿蒙宿主通过链接包装
`czocr_init_in_dev_stage`，用 CZOCR 自身的 `czocr_load_library` 从 native 目录建立
API 分发表，再以原参数调用同一算法的 `czcv_ocr_init_doc`。后续识别仍使用原 API，
鉴权校验保持原有流程。这一适配仅作用于 SDK Open 鸿蒙宿主，依赖当前 OHOS 库导出的
上述符号；更新库时需回归文字提取和 OCR 导出。构建时按排序后的模型文件名与内容
生成 SHA-256 版本资源 `sdkopen_czocr_model_version.txt`；服务检测到版本变化后会重新
释放模型，无需手工修改缓存标记。
构建检查覆盖模型、字体及原生依赖，缺失资源时拒绝生成通过结果。
2026-10-30 16:11 更新 OHOS 算法库后，显式 `language=en` 的英文文字提取和 PDF
导出已在旧双 HAP 宿主的模拟器实测通过，PDF 文字层核验正确，原有 `ncnn::Gemm::forward` 崩溃未复现。
通过版本的算法库 Build ID 为 `fdcb847a5d2f1d0ef08d8640a46d34ee14e39958`。
旧库曾在此路径崩溃，单线程、关闭 OpenMP 亲和性和保留未 strip 库的尝试均未作为修复保留。
自动/中文模式与显式英文路径需要分别验证，详情见
`../../doc/ocr/sdk_open_harmony_ocr_x86_repack_20261030.md`。
图像增强使用 arm64 CZCV 库。Harmony 不提供桌面 SANE/TWAIN 后端，
对应请求会返回 `ProviderUnavailable`，不会伪装成扫描成功。

## 迁移与验收状态

旧双 HAP/AppService 架构曾在 HarmonyOS 6.0.0(20) arm64 模拟器完成托盘、HTTP、
WebSocket 和退出清理验证；这些结果只属于历史架构，不能作为当前单 HAP 方案的验收证据。

当前单 HAP、`com.czur.opensdk` Release 整包已构建成功，tray HAP 内 30 个 native 库以及
包名、权限、Demo/模型/配置资源检查通过。托盘生命周期本地回归 21/21 通过，覆盖冷启动
命令、同进程 Ability 重建、资源准备中退出、文件部分写入，以及 native/USB 清理失败后的
重试。执行命令：

```bash
python3 -m unittest -v src/sdk_open/test/harmony_lifecycle_contract_test.py
```

2026-10-29 已使用匹配 `com.czur.opensdk` 的 debug profile 在 HarmonyOS 6.0.0.130
2in1 真机安装运行。已修复首个 HTTP 请求引起的 native crash：cpp-httplib 的
`CPPHTTPLIB_OPENSSL_SUPPORT` 宏在 runtime/providers 中不一致，导致 `Request` 布局不同。
Harmony 各承载目标现统一此宏。HTTP 健康接口、Demo、100 次并发请求、托盘停止/再启动、
真机浏览器 HTTP Demo，以及 HTTPS listener 已验证。完整结果见摄像头集成记录。

仍需以正式 Demo 授权后的采集链路验收 ET 1536×1152 预览、厂商拍照、浏览器重连、
20 分钟长测和慢消费者；独立 devices 测试不代替此项。

改包名不会迁移旧应用沙箱。如需保留旧授权或配置，应设计显式、安全的数据迁移，不能依赖
覆盖安装。

旧 `com.czur.sdkopen` 可以与新包并存，但两个 runtime 使用相同的本机端口。启动新包前
必须先从旧托盘关闭旧 runtime；不需要卸载旧包，也不要为解决端口冲突删除旧沙箱数据。

摄像头集成和完整适配状态见：

- [`摄像头集成记录`](../../doc/sdk_open_harmony_camera_integration.md)
- [`Harmony 适配计划`](../../doc/sdk_open_harmony_adaptation_plan.md)
