# SDK Open 硬拍、翻页检测、设备生命周期监听改动说明

本文整理当前 `sdk_open + sdk_private` 中硬拍、翻页检测、设备移除/接入监听相关修改。重点说明兼容层职责，避免后续把 SDK Open 专用适配下沉到 `module/devices`。

## 目标

- 硬拍统一通过 `capture.hardgrab_detected` 通知，触发后自动创建异步采集任务。
- 翻页检测统一通过 `capture.turn_detected` 通知，只通知前端，暂不自动拍照。
- 设备接入统一通过 `device.added` 通知，作为 inventory changed 提示，客户端再调用 `device.list` 刷新授权内设备。
- 设备移除统一通过 `device.removed` 通知；已打开/预览设备发送精确移除，未打开设备只发送 inventory changed。
- 兼容 still-image 设备、非 still-image 设备、设备拔出后的幂等关闭、已移除设备的错误语义。
- 不修改 `module/devices` 的设备类型判断，把 SDK Open 适配放在 private provider 和 sdk_open application 层。

## 分层职责

### module/devices

`module/devices` 仍负责真实设备、预览、硬拍、热插拔的底层检测能力。当前改动不应继续在这一层加入 SDK Open 的特殊分支。

兼容边界：

- 设备硬拍底层可来自 `setHardDetect`，也可来自 still-image 回调。
- 设备接入/移除底层通过已有 hotplug callback 触发。
- SDK Open 不应要求 `module/devices` 为 demo 协议或 web 消息格式做适配。

### sdk_private PrivateDeviceProvider

`PrivateDeviceProvider` 是 SDK Open 和底层设备能力之间的兼容层，负责把底层行为归一成 SDK Open 事件。

主要处理：

- 注册 `SetDeviceActionEventSink`，向上抛硬拍和翻页检测。
- 注册 `SetDeviceEventSink`，向上抛设备生命周期事件。
- 启动预览时注册 still-image 回调和 `setHardDetect` 回调。
- still-image 设备不依赖 `setHardDetect` 成功，收到 still-image 后转为硬拍事件。
- 非 still-image 设备通过 `setHardDetect` 置位 pending，再用下一帧预览图生成硬拍事件。
- 设备移除时清理 active stream、翻页检测状态、pending capture、pending hardgrab、still-image 抑制状态。
- 设备已移除后，`video.stop` 和 `device.close` 按幂等/明确原因处理，避免混成普通参数错误。

### sdk_open application

`CommandApplicationService` 负责把 provider 事件转换成稳定 websocket 事件协议。

主要处理：

- `SdkDeviceActionType::HardGrab` 分发为 `capture.hardgrab_detected`。
- `SdkDeviceActionType::PageTurn` 分发为 `capture.turn_detected`。
- `SdkDeviceEvent` 分发为 `device.added` 或 `device.removed`。
- `SdkDeviceEvent.type` 只接受 `added` / `removed`，未知值直接 warn 并 drop。
- 硬拍事件只发给已打开该设备的 connection。
- `device.added` 只作为 inventory changed 广播，不携带具体 `device_id`。
- `device.removed` 对已打开/预览设备发送精确通知；对未打开设备广播 inventory changed，不携带具体 `device_id`。
- 设备精确移除事件先清理 opened device、video session、capture context，再通知相关 connection。
- 硬拍自动采集使用 capture task 异步执行，先发 accepted，再通过 `capture.completed` 或 `capture.failed` 返回最终任务结果；原图进入处理 FIFO 后不会阻塞下一次拍照。

### demo-site

demo 只消费 SDK Open 事件，不再自行推断底层设备状态。

主要处理：

- 手动调用 `capture.set_turn_detect` 开启或关闭翻页检测。
- 收到 `capture.turn_detected` 后只展示通知，不自动拍照。
- 收到 `capture.hardgrab_detected` 后展示 accepted/rejected，并轮询或等待 `capture.completed` 更新结果。
- 收到 `device.added` 后刷新设备列表。
- 收到 inventory changed 类 `device.removed` 后刷新设备列表。
- 收到带 `device_id` 的精确 `device.removed` 后清理本地 preview/video 状态和设备列表。
- UI 时间线只展示一条用户可见 `device.removed`，内部处理日志使用 internal runtime event。

## 硬拍链路

### 事件来源

硬拍有两类来源：

- still-image 设备：底层 still-image 回调直接携带原始大图，private 层转成 `SdkDeviceActionEvent{HardGrab}`。
- 非 still-image 设备：底层 `setHardDetect` 只代表按键触发，private 层记录 pending，下一帧预览图到达时生成硬拍图。

兼容说明：

- still-image 设备上 `setHardDetect` 失败不作为 warning。因为该类设备的硬拍图像由 still-image 回调提供，`setHardDetect` 不是唯一触发通道。
- still-image 回调可能包含大小图或原图/激光图，private 层选择大图并保存到 runtime 的
  `capture/hardgrab/<capture-id>/original.jpg`，激光图保存为同目录下的 `laser.jpg`。
- 正常 `capture.take` 期间收到 still-image，会优先完成 pending capture，不重复发布硬拍事件。
- 正常拍照失败或结束后的短时间内会抑制迟到 still-image，降低误触发硬拍风险。

### 自动采集

硬拍事件会自动创建 capture task，但不在事件回调线程同步做完整图像处理。

流程：

1. private 层发布 `SdkDeviceActionEvent`，其中 `capture.original_path` 和可选的
   `capture.laser_path` 指向 Provider 已保存的临时原图。
2. sdk_open 收到后发送 `capture.hardgrab_detected`。
3. sdk_open 先做 `capture.take` 鉴权和采集准入：真实物理采集占用时返回 `DeviceBusy`，同设备 1500ms 窗口内返回 `RateLimited (1004)` 与 `retry_after_ms`。
4. 采集准入并成功登记 task 后才扣减 `capture.take` quota；鉴权、参数、限频、物理采集中、配额或登记失败都不创建可运行任务，事件返回 `accepted=false`、`code`、`message`、`error`。
5. 成功时创建异步采集任务，`capture.hardgrab_detected` payload 中返回 `accepted=true` 和 `task_id`。
6. capture task 校验并将 Provider 返回的原图路径转存到当前任务专属 raw 目录；原图成功后立即进入该 device 的处理 FIFO，再走普通 `capture.take` 相同 pipeline。
7. 处理中的 FIFO 不占用物理拍照，后续手动拍照或硬拍可继续采集；任务结束后发送 `capture.completed` 或 `capture.failed`，并支持 `capture.get` 和 `capture.session.get` 查询快照。

### Profile 与 workflow 兼容

硬拍事件来自异步 provider 回调，本身没有请求参数。因此 sdk_open 维护 connection + device 维度的采集上下文缓存：

- `video.start` 可携带并缓存 `CaptureProfile` 和可选增强 workflow。
- `video.set_profile` 可更新并缓存 `CaptureProfile` 和可选增强 workflow。
- `capture.take` 成功创建任务后也会缓存当次 `CaptureProfile` 和 workflow。
- 硬拍自动采集时复用最近一次缓存的 `CaptureProfile` 和 workflow。

兼容说明：

- 旧客户端不传 `pipeline` 时，不清空既有 workflow 缓存，避免改变旧调用语义。
- demo 选择或取消增强 workflow 时，需要显式通过 `video.set_profile` 同步；取消时要同步空 workflow，避免后端继续复用旧 workflow。
- 第一次打开预览后，如果未同步 profile/workflow 就硬拍，只能使用默认 profile 且 `pipeline_steps=0`。
- 已同步后，日志应出现 `hardgrab task submit begin ... profile_revision=<当前值> pipeline_steps=<workflow步数>`。

## 翻页检测链路

翻页检测与实时纸张框不是同一个功能。

当前流程：

1. demo 采集 Profile 分组下提供翻页检测开关。
2. 开启时调用 `capture.set_turn_detect`，参数包含 `enabled=true`、`auto_capture=false`、`scan_device_type`、`cooldown_ms`。
3. private 层保存 `TurnDetectState`。
4. 预览帧到达时，private 层调用 czcv 翻页检测能力。
5. 首帧用于初始化，后续检测到翻页时发布 `SdkDeviceActionEvent{PageTurn}`。
6. sdk_open 转成 `capture.turn_detected` 发送给打开该设备的 connection。
7. demo 收到后只展示通知，不自动拍照。

兼容说明：

- 关闭翻页检测时，即使设备已移除，也允许清理本地状态并返回成功，避免关闭流程报 `device not found`。
- `auto_capture` 当前强制为 false，避免翻页检测影响正常采集流程。
- 当前实现没有在 sdk_open 层做额外去重；是否单次动作只通知一次主要依赖底层算法和 provider 状态。
- 设备停止预览或移除时会清理/重置翻页检测状态，避免旧状态跨设备复用。

## 设备生命周期链路

当前补齐的是 SDK Open 层设备生命周期事件，而不是重新实现底层热插拔。

### 设备接入

流程：

1. `module/devices` 触发 hotplug add callback。
2. `PrivateDeviceProvider::HandleHotplugEvent` 接收 add。
3. private 层发布 `SdkDeviceEvent{type=added, reason=hotplug_added}`。
4. sdk_open `DispatchDeviceEvent` 转为 `device.added`。
5. sdk_open 向当前 command connection 广播 inventory changed，不携带具体 `device_id`。
6. demo 收到 `device.added` 后调用 `device.list` 刷新授权范围内设备。

### 设备移除

流程：

1. `module/devices` 触发 hotplug remove callback。
2. `PrivateDeviceProvider::HandleHotplugEvent` 接收 remove。
3. private 层记录 `recently_removed_device_ids_`，用于区分“设备已移除”和普通参数错误。
4. private 层完成或失败 pending capture，清理 stream、turn detect、pending hardgrab、still-image 抑制等状态。
5. private 层发布 `SdkDeviceEvent{type=removed, reason=hotplug_removed, was_opened, was_streaming}`。
6. sdk_open `DispatchDeviceEvent` 转为 `device.removed`。
7. 如果该设备已被某个 connection 打开或持有 video session，sdk_open 在发送事件前清理 opened device、video session、capture context，并关闭对应 video stream。
8. 如果该设备未打开且没有 video session，sdk_open 广播 inventory changed，不携带具体 `device_id`。
9. demo 收到带 `device_id` 的 `device.removed` 后清理本地预览状态、设备列表和错误提示；收到不带 `device_id` 的 `device.removed` 后只刷新设备列表。

兼容说明：

- `device.added` 不发送具体设备信息，只提示 inventory changed，避免绕过 `deviceScope` 授权过滤。
- 已打开/预览设备的 `device.removed` 发送给实际打开或持有该设备 video session 的 connection，用于精确清理本地状态。
- 未打开设备的 `device.removed` 不携带敏感设备信息，只提示 inventory changed，客户端必须再调用 `device.list` 获取授权内设备。
- 对同一设备重复 remove 会被 private 层去重，避免 UI 多次展示。
- `device.close` 对已移除设备应保持幂等成功，业务上等价于“底层已清理，本地完成关闭”。
- `video.stop` 在设备已拔出时可能无法再调用到底层成功，应返回明确 reason，而不是只报普通 `device not found`。
- 前端收到 `device.removed` 后不需要再强制调用 `video.stop` 来停止底层流，底层移除链路已清理。

## 协议变化

新增/明确的事件：

- `capture.hardgrab_detected`
- `capture.session.updated`
- `capture.turn_detected`
- `device.added`
- `device.removed`

`capture.hardgrab_detected` payload 关键字段：

- `device_id`
- `auto_capture`
- `ts_ms`
- `accepted`
- `code`
- `message`
- `error`
- `capture`
- `task`
- `task_id`
- `status`
- `retry_after_ms`（限频拒绝时）

`capture.turn_detected` payload 关键字段：

- `device_id`
- `auto_capture`
- `ts_ms`

`capture.session.updated` payload 关键字段：

- `device_id`
- `task_id`
- `reason`（`raw_captured`、`capture_failed`、`processing_completed`、`processing_failed`）
- `summary`（当前 connection 的 `captured_count`、`processed_count`、`failed_count`、`pending_count` 和 `devices` 明细）

`device.added` payload 关键字段：

- `reason`
- `ts_ms`

`device.removed` 精确移除 payload 关键字段：

- `device_id`
- `reason`
- `was_opened`
- `was_streaming`
- `ts_ms`

`device.removed` inventory changed payload 关键字段：

- `reason`
- `ts_ms`

兼容说明：

- 硬拍事件只保留 `capture.hardgrab_detected`，不再使用 `source`、`trigger` 等冗余字段。
- 翻页检测不新增 `pageTurnDetect` 或 `page_turn` 冗余字段。
- 设备生命周期事件不复用 `SdkDeviceActionEvent`，因为 action event 语义是硬拍/翻页，生命周期事件使用独立 `SdkDeviceEvent`。
- payload 不携带 `type` 字段，事件类型由 websocket event name 表达。
- `device.added` 和未打开设备的 `device.removed` 都只表达 inventory changed，不保证具体设备一定在当前 session 授权范围内。

## 日志验证点

### 硬拍

期望日志：

```text
device.action_event hard_detect_registered ...
device.action_event hard_detect_unavailable ... still_image_fallback=true
device.action_event still_image_hard_detect ... captured=true payload_bytes=...
device.action_event hardgrab_capture_ready ...
device.action_event publish ... type=hardgrab auto_capture=true
[sdk_device_action] dispatch event=capture.hardgrab_detected ... target_connections=...
[sdk_device_action] hardgrab task submit begin ... profile_revision=... pipeline_steps=...
[sdk_device_action] hardgrab task submit end ... accepted=true code=0
[capture_task] task finished ... status=succeeded
[sdk_command_event] send event=capture.completed ...
```

若带 workflow，`pipeline_steps` 应大于 0，并能看到 `image_enhance` 阶段或增强输出文件。

### 翻页检测

期望日志：

```text
device.turn_detect set begin ... enabled=true auto_capture=false
device.turn_detect initialized ...
device.turn_detect detected ... auto_capture=false
device.action_event publish ... type=turn_detect
[sdk_device_action] dispatch event=capture.turn_detected ...
```

### 设备生命周期

期望日志：

```text
device.lifecycle added ... reason=hotplug_added
device.lifecycle publish ... type=added reason=hotplug_added
[sdk_device_event] dispatch event=device.added mode=inventory_changed ... target_connections=...
[sdk_command_event] send event=device.added ...
device.lifecycle removed ... was_opened=... was_streaming=...
device.lifecycle publish ... type=removed reason=hotplug_removed
[sdk_device_event] dispatch event=device.removed mode=exact_device ... target_connections=...
[sdk_device_event] dispatch event=device.removed mode=inventory_changed ... target_connections=...
[sdk_command_event] send event=device.removed ...
```

如果出现 `drop event=device.added reason=no_target_connection` 或 `drop event=device.removed reason=no_target_connection`，表示当前没有活跃 command connection，不一定是异常。

## 跨平台与兼容风险

- 当前 SDK Open 适配不依赖 Linux 专用宏；跨平台差异应由 private provider 内部或底层 provider 实现隔离。
- 不应在 `device_manager_linux.cpp` 中加入 SDK Open 特有设备类型判断，否则会影响非 SDK Open 的 CZURScanner 既有流程。
- still-image 与非 still-image 分支都必须保留，否则不同机型硬拍会失效。
- 硬拍自动采集复用 capture task 后，会消耗 `capture.take` 配额；这是和正常拍照对齐的行为。
- 硬拍原图已由底层回调获得，因此 Open SDK 的原图落盘不再占用 device busy；进入处理 FIFO 后同样不占用设备。后续手动 `capture.take` 或硬拍只受真实物理采集占用和固定 1500ms 限频约束。
- 设备移除后仍可能有迟到帧或迟到 still-image，private 层需要继续依赖 stream 状态和 suppress 状态过滤。
- demo 固定 `scan_device_type: 0` 时，private 层/底层仍应尽量按 CZURScanner 逻辑做设备适配，不建议把 demo 参数作为唯一设备类型来源。

## 当前回归建议

- 打开预览后直接硬拍，确认默认 profile 下 `pipeline_steps=0` 行为可接受。
- 选择 workflow 后调用 `video.set_profile`，再硬拍，确认 `pipeline_steps` 与 workflow 步数一致。
- 正常 `capture.take` 与硬拍结果对比，确认 page process、color mode、thumbnail、format convert、image enhance 输出一致。
- 开启翻页检测后翻页，确认只收到 `capture.turn_detected`，不会自动拍照。
- 开启翻页检测后停止预览或关闭设备，确认不会残留 turn detect 状态。
- 打开预览后拔设备，确认收到一条 UI 可见 `device.removed`，视频区域被清理。
- 未打开设备直接拔出，确认收到不带 `device_id` 的 `device.removed`，demo 只刷新设备列表，不展示敏感设备信息。
- 初次无设备时接入设备，确认收到 `device.added`，demo 自动刷新 `device.list` 后可打开预览。
- 设备移除后点击 `device.close`，确认幂等成功或返回明确“已移除”语义，不再误判为参数错误。
