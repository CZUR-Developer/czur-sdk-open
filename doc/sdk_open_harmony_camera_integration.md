# Harmony 托盘摄像头集成记录

日期：2026-10-29。此记录描述新目录下的正式 SDK 集成，与主仓独立 devices 真机验收分开。

## 基线与目录

- 主仓先快进到 `25267f363`，再通过 merge commit `f8b9c8f40` 合入摄像头分支 `fe2eab7f9`。
- SDK 修复从子模块 `f08675b` 开始；三方库保持目标分支的 `c4f5f22`。
- `platform/harmony` 负责 N-API、托盘 runtime 和 HAP；`providers/private` 负责真实设备接入；
  `transport` 负责现有视频协议；主仓 `src/module/devices` 继续负责 USB/UVC。
- 不恢复迁出前的 `sdk_open_harmony` 或 `src/sdk_private`，主仓构建入口继续转发到子模块。

## 实现边界

正式托盘 runtime 使用 API 18+ USB Host 授权、控制和 Probe/Commit，C++ USBFS reader 接收 ISO，
devices 组装完整 JPEG。ET 的 `0x82` 是 ISO endpoint，未把 DDK bulk/interrupt 契约当作
ISO 保证，也未加入 CameraService 回退。

Harmony factory 和 host 使用真实 `PrivateDeviceProvider`，保留其余已有 provider 组合。
devices 始终 `decodeFrame=false`、`deliverNativeMjpeg=true`。预览默认直接转交原 JPEG；
仅显式 BGR 输出、实时寻边或翻页检测需要时，provider 用 OpenCV 解码。厂商拍照和 still
pair 仍走已有可靠采集流水线。SDK 的 `stream.frame_meta` 和 JPEG binary 格式不变。

视频 WS 每连接最多保留一组已提交 metadata/binary 和一组最新待发帧。慢连接期间覆盖待发
预览，单 timer 等待 websocketpp buffered amount 清空，不逐帧积累 `io_service.post`。
binary 发送失败会关闭连接，防止 metadata 和下一帧错配。预览丢弃计数和发送失败计数可从
状态接口的 `ws.video.previewFramesDropped`、`ws.video.sendFailures` 读取。拍照结果不走
这个预览槽。

托盘 runtime 先复制配置到 `${workDir}/configs/device_config_compact.conf` 并安装 USB Host adapter，
再异步执行 native start。native stop 同样在 async work 中执行；顺序为 runtime/providers
停止、devices release、ArkTS pipe 清理。同步 status 使用原子状态，不等待 USB 生命周期锁。
当前单 HAP 架构由隐藏的 `SdkOpenTrayAbility` 持有 runtime 和 USB Host；托盘退出时等待
异步清理完成后再结束 Ability。它不使用 AppServiceExtension。

模型与网页使用异步资源读取和分块文件写入，避免首次启动阻塞托盘主线程；退出会取消
尚未进入 native 的资源准备。冷、热启动均处理托盘命令，普通 Ability 销毁使用可重启的
stop，应用正常退出才进入最终 stopForExit。

USB Host bridge 的请求超时会封闭当前 N-API env，以隔离仍可能产生副作用的旧请求。这是
既有 devices 安全策略：发生该终止性超时后须完整退出并重新启动宿主进程，runtime 内部
stop/start 不重置 bridge；不能宣称所有异常都能在同一 env 内自动恢复。

## 构建与配置

本机工具链为 DevEco SDK `6.1.1.125` / API 24。工程 modelVersion 使用 `6.1.1`，
compile/target 使用 `6.1.1(24)`，最低兼容 API 22；native 显式传入
`OHOS_COMPATIBLE_SDK_VERSION=22`。原有 `26.0.0` 字段在当前 Hvigor 中无法解析。
设备、provider、host 的编译命令均为 C++11、`-O2`、ARM64，保留 USB 线程 QoS。

从主仓根目录构建：

```bash
src/sdk_open/scripts/harmony/build_sdk_open_app.sh --build-web --release
```

首次构建会创建子模块 `web/` 父目录，再同步 Demo/Admin、模型和设备配置；生成资源不入库。
未签名产物：

```text
src/sdk_open/platform/harmony/build/outputs/default/harmony-default-unsigned.app
src/sdk_open/platform/harmony/sdkopen_tray/build/default/outputs/default/sdkopen_tray-default-unsigned.hap
```

当前单 HAP Release 整包已构建成功。tray HAP 内 30 个 native 库通过 AArch64、NEEDED 和
MuPDF 私有 JPEG 符号隔离检查；`com.czur.opensdk`、权限、配置、Demo 和模型资源检查通过。
独立复查命令为：

```bash
python3 src/sdk_open/scripts/harmony/verify_sdk_open_native.py \
  src/sdk_open/platform/harmony/sdkopen_tray/build/default/outputs/default/sdkopen_tray-default-unsigned.hap
```

## 迁移前回归基线

下表是在双 HAP/AppService 生命周期下完成的本地回归。devices、provider 和 WS 测试仍可作为
算法与协议基线；其中 Harmony 生命周期静态契约 8/8 已因单 HAP 托盘生命周期变化而失效，
需要按 `SdkOpenRuntime` 和隐藏 `SdkOpenTrayAbility` 重写并重新执行。

| 范围 | 结果 |
| --- | --- |
| 主仓 devices/UVC/USB Host/ISO host tests，ASan + UBSan | 7/7 |
| USB Host N-API bridge/字节数/超时/async cleanup | 4/4 |
| devices Python 审计 | 16/16 |
| Harmony 生命周期静态契约 | 8/8 |
| 真实 JPEG、按需解码策略、runtime 配置路径 | 1/1 |
| WS 状态机与真实 socket 集成 | 2/2 |
| WS 实际连接重复运行 | 50/50 |
| WS ASan + UBSan / TSan 重复运行 | 20/20、5/5 |
| SDK capture task / OCR language 后端回归 | 2/2 |
| Demo/Admin production build | 通过 |
| TLS ON mac-arm64 runtime 编译 | 通过 |

新增测试名为 `sdk_private.harmony_device_provider_contract`、
`sdk_open.ws_video_backpressure`、`sdk_open.ws_video_server_integration`。
生命周期契约执行：

```bash
python3 -m unittest src/sdk_open/test/harmony_lifecycle_contract_test.py -v
```

真实 WS 测试会启动服务和客户端，持续发布 500 张 64 KiB 帧，验证慢消费发生预览覆盖、
恢复后最终收到序号 500、metadata/binary 匹配，并验证并发 Publish/Stop 和 CloseStream。
这些测试证明本地发送状态的有界行为，不能替代真机浏览器端到端延迟测量。

## 单 HAP 托盘生命周期回归

当前 `SdkOpenRuntime`、隐藏主体 Ability 和弹窗命令路由的本地回归 19/19 通过。测试实际
转译并执行 ArkTS 生命周期源码，使用 Harmony API stub 验证串行启停、退出时拒绝排队启动、
资源准备中取消、文件部分写入和异常关闭，以及 native/USB 清理失败后的重试；同时检查
弹窗不持有 native、冷启动命令不会遗漏、同进程 Ability 重建后仍能启动。

```bash
python3 -m unittest -v src/sdk_open/test/harmony_lifecycle_contract_test.py
```

这些本地测试不证明系统的后台驻留和退出回调行为。本机缺少对应 2in1 模拟器镜像，
本轮未完成模拟器运行验证；新包名的真机验证仍需匹配签名/profile。

扩大回归还发现两项既有基线差异，相关文件与 `f08675b` 一致，本轮未修改：

- `sdk_open.status_software` 要求 software 恰有 3 个字段，现有实现含 platform 共 4 个字段。
- Demo `ocr-language.test.mjs` 中，Demo 语言表比主仓 enabled 集合少 `vi`（越南语），
  前端测试结果为 2/3。

## 真机待验收项

当前 `192.168.1.116:8007` 在线，但正式 `com.czur.opensdk` 签名/profile 尚未准备，API KEY
尚未提供。因此本记录没有正式 SDK 真机或 Demo 采集通过结论。

当前单 HAP 托盘形态不申请 `SUPPORT_APP_SERVICE_EXTENSION`，也不依赖
`START_INVISIBLE_ABILITY`；仍须生成与新包名匹配的普通签名/profile。旧包名 profile 和沙箱
不能迁移。若旧 `com.czur.sdkopen` runtime 仍运行，须先从旧托盘关闭它以释放相同的本机
端口；不需要卸载旧包或删除旧数据。签名就绪后用现有安装入口安装整包，启动托盘并点击
“启动服务”，再打开 Demo、输入 API KEY，然后执行：

1. ET `04FC:6333` 枚举、1536×1152 预览、厂商拍照及结果显示；FPS 读取实际协商值。
2. 托盘进程重启、退出后的 USB 资源释放；断开并重新连接浏览器。
3. 正式链路持续 20 分钟，每分钟拍照一次，记录图对完整性、错误、队列、内存和阶段耗时。
4. 空消费者及 50/100/200 ms 慢消费者，确认恢复消费后交付最新预览、图对不乱序。
5. 异常断连和 bridge 超时后的恢复边界；超时封闭 env 时完整重启宿主。

SDK 帧 `timestamp_ms` 保留 provider 发布时的既有语义，不能当作曝光时间或绝对采集到显示
延迟。长测须结合 devices 单调时钟阶段统计和浏览器本地测量；不能仅凭最近 render 时间声称
没有延迟。

主仓 `test/ohos_devices/acceptance-2026-10-29.md` 的 20 分钟后台测试和拔插记录属于独立
devices HAP，不替代上述正式集成验收。旧双 HAP/AppService 的模拟器验证同样不能替代当前
单 HAP、`com.czur.opensdk` 架构的真机验收。

GBrain：已查询，未命中本次目录迁移的直接记录；以当前主仓、SDK 子模块和实际构建结果为准。
