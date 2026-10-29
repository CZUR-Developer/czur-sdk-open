# SDK Open HarmonyOS 托盘应用

该目录是 `sdk_open` 的独立 HarmonyOS 2in1 工程。当前正式架构只有一个
`sdkopen_tray` entry HAP，bundle name 为 `com.czur.opensdk`：

- `SdkOpenLauncherAbility` 是外部启动入口，负责注册桌面状态栏图标；
- `SdkOpenTrayAbility` 以 `ATTACH_TO_STATUS_BAR_ITEM` 隐藏运行，持有 native runtime、
  USB Host 和设备生命周期；
- `SdkOpenStatusBarAbility`（`statusBarView`）提供托盘弹窗，Demo 通过本机 HTTP 打开；
- 退出托盘时先停止 runtime 和设备，再释放 USB Host；
- 不再包含 `sdkopen` feature HAP 或 `AppServiceExtensionAbility`。

因此该形态不申请 `ohos.permission.SUPPORT_APP_SERVICE_EXTENSION`，也不依赖
`ohos.permission.START_INVISIBLE_ABILITY`。真机安装仍需要与 `com.czur.opensdk` 匹配的
证书和 profile。旧 `com.czur.sdkopen` 的 profile、应用沙箱和本地数据不会随包名迁移。

该工程不参与 CapEditor 根 Hvigor 工程、桌面端 `build.sh` 或
`src/sdk_open/scripts/package-sdkopen.sh` 的默认构建。

## 能力边界

native runtime 在托盘进程内提供 admin、demo/asset HTTP 与 command/video WebSocket。
Harmony 复用主程序 private provider 和 OHOS 底层库：

- device 使用主仓 USB Host/C++ UVC 后端，预览保留原始 MJPEG；
- graphic、recognition、auth、imageEnhance、OCR、OFD 使用现有真实 provider；
- Harmony 不编入桌面 SANE/TWAIN 后端；
- TLS 当前关闭，服务默认只监听 `127.0.0.1`。

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
密码或本机签名路径。

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

工作目录和日志位于应用 `filesDir` / `logFileDir` 对应的 SDK Open 沙箱目录。模型和
`sdkopen_device_config.conf` 在首次启动或版本变化时从 rawfile 写入运行目录。

## 迁移与验收状态

旧双 HAP/AppService 架构曾在 HarmonyOS 6.0.0(20) arm64 模拟器完成托盘、HTTP、
WebSocket 和退出清理验证；这些结果只属于历史架构，不能作为当前单 HAP 方案的验收证据。

当前单 HAP、`com.czur.opensdk` Release 整包已构建成功，tray HAP 内 30 个 native 库以及
包名、权限、Demo/模型/配置资源检查通过。托盘生命周期本地回归 19/19 通过，覆盖冷启动
命令、同进程 Ability 重建、资源准备中退出、文件部分写入，以及 native/USB 清理失败后的
重试。执行命令：

```bash
python3 -m unittest -v src/sdk_open/test/harmony_lifecycle_contract_test.py
```

当前机器只有 2in1 模拟器配置，未安装对应系统镜像，因此本轮没有模拟器运行通过结论。
仍需完成：

1. 新包名签名/profile 的 2in1 真机安装；
2. 托盘启动、退出、后台驻留和 USB 资源释放；
3. ET 1536×1152 预览、厂商拍照、浏览器重连；
4. 20 分钟正式链路长测和慢消费者验证。

改包名不会迁移旧应用沙箱。如需保留旧授权或配置，应设计显式、安全的数据迁移，不能依赖
覆盖安装。

旧 `com.czur.sdkopen` 可以与新包并存，但两个 runtime 使用相同的本机端口。启动新包前
必须先从旧托盘关闭旧 runtime；不需要卸载旧包，也不要为解决端口冲突删除旧沙箱数据。

摄像头集成和完整适配状态见：

- [`摄像头集成记录`](../../doc/sdk_open_harmony_camera_integration.md)
- [`Harmony 适配计划`](../../doc/sdk_open_harmony_adaptation_plan.md)
