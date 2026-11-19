# 图像处理阶段耗时日志（历史排查记录）

2026-11-19 已移除运行时图像处理计时采样及耗时日志，保留鸿蒙 DPI 读取和输出宽高
复用优化。以下日志格式、阶段名称及性能数据仅用于解释此前采集的日志；当前版本
不再生成 `[graphic_timing]`，也不再输出图像编解码、DPI 写入及格式转换日志中的
耗时字段。`scripts/analyze_graphic_timing.py` 保留用于分析历史日志。

历史版本的真实 Graphic Provider 输出 `[graphic_timing]` 日志，用于对比 SDK Open 请求耗时与
直接调用 CZCV 的纯算法耗时。计时用于观测算法、元数据及文件处理的开销，不调整算法参数、线程数、
锁范围或中间文件流程，也不修改 mock。鸿蒙 DPI 优化范围见下文。

## 获取与汇总

鸿蒙日志写入现有 `sdk_open_runtime.log`，可通过管理接口读取。开发机已有 admin
端口转发时：

```bash
curl -fsS 'http://127.0.0.1:27080/api/logs/sdk_open_runtime?tailBytes=1048576' \
  -o /tmp/sdk-open-timing.json
python3 src/sdk_open/scripts/analyze_graphic_timing.py /tmp/sdk-open-timing.json
```

接口如配置了认证，沿用现有管理接口凭据。脚本也接受导出的纯文本日志，默认显示最后
一个 trace；`--last 3` 显示最后三个，`--trace <id>` 选择指定处理记录。

```bash
rg '\[graphic_timing\]' /tmp/sdk_open_runtime.log
python3 src/sdk_open/scripts/analyze_graphic_timing.py /tmp/sdk_open_runtime.log --last 3
```

## 字段与计时口径

| 字段 | 含义 |
| --- | --- |
| `trace` | 一次同步 Provider 调用的关联标识，线程隔离；进程重启后使用新的时间戳种子 |
| `span` / `parent` | 本 trace 中的阶段编号与父阶段编号；根阶段 `parent=0` |
| `op` / `stage` | 操作与阶段名称 |
| `input` / `output` | 文件名摘要；不输出完整沙盒路径 |
| `start_ms` | 相对根阶段起点的毫秒偏移 |
| `duration_ms` | 当前阶段的墙钟耗时，包含子阶段 |
| `self_ms` | 当前阶段耗时减去直接子阶段耗时，避免重复计算 |
| `code` | 绑定了结果对象的阶段返回码；`-` 表示此阶段没有单独返回码 |
| `exception` | 该作用域是否因异常展开退出；`false` 不代表业务成功，应结合返回码 |
| `dimensions` / `channels` | 已采集的输入或算法输出尺寸、通道；`0` 表示此阶段未采集 |

耗时使用 C++11 `steady_clock`，保留三位小数。`trace` 的种子使用系统时间，
不参与耗时计算。新增阶段记录在线程内缓存，最外层作用域结束时统一写出，
不在两个纯算法计时区间内写日志；原有业务日志保持原样。

根阶段 `duration_ms` 不包含本批新增计时日志的最终输出时间，所以可能略小于命令层
记录的请求总耗时。`self_ms` 包含尚未拆分的业务逻辑、对象释放、原有日志及计时采样
开销，不能将其全部解释为算法或磁盘 I/O。

同一 trace 的所有 `self_ms` 相加应约等于根阶段 `duration_ms`，允许小数舍入误差。
不要直接累加所有 `duration_ms`。汇总脚本按 `op.stage` 聚合独占耗时，并单独列出
CZCV 调用耗时；若日志尾部截断导致父子记录缺失，会提示扩大导出范围。

`image.process` 内的页面处理、颜色处理和格式转换共享 trace。独立调用
`image.process_page`、`image.apply_color_mode` 或生成缩略图，各自产生 trace。
拍照流水线可通过原有任务阶段日志、线程号及时间关联这些调用。

## 重点阶段

| 操作 | 主要阶段 |
| --- | --- |
| `image_dpi` | `read_metadata`、`read_exif`、`read_jfif`、`resolve` |
| `image_io` | `read_file_bytes`、`codec_lock_wait`、`imwrite`、`write_with_dpi` |
| `czcv_runtime` | `init_lock_wait`、`init_czcv` |
| `single_page` | `processor_lock_wait`、`input_read`、`imread`、`algorithm_lock_wait`、`single_cut_pipeline`、`cut_page_by_keypoints`、`page_keypoints_detect`、`result_clone`、`detected_rects_copy`、`postprocess`、`output_info` |
| `single_page` 可选后处理 | `page_filling_black_edge`、`black_edge_result_clone`、`recog_text_rotation`、`deskew_page_by_text_line` |
| `color_mode` | `imread`、`normalize_input`、`algorithm_lock_wait`、`color_filter`、`result_clone`、`passthrough_copy` |
| `provider` | `output_info`、`codec_lock_wait`、`output_info_imread` |
| `format_convert` | `imread`、`resize`、`prepare_jpeg`；桌面还记录 `image_engine_*` |
| `image_dpi` 写入 | `write_metadata`、`rewrite_jpeg`、`copy_jpeg_payload`、`close_jpeg_files`、`rename_jpeg` |
| `thumbnail` | `codec_lock_wait`、`imread`、`resize`、`imwrite`；桌面为 `image_engine_thumbnail` |
| `image.process` | `total`、`temporary_cleanup` |

`imread` 包含文件读取与解码，`imwrite` 包含编码与文件写入。这里不将 OpenCV API
内部的 CPU 编解码和系统写入强行拆开。`read_file_bytes` 是现有完整文件读取及
buffer 构建，不是纯磁盘吞吐测试。通用图像工具仅在上述 Graphic 操作的有效 trace
内采集详细阶段，避免对预览帧等其他调用持续输出日志。

现有 `graphic.single_page options` 同时记录 `scan_device_type`、分页、黑边、旋转、
圆角和裁边参数。对比直接测试时，应先核对 HSC=2 等参数；纯算法对比使用
`stage=single_cut_pipeline` 和 `stage=color_filter` 的 `duration_ms`。

当前鸿蒙构建的真实 Provider 源码仍位于主仓库 `src/sdk_private/providers/`，
由子工程显式链接。计时实现位于该实际调用链，日志说明和汇总脚本归入 SDK Open 子工程。

## 仅鸿蒙启用的 DPI 读取优化

`SDK_OPEN_HARMONY` 构建使用流式 JFIF 解析：验证 JPEG 文件及段长度，仅读取
APP0 的固定头部，跳过其他头部段，遇到 SOS/EOI 停止，不读取或缓冲压缩图像数据。
EXIF 继续使用现有 libexif；EXIF/JFIF 优先级、有效范围、单位换算、四舍五入和
缺省 72 DPI 的规则保持不变。PNG、TIFF、BMP 解析器也保留现状。

鸿蒙 Provider 在 `image.process` 中将已解析的 DPI 传给页面处理；页面和颜色处理
在已有正数 DPI 时跳过元数据读取，否则各解析一次。复用范围限于当前请求，不引入
全局文件缓存。日志中 `metadata_dpi=0 source=request` 表示直接采用当前调用的 DPI，
此时没有重新读取元数据；该 DPI 也可能由上一处理阶段传入。

最终格式转换仍从其输入文件读取 DPI，以保留现有公共请求结构及接口行为。因此，
单页 JPEG → 颜色处理 → JPEG 转换通常由五次元数据解析减少为两次；直接调用页面或
颜色处理且没有指定 DPI 时，由两次减少为一次。`read_jfif` 阶段仍保留计时，鸿蒙下
不再出现该阶段内的 `read_file_bytes` 子阶段。

非鸿蒙构建保留原 JFIF 全文件解析和 Provider DPI 解析路径。此次优化不修改 mock，
不更换图像编解码库，也不修改 CZCV 算法调用。

## 仅鸿蒙启用的输出宽高复用

鸿蒙真实 Provider 在输出文件成功写入后，复用最终 `cv::Mat` 的宽高：

- 单页处理（包括多目标及区域裁剪）从每张完成后处理的图像填充 `SdkPageOutput`。
- 颜色处理从算法结果填充已有 `SdkColorModeResult.width/height`。
- 综合处理最终输出在不缩放时沿用颜色处理结果的宽高。

信息查询在宽高均为正数时记录 `single_page.output_info_reuse` 或
`provider.output_info_reuse`，文件大小仍从实际输出文件通过 `stat` 获取。
未获得完整宽高（例如颜色直通）、独立的保留原图查询、或综合处理引入缩放时，
继续回退到原有解码查询。宽高来自最终处理图像，因此包含旋转、裁剪后的尺寸。

对已验证的单页 → 彩色 → JPG 综合处理，这会移除三次仅为获取信息的完整解码，
保留原图、颜色处理输入及格式转换输入的三次业务解码。非鸿蒙保留原解码路径；
公共 DTO、接口、算法参数、编码质量和中间文件流程均不改变。

该项已完成 C++11 本机辅助函数验证（JPEG/PNG/TIFF、多个输出索引、宽高缺失或
不完整时的回退、空指针和文件不存在）、鸿蒙原生库编译及实体机部署回归。

2026-11-18 实体机使用相同测试输入和单页／彩色／JPG 参数，观察结果如下。
以下为 Provider 耗时，不包含上传和浏览器显示结果的时间：

| 版本及处理时间 | 总耗时 | DPI 读取 | 完整解码次数 | 宽高复用次数 |
| --- | ---: | ---: | ---: | ---: |
| 优化前 17:27 | 6719.932 ms | 3952.201 ms（5 次） | 6 | 0 |
| DPI 优化后 18:13 | 3163.385 ms | 0.691 ms（2 次） | 6 | 0 |
| 宽高复用后 18:35 | 2051.937 ms | 0.897 ms（2 次） | 3 | 3 |
| 宽高复用后复测 18:57 | 2343.625 ms | 0.719 ms（2 次） | 3 | 3 |

三次宽高复用分别累计耗时 0.040 ms、0.057 ms；两次均成功返回，输出尺寸
2746 × 1949。宽高复用前对应的三次查询解码累计为 496.741 ms。
总耗时变化还包含算法、编码和文件读写波动，不能将全部差值归因于宽高复用。
非鸿蒙验证为本机辅助函数回归，不表示完成 Windows/Linux 全量构建或部署。
