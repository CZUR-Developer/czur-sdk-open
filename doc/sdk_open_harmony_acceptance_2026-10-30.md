# Harmony 单 HAP 托盘正式 SDK 验收（2026-10-30）

本记录针对 `com.czur.opensdk` 托盘、真实 private provider、USB Host/C++ ISO、WSS、
真机华为浏览器 Demo 的正式集成链路。与 `test/ohos_devices` 独立 HAP 的历史验收分开。

## 环境和版本

- 2in1 真机：HarmonyOS 6.0.0.130，ARM64；hdc `192.168.1.116:8007`。
- 应用：`com.czur.opensdk`，单 `sdkopen_tray` HAP；runtime 位于隐藏托盘 UIAbility。
- 不使用 `SUPPORT_APP_SERVICE_EXTENSION`，证书安装使用普通 `ACCESS_CERT_MANAGER`。
- TLS/首次集成验收包内嵌 HAP SHA-256：`db5881a8949c8f523f48b28698381de9baaa8832709e3f318c90f7207603d97a`。
- 1024 分块激光修复及本轮曲面长测包 HAP SHA-256：`33358b4a56e6da15638c933a744f34aaa3a6d22cae35178c6446fcf64dc58a32`，PID `23879`。
- ET24 Pro：`04FC:6333`，当前 USB identity `262151`，endpoint `0x82` 为 ISO。
- 当前 device.open 返回 `1536x1152 fps=30 image_transfer=true`；30 是本次设备返回值，
  不能推广到所有 1536×1152 profile。

## HTTPS、WSS 和宿主

用户已通过系统对话框安装当前应用生成的 `Local Runtime Root CA`。重新加载 HTTPS 页后，
华为浏览器正常显示 Demo，无证书警告；连接页显示 `wss://127.0.0.1:18090`，
`system.ping` 成功。用户在 HTTPS origin 输入 API KEY 后，`IS_VALID=是`，
服务端 `boundSessions=1`；视频 WSS `18091` 随真实 stream 建立，持续发送 JPEG。

HTTPS/HTTP 授权状态按浏览器 origin 隔离；HTTP 页的本地令牌不会自动迁移到 HTTPS 页。
不导出 API KEY、会话令牌或私钥。证书前台窗口关闭后，原托盘 PID `43483` 保持运行；
开始设备验收前 uptime 约 10 小时，健康接口正常，未发现新增应用 crash。
这段空闲驻留不替代下面的采集长测。

## 厂商拍照冒烟

首次 `capture.take`：`image_transfer=true`，最终 `source=still_image`、`code=0`、
`captured=true`，耗时 3140 ms。保存结果大图 5696×4272，JPEG 4571388 bytes，
独立激光 JPEG 213571 bytes。Demo 显示 `SUCCEEDED` 并成功加载 HTTPS 缩略图。
5696×4272 是 provider 保存结果的尺寸，可能包含设备配置的输出缩放，不把它称作 USB 原始尺寸。

## 20 分钟正式长测

早期 `v2` / `v3` 未达到“20 分钟、20 次拍照全部成功”的通过条件；后续正式曲面
`v4` 已通过，最终结果见下文及 `evidence/harmony_et_laser_2026-10-30.json`。

- `v2` 自动化第 12 次点击未产生 `capture.take` / `device.capture armed`，随后实际拍照成功。
  该轮中止，不把自动点击计为成功请求。
- `v3` 持续运行 1200.01 秒，PID `43483` 不变，视频发送计数 28189→44640，
  `sendFailures` 保持 0；仅 3 次自动拍照实际执行并成功。后 17 个时隙被脚本页面识别拒绝。
  复查发现结果列表增长和滚动使标题不可见，脚本错误地判断采集页不在前台。该轮不能算采集长测通过。
- 配置复核还发现页面实际使用了 `single_page` / `auto_optimize`。此前“保留原图、无优化”的
  人工记录不准确；后续以实际 profile 和算法阶段日志为准。

## 曲面和单页问题复现

用户反馈曲面展平分页、单页识别效果不符合预期。曲面拍照 token=21 成功接收大图及激光图，
但 `graphic.curved_book fallback_edge laser_present=true`，最终
`success outputs=2 used_laser=false`。生成左右页不等于使用了 ET 激光展平。

已定位：ET `LASER_CONFIG_CMD` 需要 4096 字节，Harmony USB Host ArkTS 与 N-API 上限均为
1024 字节，导致打开时 `device.laser_config read_failed`。桥接上限和单页识别修复已完成 Release 签名构建并安装。新进程 PID `9940` 下重新打开 ET，
仍出现 `device.laser_config read_failed`，因此 4096 字节上限并非唯一问题，激光验收仍未通过。
已补充控制传输阶段、请求长度和实际长度诊断，继续定位。验收必须重新确认 `device.laser_config refreshed`、`used_laser=true` 和结果图像。

## 鸿蒙标定分块读取依据与待验收项

带诊断包在真机记录：`unit=1 selector=0x18 request=0x81 requested=4096 actual=1024 error=none`，
失败发生于 `stage=size`；对应 4096 字节模式切换写入成功。

官方 OpenHarmony 6.0 的 [libusb adapter 控制读实现](https://github.com/openharmony/drivers_peripheral/blob/8cd47d4bb09a4d160e8b713b2fbccf8d41e2025f/usb/libusb_adapter/src/libusb_adapter.cpp#L1097-L1126)
和 [HDI 控制读实现](https://github.com/openharmony/drivers_peripheral/blob/8cd47d4bb09a4d160e8b713b2fbccf8d41e2025f/usb/hdi_service/src/usb_impl.cpp#L1870-L1915)
均将单次读长度限制到 1024，并以限制后的长度实际发出 USB 请求。因此本轮采用鸿蒙专用的
1024 字节重复 GET_CUR，保留已成功的模式切换写长度。用户确认固件支持 1024 字节多次读取。
其他平台继续使用其原有块大小；普通控制读保持严格语义。

必须检查设备声明的文件长度与实际累计写入长度一致、零进展和失败不替换有效文件，并确认
`device.laser_config refreshed` 和 `graphic.curved_book success outputs=2 used_laser=true`。
1024 分块修复已完成 Release 签名构建并安装。真机设备日志确认
`expected=41508 received=41508 reads=41 request=1024`，runtime 确认
`device.laser_config refreshed`。首次曲面任务 `cap-1793327103-1` 成功接收
5696×4272 原图及独立激光图，记录 `graphic.curved_book success outputs=2 used_laser=true`
和 `processing complete status=succeeded code=0 assets=13`；HTTPS Demo 显示左右两页。

主仓 host 回归 7/7、N-API 4/4 通过；标定测试经真实
DeviceCommand→DeviceManager→OHOS Host transport 调用链覆盖 2050 字节尾块、中途读取失败
保留原文件、零长度拒绝更新，并断言 SET_CUR 为 4096、GET_CUR 为 1024 及失败时删除临时文件。
其他平台保留原块大小。

正式曲面 `v4` 长测已完成：持续 1200.005 秒，20 次采集均通过采集 token、任务 ID、
激光图及处理完成日志的独立关联检查，均使用激光展平并输出两页。PID 始终为 `23879`，
236 个状态样本无异常，WSS `sendFailures=0`，视频发送累计增加 34094 帧；
单次请求到处理完成为 12.266～14.204 秒。最终审计保存在
`evidence/harmony_et_laser_2026-10-30.json`，原始长测目录为
`/tmp/sdkopen-laser-soak-20261030-v4/`。触发以每分钟为目标，实际间隔受真机 UI 自动化影响，
为 31.528～88.134 秒；不把它表述为严格每 60 秒一次。

单页修复包括实时检测失败时清除过期框、隔离旧 stream 的框缓存、单页算法无有效四角时
尝试 page_keypoints，并剔除整幅图的兜底框。曲面长测后已切换单页，首次任务
`cap-1793328528-22` 成功，但实时框为 `rects=0`、处理为 `used_rect=false`，
因此尚不能认定识别框贴合通过。

## 平整单页最终验收：通过

在上述失败记录后，补齐模型资源并重新构建、签名和覆盖安装。最终 HAP SHA-256 为
`68387ae0c2ffabb1a478d3f4d20e0bb0bc6166ee6fe6d0efcb0547ddb4bb1d36`，
真机进程 PID 为 `56023`。用户已把样本换为黑色垫板上的平整文字纸，四边均在镜头内；
验收在真机华为浏览器 `https://127.0.0.1:18081/capture-acquisition` 完成。

修正内容：

- 鸿蒙原先打包的 `czcv/models/arm64` 比完整资源目录少 8 个模型。改用
  `czcv/models/x86-64` 的模型数据；两目录共有的 32 个文件内容一致。目录名不代表
  native 库架构变化，运行库仍为 OHOS AArch64。
- 模型完成标记升级为 `.sdkopen-czcv-model-v2`，覆盖安装会重新复制模型。真机确认
  v2 标记存在，新增 8 个文件 SHA-256 与源模型逐一一致。
- 单页算法返回整帧四角时继续尝试 `page_keypoints_detect`，单页回退使用
  `isMultiplePage=false`；整帧兜底框不作为纸边。检测失败清除缓存、旧 stream 隔离
  的修正保留。

首帧、视频重启后的首帧及后续抽样日志均为
`points=4 full_frame=false keypoints_fallback=false rects=1`。原始截图人工检查显示
识别框四边贴合纸边。完成两轮识别框关闭/开启：关闭后框消失，重新开启后恢复；
另完成一轮 `video.stop` → `video.start`，恢复后仍贴合。

随后连续三次实际拍照，每次均独立核对采集 token、处理线程和任务 ID：

| 任务 ID | 采集 token | 原图尺寸 | 单页输出尺寸 | 使用识别四角 | 处理结果 |
| --- | --- | --- | --- | --- | --- |
| `cap-1793332173-1` | 1 | 5696×4272 | 2431×3413 | `used_rect=true` | `succeeded code=0` |
| `cap-1793332186-2` | 2 | 5696×4272 | 2466×3411 | `used_rect=true` | `succeeded code=0` |
| `cap-1793332198-3` | 3 | 5696×4272 | 2465×3413 | `used_rect=true` | `succeeded code=0` |

三次均为 `source=still_image`，单页处理 `rects=1 outputs=1`，每任务资产数为 8。
从服务端收到 `capture.take` 到处理完成分别为 10.168、9.546、7.554 秒，均使用设备日志
的同一时钟计算。逐张检查保存的 `page_processed.jpg`，页眉、完整目录、底部页码均保留，
黑色垫板已去除。全程 PID 不变，采集前后状态样本 `sendFailures=0`。

验证：生命周期契约 24/24 通过；C++11 设备 provider 契约目标重新构建并直接运行退出 0；
签名 APP 验签、30 个 native 库 ABI/NEEDED 检查和 `git diff --check` 通过。
本地顶层 CTest 未注册该设备契约测试，因此不把 CTest 的空测试列表记为测试通过。
独立只读审查结论为 Approve。

证据与范围：

- [结构化验收结果与资产哈希](evidence/harmony_single_page_2026-10-30.json)
- [重新开启后的识别框](evidence/harmony_single_page_2026-10-30/preview-reenabled.jpeg)
- [关闭识别框](evidence/harmony_single_page_2026-10-30/preview-disabled.jpeg)
- [视频重启后的识别框](evidence/harmony_single_page_2026-10-30/preview-restarted.jpeg)
- [第 3 次原图](evidence/harmony_single_page_2026-10-30/capture-3-original.jpg)与
  [第 3 次单页裁切图](evidence/harmony_single_page_2026-10-30/capture-3-page-processed.jpg)
- [过滤后的本轮日志](evidence/harmony_single_page_2026-10-30/runtime.log)；全部三次大图和
  原始采样记录在 `/tmp/sdkopen-single-fix-acceptance-20261030/`。

结论限定当前平整文字纸样本。纸张移出、遮挡、其他尺寸和旋转角度未在本轮扩展；
单页未单独重复 20 分钟长测。识别框贴合为截图视觉验收，没有毫米级误差阈值。

## 统计边界

- `ws.video.binaryFrames/binaryBytes` 表示服务端提交发送的数量，不等于浏览器绘制数量。
- `previewFramesDropped` 是服务端 latest-frame-wins 的覆盖计数；丢弃旧预览允许发生。
- Demo 接收、绘制、丢弃和解码失败计数用于观察消费情况；不把“最近帧时间”当作采集延迟。
- devices 的 USB→回调单调时钟统计未在当前正式 SDK 状态接口暴露，本轮不借用旧独立 HAP
  的帧龄数字作为正式 SDK 数据。
- 没有曝光→USB→SDK→Canvas 的统一帧 ID/时钟测量，不能对绝对端到端延迟作零延迟保证。

GBrain：已查询并命中桌面曲面算法流程背景，确认原图＋激光图→载入标定→激光展平→失败时边缘回退。
该背景不替代本轮真机验收，结论以当前源码、签名包和真机证据为准。
本轮另窄查询单页识别框/CZCV 背景，未命中本次模型缺失的直接记录。
