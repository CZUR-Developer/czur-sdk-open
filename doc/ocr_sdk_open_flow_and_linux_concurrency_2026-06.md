# SDK Open OCR 与条码识别链路、Linux 串行执行闸门说明

本文整理 2026-06 在宿主机上的一次真实联调结论，目标是把 SDK Open 当前 OCR/识别相关接口的执行链路、Linux 并发约束、状态语义与验证结果固定下来，便于后续排查和回归。

范围只覆盖以下 5 个接口：

- `ocr.recognize`
- `ocr.get`
- `ocr.cancel`
- `ocr.extract_text`
- `recognition.barcode_detect`

## 1. 结论摘要

- `ocr.recognize` 支持多请求并发提交，但在 Linux 下底层 OCR 引擎执行已改为单槽位串行进入。
- `ocr.get` 现在可以稳定反映 `queued / running / completed / cancelled` 这几个核心状态。
- `ocr.cancel` 对排队中任务和运行中任务都生效；运行中取消不再落成 `未知错误 (错误码: 99)`。
- `ocr.extract_text` 已确认与 `ocr.recognize` 共享同一个 Linux OCR 执行槽位，因此也受串行闸门保护。
- `recognition.barcode_detect` 链路独立，可正常调用，但不经过 `OcrManager`，不参与 OCR 串行排队。

## 2. 五个接口的完整执行流程

### 2.1 `ocr.recognize`

入口在 [command_application_service.cpp](/home/lyc/czur-capeditor/src/sdk_open/application/command_application_service.cpp:3200) 的 `HandleOcrRecognize(...)`。

主流程如下：

1. 校验 capability。
2. 解析 `input_upload_id(s)`、`input_path(s)`、`output_path/output_dir`、`format`、`exportType`。
3. 把上传资产解析成磁盘路径。
4. 补齐并校验 `ext_params`、输出目录和输入文件存在性。
5. 调用 `ocr_facade_.Recognize(...)`。

Facade 最终落到私有 provider：

- [private_provider_factory.cpp](/home/lyc/czur-capeditor/src/sdk_open/providers/private/private_provider_factory.cpp:230) `PrivateOcrProvider::Recognize(...)`

provider 侧关键行为：

- 先生成 SDK 层任务号，例如 `ocr-13`，并立刻把快照初始化为 `queued`。
- `single-page` 模式走 `submitBatchTaskWithDetailedProgressAndParams(...)`。
- `multi-page` 模式走 `submitTaskWithDetailedProgressAndParams(...)`。
- SDK 返回给调用方的是 provider 维护的任务快照，不是 `OcrManager` 原始内部任务号。

真正进入 OCR 管理器后：

- 单任务处理逻辑在 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:579) `OcrManager::processTask(...)`
- Linux 串行闸门在 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:1333) `waitForLinuxOcrExecutionSlot(...)`

当前关键语义是：

- 任务进入 `OcrManager` 后，不会立刻切成 `running`。
- 只有真正拿到 Linux OCR 执行槽位以后，才在 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:651) 将状态切为 `Processing`。
- 因此等待槽位期间，SDK 对外仍表现为 `queued`，这和真实执行状态是一致的。

### 2.2 `ocr.get`

入口在 [command_application_service.cpp](/home/lyc/czur-capeditor/src/sdk_open/application/command_application_service.cpp:3335) 的 `HandleOcrGet(...)`。

主流程很短：

1. 校验 capability。
2. 读取 `task_id`。
3. 调用 `ocr_facade_.GetTask(...)`。
4. 返回 provider 保存的任务快照。

provider 实现在 [private_provider_factory.cpp](/home/lyc/czur-capeditor/src/sdk_open/providers/private/private_provider_factory.cpp:392) `PrivateOcrProvider::GetTask(...)`。

这里没有直接触发 OCR，只是读取 SDK 层的任务镜像。状态值来自 provider 在提交、进度回调、完成回调、取消回调中的持续更新。

### 2.3 `ocr.cancel`

入口在 [command_application_service.cpp](/home/lyc/czur-capeditor/src/sdk_open/application/command_application_service.cpp:3356) 的 `HandleOcrCancel(...)`。

主流程：

1. 校验 capability。
2. 读取 `task_id`。
3. 调用 `ocr_facade_.Cancel(...)`。
4. 返回 `cancelled` 标记和最新任务快照。

provider 实现在 [private_provider_factory.cpp](/home/lyc/czur-capeditor/src/sdk_open/providers/private/private_provider_factory.cpp:404) `PrivateOcrProvider::Cancel(...)`。

provider 会先把 SDK 任务号映射回内部任务：

- 单任务走 `OcrManager::cancelTask(...)`
- 单页批量任务走 `OcrManager::cancelBatchTask(...)`

对应实现位置：

- [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:316) `cancelTask(...)`
- [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:1707) `cancelBatchTask(...)`

取消路径有两个关键点：

1. 排队中的任务
   - 如果任务还在 `m_pendingTasks` 队列中，会直接出队并标记取消。
   - 如果任务已经不在队列，但还在等待 Linux 执行槽位，会记录“已出队，当前正在等待OCR执行槽位”这一中间状态，随后仍可取消。

2. 运行中的任务
   - 不会强杀线程，而是设置取消标志。
   - 执行线程在进度回调中检测 `task->isCancelled()`，然后抛出 `"任务被取消"`，走受控退出路径。

### 2.4 `ocr.extract_text`

入口在 [command_application_service.cpp](/home/lyc/czur-capeditor/src/sdk_open/application/command_application_service.cpp:3378) 的 `HandleOcrExtractText(...)`。

主流程：

1. 校验 capability。
2. 解析 `input_upload_id` 或 `input_path`。
3. 把上传图片解析到磁盘路径。
4. 调用 `ocr_facade_.ExtractText(...)`。

provider 实现在 [private_provider_factory.cpp](/home/lyc/czur-capeditor/src/sdk_open/providers/private/private_provider_factory.cpp:441) `PrivateOcrProvider::ExtractText(...)`。

这里和 `ocr.recognize` 的差别是：

- 它同步读取图片，解码成 `cv::Mat`。
- 然后直接调用 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:1251) `OcrManager::processImageData(...)`。
- 识别结果组装成 `blocks` 直接返回，不创建可轮询任务。

本次修复后，`processImageData(...)` 同样会先经过 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:1333) `waitForLinuxOcrExecutionSlot(...)`，因此和 `ocr.recognize` 共享同一个 Linux OCR 执行槽位。

这意味着：

- `extract_text` 与 `recognize` 并发时，不会再同时冲进 Linux OCR 底层。
- 两者都会被串行化，避免引擎并发初始化/调用导致崩溃。

### 2.5 `recognition.barcode_detect`

入口在 [command_application_service.cpp](/home/lyc/czur-capeditor/src/sdk_open/application/command_application_service.cpp:3424) 的 `HandleBarcodeDetect(...)`。

主流程：

1. 校验 capability。
2. 解析 `input_upload_id` 或 `input_path`。
3. 解析可选的 `formats/detect_type`。
4. 调用 `recognition_facade_.DetectBarcode(...)`。

provider 实现在 [private_provider_factory.cpp](/home/lyc/czur-capeditor/src/sdk_open/providers/private/private_provider_factory.cpp:1233) `DetectBarcode(...)`。

这条链路的结论很明确：

- 它不经过 `OcrManager`。
- 它不创建异步 OCR 任务。
- 它不参与 Linux OCR 串行槽位竞争。
- 它是独立同步识别链路。

## 3. Linux 下“并发提交、串行执行”的实现说明

### 3.1 问题背景

宿主机真实联调时，Linux OCR 底层并不适合并发进入。并发触发 `recognize/extract_text` 时，曾出现底层崩溃迹象，典型现象是 `InitGoogleLogging() twice`。

因此，本次修复没有禁止上层多请求并发提交，而是在真正进入底层 OCR 前增加全局执行闸门，把底层执行改成单槽位串行。

### 3.2 串行闸门的位置

核心实现都在 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:1333) 附近：

- `waitForLinuxOcrExecutionSlot(...)`
- `releaseLinuxOcrExecutionSlot()`

关键机制：

- Linux 下由 `m_linuxOcrExecutionMutex + m_linuxOcrExecutionCondition + m_linuxOcrExecutionBusy` 控制单一执行槽位。
- 没拿到槽位的任务会循环等待。
- 当前任务完成或异常退出后，通过 RAII guard 释放槽位并唤醒等待者。

### 3.3 为什么 `queued` 能准确表示“等待执行槽位”

以前一个容易混淆的问题是：任务已经被工作线程拿到，但还没真正进入 OCR 引擎时，对外到底算什么状态。

本次实现明确选择：

- 进入工作线程但尚未拿到 Linux 执行槽位：仍然表现为 `queued`
- 真正进入底层 OCR：才切到 `running`

对应代码在 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:604) 到 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:653)：

- `WaitingForExecutionGuard` 把任务加入 `m_tasksWaitingForExecution`
- `waitForLinuxOcrExecutionSlot(task)` 等待槽位
- 拿到槽位后才 `task->setProgress(0, TaskStatus::Processing)`

这解决了两个问题：

1. `ocr.get` 不会过早把“尚未真正执行”的任务显示成 `running`
2. `ocr.cancel` 能识别“已出队、正在等槽位”的任务，不会误判为不可取消

### 3.4 `extract_text` 为什么也必须走同一个槽位

如果只串行 `processTask(...)`，但 `processImageData(...)` 仍可并发进入，那么 `ocr.extract_text` 和 `ocr.recognize` 仍然会在 Linux 上并发打到底层引擎，问题并没有根治。

因此当前实现把两条入口统一收口：

- 异步导出任务：`processTask(...)`
- 同步内存图识别：`processImageData(...)`

二者共用同一个 `waitForLinuxOcrExecutionSlot(...)`。

## 4. 状态语义

对 SDK Open 用户而言，本轮联调后可以按下面语义理解状态：

### 4.1 `queued`

表示任务已经被接受，但尚未真正进入底层 OCR 执行。可能有两种情况：

- 仍在 `m_pendingTasks` 队列中等待工作线程处理
- 已经被工作线程取出，但正在等待 Linux OCR 执行槽位

对于 `single-page` 批量任务，`queued` 还可能表示当前没有任何子任务进入 `Processing`，对应 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:1596) 到 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:1601) 的批量进度语义。

### 4.2 `running`

表示任务已经拿到 Linux OCR 执行槽位，并真正开始进入引擎处理。

对应代码是 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:651) 到 [ocr_manager.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr_manager.cpp:699)。

### 4.3 `completed`

表示任务处理结束，并由 provider 完成结果汇总：

- 单任务由 [private_provider_factory.cpp](/home/lyc/czur-capeditor/src/sdk_open/providers/private/private_provider_factory.cpp:353) 之后的完成回调更新。
- 单页批量任务由 [private_provider_factory.cpp](/home/lyc/czur-capeditor/src/sdk_open/providers/private/private_provider_factory.cpp:291) 之后的完成回调更新。

注意：

- 批量任务出现部分失败时，SDK 侧仍可能返回 `completed`，但 `task.error/task.message` 会带首个错误。
- 这属于当前 provider 的既有设计，不是这次修复新增的语义。

### 4.4 `cancelled`

表示任务被 SDK 调用者主动取消，并且 provider 已经把快照状态写成 `cancelled`。

对应位置：

- [private_provider_factory.cpp](/home/lyc/czur-capeditor/src/sdk_open/providers/private/private_provider_factory.cpp:429)
- [private_provider_factory.cpp](/home/lyc/czur-capeditor/src/sdk_open/providers/private/private_provider_factory.cpp:433)

这里要注意一个实现层面的区别：

- `OcrManager` 内部取消结果仍然落在“失败态 + 错误消息 = 任务被用户取消”这一路上。
- SDK provider 对外把这个语义进一步提升成显式 `cancelled` 状态。

因此 SDK 使用方应优先以 `task.status == "cancelled"` 作为取消判定，而不是继续依赖底层错误码。

## 5. 为什么运行中取消不再出现错误码 99

之前运行中取消有时会暴露成 `未知错误 (错误码: 99)`，根因是底层异常被当成未知异常吞掉了。

本次处理的关键在两层：

1. `OcrManager::processTask(...)`
   - 进度回调检测到取消时抛出 `"任务被取消"`。
   - catch 分支中如果确认是取消触发，就把结果改写成“任务被用户取消”，而不是普通异常。

2. `Ocr::processImages(...)`
   - 在 [ocr.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr.cpp:445) 到 [ocr.cpp](/home/lyc/czur-capeditor/src/module/ocr/ocr.cpp:452) 对 `"任务被取消"` 做了专门分支。
   - 该分支只返回 `common::OcrErrOcrRecog`，不再落入 `OcrErrUnknown`。

因此现在运行中取消的最终对外语义是：

- `ocr.cancel` 返回 `cancelled: true`
- 后续 `ocr.get` 返回 `task.status = "cancelled"`
- 日志中不再出现本轮取消被归类为 `未知错误 (错误码: 99)` 的情况

## 6. 宿主机真实验证结果

以下结果来自本轮宿主机真实调用，不是本地静态推断。

### 6.1 `single-page` 基线与 5 并发

探针：`/tmp/ocr_single_page_probe`

结论：

- baseline 单任务呈现 `queued -> running -> completed`
- 5 个并发 worker 全部成功完成
- 并发期间可见多个任务长时间停留在 `queued`
- 随后各任务依次进入 `running`
- 最终全部 `completed`

这证明当前行为是“上层可并发提交，底层串行排队执行”。

### 6.2 `ocr.cancel`

探针：`timeout 90s /tmp/ocr_cancel_extract_probe cancel`

结论：

- 运行中取消前，任务真实进入过 `running`
- 排队中取消前，任务真实出现过 `queued`
- 两类取消最终都落成 `status = cancelled`
- `running_has_unknown_error_99_log = false`
- `queued_has_unknown_error_99_log = false`

这说明取消路径已经按预期收敛。

### 6.3 `ocr.extract_text`

探针：`timeout 120s /tmp/ocr_cancel_extract_probe extract`

结论：

- 并发背景下 `recognize` 任务真实进入过 `running`
- `extract_text.code = 0`
- `extract_text.data.recognized = true`
- 返回了有效 `blocks`
- `recognize_after_extract` 最终 `status = completed`

这说明 `extract_text` 已被纳入同一个 Linux OCR 执行槽位控制，不会再绕过串行保护。

### 6.4 `recognition.barcode_detect`

探针：`timeout 60s /tmp/barcode_detect_probe`

结论：

- `create_session.code = 0`
- `upload.upload_id` 正常返回
- `barcode_detect.code = 0`
- `provider = "czur-recognition-provider"`
- `detected = false`
- `count = 0`

这里 `detected = false` 只表示测试图片里没有识别到条码/二维码，不代表接口异常。接口链路已经打通。

## 7. 健康检查路由结论

本轮顺带确认了 SDK Open 的健康检查路由：

- 正确路由是 `GET /healthz`
- 不是 `GET /api/health`

实现位置在 [sdk_http_server.cpp](/home/lyc/czur-capeditor/src/sdk_open/transport/sdk_http_server.cpp:190)。

宿主机已确认以下地址返回 `200 OK`：

- `http://127.0.0.1:17080/healthz`
- `http://127.0.0.1:17081/healthz`
- `http://127.0.0.1:17082/healthz`

## 8. 最终结论

截至 2026-06-16，本轮 OCR SDK Open 相关问题可以下结论为：

- Linux OCR 并发崩溃风险已通过“全局单槽位串行闸门”收敛。
- `ocr.recognize`、`ocr.get`、`ocr.cancel`、`ocr.extract_text` 的状态与取消语义已经和真实执行过程对齐。
- `ocr.extract_text` 不再是并发绕行口。
- `recognition.barcode_detect` 可正常调用，但属于独立识别链路，不应和 OCR 任务排队模型混为一谈。

后续若继续扩展 OCR 能力，凡是会真正进入 Linux OCR 底层引擎的新入口，都应先确认是否复用 `waitForLinuxOcrExecutionSlot(...)`，否则会重新引入并发进入底层的问题。
