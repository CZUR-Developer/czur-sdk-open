# sdk_open / sdk_private 上传、DPI 与测试结构现状

本文只记录当前保留的最终状态，不再描述中间迁移过程。

## 1. 上传与图片输入规则

当前上传链路规则：

1. 文件名有后缀时，优先按后缀判断是否允许上传。
2. 文件名无后缀时，退回按 MIME 判断。
3. 后缀不支持时，即使 MIME 看起来合法，也不会放行。
4. 当前上传阶段还没有增加“真实文件内容解码校验”。

当前允许的图片后缀：

- `jpg`
- `jpeg`
- `png`
- `bmp`
- `tif`
- `tiff`
- `webp`

当前允许的文档后缀：

- `pdf`
- `ofd`

当前 `IsSupportedImageMimeType()` 已兼容：

- `image/*`
- `application/bmp`
- `application/x-bmp`
- `application/tif`
- `application/tiff`

图片类能力在真正执行前，还会统一做“是否为图片输入”的入口校验；非图片输入会返回更准确的错误信息，例如：

- `image.process only supports image inputs`
- `ocr.extract_text only supports image inputs`
- `recognition.barcode_detect only supports image inputs`
- `image.enhance only supports image inputs`

对应实现：

- [src/sdk_open/application/command_application_service.cpp](src/sdk_open/application/command_application_service.cpp:161)
- [src/sdk_open/application/command_application_service.cpp](src/sdk_open/application/command_application_service.cpp:1911)
- [src/sdk_open/application/command_application_service.cpp](src/sdk_open/application/command_application_service.cpp:1997)

## 2. DPI 与 JPEG/拍照链路

当前 `sdk_private` 已统一通过 `ReadImageDpiFromMetadata()` 读取以下格式的 DPI：

- `jpeg`
- `png`
- `tiff`
- `bmp`

DPI 处理优先级：

1. 优先使用请求显式传入的 `dpi`
2. 否则读取原图 metadata DPI
3. 仍失败时回退默认 DPI

JPEG 写出当前有两条路径：

1. 已有 JPEG 字节流时，直接走 `WriteJpegBytesWithDpi()`
2. 手里是 `cv::Mat` 时，走 `WriteImageWithDpiLocked()`，内部先 `imencode` 再一次写出

拍照 JPEG 输出当前行为：

1. 原始 MJPEG 尺寸已匹配目标尺寸时，直接用原始 bytes 写最终 JPEG 并带 DPI
2. 需要插值时，只做一次 JPEG 解码，在内存里 `resize` 后一次写出最终 JPEG

这意味着当前拍照链路已经不再走旧的“先写原 JPEG -> 再读回 -> resize -> 再写 JPEG -> 再回写 DPI”路径。

当前 `sdk_private` 已统一通过 `WriteImageDpi()` / `WriteImageWithDpiLocked()` 写回以下格式的 DPI：

- `jpeg`
- `png`
- `tiff`
- `bmp`

其中：

- `jpeg` 通过 JFIF/EXIF 写回
- `png` 通过 `pHYs` chunk 写回
- `tiff` 通过 `libtiff` 写回
- `bmp` 通过 `biXPelsPerMeter/biYPelsPerMeter` 写回

图像处理分支中的 DPI 透传也已打通：

1. `ProcessPage` 先解析 `resolved_request.dpi`
2. 页处理输出会带上 `output.dpi`
3. `ApplyColorMode` 优先使用页输出 DPI，没有则回退请求 DPI
4. `ConvertImageFormat` 默认读取输入图 DPI 作为输出 DPI

对应实现：

- [src/sdk_open/providers/private/image_write_utils.cpp](src/sdk_open/providers/private/image_write_utils.cpp:876)
- [src/sdk_open/providers/private/image_write_utils.cpp](src/sdk_open/providers/private/image_write_utils.cpp:919)
- [src/sdk_open/providers/private/image_write_utils.cpp](src/sdk_open/providers/private/image_write_utils.cpp:980)
- [src/sdk_open/providers/private/image_write_utils.cpp](src/sdk_open/providers/private/image_write_utils.cpp:1003)
- [src/sdk_open/providers/private/image_write_utils.cpp](src/sdk_open/providers/private/image_write_utils.cpp:1027)
- [src/sdk_open/providers/private/private_device_provider.cpp](src/sdk_open/providers/private/private_device_provider.cpp:307)
- [src/sdk_open/providers/private/private_device_provider.cpp](src/sdk_open/providers/private/private_device_provider.cpp:1017)
- [src/sdk_open/providers/private/private_device_provider.cpp](src/sdk_open/providers/private/private_device_provider.cpp:1152)
- [src/sdk_open/providers/private/private_graphic_provider.cpp](src/sdk_open/providers/private/private_graphic_provider.cpp:161)
- [src/sdk_open/providers/private/private_graphic_provider.cpp](src/sdk_open/providers/private/private_graphic_provider.cpp:255)
- [src/sdk_open/providers/private/private_graphic_provider.cpp](src/sdk_open/providers/private/private_graphic_provider.cpp:320)
- [src/sdk_open/providers/private/graphic/color_mode_processor.cpp](src/sdk_open/providers/private/graphic/color_mode_processor.cpp:63)
- [src/sdk_open/providers/private/graphic/format_convert_processor.cpp](src/sdk_open/providers/private/graphic/format_convert_processor.cpp:92)

当前已完成多轮真实服务回归，已覆盖：

1. JPEG 输入
2. PNG 输入
3. BMP 输入
4. TIFF 输入

已确认以下真实链路输出 DPI 均保持源图 `320`：

- `image.process_page`
- `image.apply_color_mode`
- `image.enhance` 离线链路：
  - `rotate`
  - `crop_enhance`
  - `red_green_head`
  - `blank_page_detect`
  - 多 step `rotate -> crop_enhance`

另有一个显式改 DPI 的真实链路：

- `image.enhance.normalize_spec` 最终输出按 step 参数写为 `300`

这次补齐的关键问题是：

- `image.enhance` 里的 `rotate`
- `image.enhance` 里的 `crop_enhance`
- `image.enhance` 里的 `red_green_head`

之前会因为 step 内直接写文件、没有复用公共 DPI 保持逻辑，导致输出回落为默认 `72 DPI`；当前已统一改为复用 `WriteImageWithDpiLocked()` / `WriteDpiToExistingImage()`。

其中最新一轮真实结果确认：

- `image.apply_color_mode.no_optimize` 实际 DPI 为 `320x320`
- `image.enhance.rotate.manual.jpg` 实际 DPI 为 `320`
- `image.enhance.rotate.auto.png` 实际 DPI 为 `320`
- `image.enhance.red_green_head.tiff` 实际 DPI 为 `320`
- `image.enhance.crop_enhance.padding.png` 实际 DPI 为 `320`
- `image.enhance.blank_page_detect.jpg` 实际 DPI 为 `320`
- `image.enhance.multi_step.rotate_then_crop.png` 实际 DPI 为 `320`
- `image.enhance.normalize_spec.jpg` 实际 DPI 为 `300`

当前还不能把 `image.enhance` 说成“全模式都已真实通过”：

- 在线 step `doc_crop_enhance`
- 在线 step `remove_handwriting`
- 在线 step `doc_repair`
- 在线 step `remove_moire`

在当前服务环境下真实执行仍失败，现象是：

- `online image enhance request failed: Could not establish connection`

因此目前准确结论是：

- `image.enhance` 离线 step 已完成 DPI 修复并真实回归通过
- `image.enhance` 在线 step 还需要单独排查连通性问题

## 2.1 `file.convert` 图片源直连 API 真实回归

这轮额外补了 `file.convert` 的独立真实服务回归，但只统计图片源分支：

- `source.type = image`
- 输入格式：
  - `jpg`
  - `png`
  - `bmp`
- 输出格式：
  - `jpg`
  - `png`
  - `tiff`
  - `pdf`

这轮不纳入结论的范围：

- `source.type = pdf`
- `source.type = ofd`
- `source.type = tiff` 的文档源分支

原因是这几条分支内部仍可能命中 `czur-file-convert-provider` 里的固定 `72 DPI` 逻辑；按当前要求，这部分先忽略，不放进本次 DPI 结论。

真实回归输入类型：

- `jpg`
- `png`
- `bmp`

其中 `png/bmp` 是由同一张 `320 DPI` JPEG 生成，用于排除“只有 JPEG 源保 DPI”的误判。

真实回归请求规则：

1. `source.type` 必须传 `image`，不能传具体后缀 `jpg/png/bmp`
2. `target.type` 分别传 `jpg/png/tiff/pdf`
3. `options.export_type` 统一传 `single-page`

真实回归结果：

- `jpg -> jpg/png/tiff`：全部成功，输出实际保持 `320 DPI`
- `png -> jpg/png/tiff`：全部成功，输出实际保持 `320 DPI`
- `bmp -> jpg/png/tiff`：全部成功，输出实际保持 `320 DPI`

这组图片输出实际命中的 provider 是：

- `czur-graphic-provider`

也就是 `sdk_open` 里的 `graphic_facade_.ConvertImageFormat()` 分支，而不是固定 `72` 的文档转换 provider 分支。

`image -> pdf` 也已真实跑通，但这轮只确认：

- 接口成功
- 输出文件存在

不把它纳入图片 DPI 保持结论，因为 PDF 输出不直接按图片 metadata DPI 去读。

## 3. 错误码与 HTTP 返回

上传错误码已拆分：

- `UploadFileEmpty = 1005`
- `UploadFileTooLarge = 1006`

对应文案：

- `uploaded file is empty`
- `uploaded file exceeds 50MB limit`

HTTP 层当前会把这类参数/上传错误尽量映射成 JSON `400`，避免前端落成纯文本 `404` 难以定位。

对应实现：

- [src/sdk_open/interfaces/sdk_status_code.h](src/sdk_open/interfaces/sdk_status_code.h:8)
- [src/sdk_open/transport/sdk_http_server.cpp](src/sdk_open/transport/sdk_http_server.cpp:24)

## 4. 测试结构

当前测试入口按职责拆分：

- 根目录 `test/` 只承载顶层或跨模块测试
- `sdk_private` 相关 smoke test 全部由 `src/sdk_open/providers/private` 模块内管理

当前有效入口：

- [src/sdk_open/providers/private/CMakeLists.txt](src/sdk_open/providers/private/CMakeLists.txt:286)
- [test/CMakeLists.txt](test/CMakeLists.txt:1)
- [CMakeLists.txt](CMakeLists.txt:457)

`sdk_private` 相关测试文件位置：

- [src/sdk_open/test/private/sdk_private_c_api_smoke_test.cpp](src/sdk_open/test/private/sdk_private_c_api_smoke_test.cpp:1)
- [src/sdk_open/test/private/sdk_private_image_enhance_provider_smoke_test.cpp](src/sdk_open/test/private/sdk_private_image_enhance_provider_smoke_test.cpp:1)
- [src/sdk_open/test/private/sdk_private_image_write_smoke_test.cpp](src/sdk_open/test/private/sdk_private_image_write_smoke_test.cpp:1)
- [src/sdk_open/test/private/sdk_private_image_write_benchmark.cpp](src/sdk_open/test/private/sdk_private_image_write_benchmark.cpp:1)

这样放的原因只有一个：避免根目录 `test/` 与 `src/sdk_open/providers/private` 同时注册同名 `sdk_private_*_smoke_test` target，导致 CMake 重复定义冲突。

## 5. 构建与验证

如果 CMake 已完成配置，可直接按 target 构建：

```bash
cmake --build <build-dir> --target sdk_open_upload_smoke_test -j4
cmake --build <build-dir> --target sdk_private_c_api_smoke_test -j4
cmake --build <build-dir> --target sdk_private_image_enhance_provider_smoke_test -j4
cmake --build <build-dir> --target sdk_private_image_write_smoke_test -j4
cmake --build <build-dir> --target sdk_private_image_write_benchmark -j4
cmake --build <build-dir> --target png_compression_smoke_test -j4
```

常用运行方式：

```bash
./<build-dir>/sdk_open_upload_smoke_test
./<build-dir>/sdk_private_c_api_smoke_test
./<build-dir>/sdk_private_image_enhance_provider_smoke_test
./<build-dir>/sdk_private_image_write_smoke_test
./<build-dir>/sdk_private_image_write_benchmark
./<build-dir>/png_compression_smoke_test
```

当前已验证通过的本地 target：

- `sdk_open_upload_smoke_test`
- `sdk_private_c_api_smoke_test`
- `sdk_private_image_enhance_provider_smoke_test`
- `sdk_private_image_write_smoke_test`
- `sdk_private_image_write_benchmark`

当前已验证通过的真实服务链路：

- JPEG 输入：
  - `image.process_page`
  - `image.apply_color_mode`
  - `image.enhance`
  - `file.convert`
- PNG 输入：
  - `image.process_page`
  - `image.apply_color_mode`
  - `image.enhance`
  - `file.convert`
- BMP 输入：
  - `image.process_page`
  - `image.apply_color_mode`
  - `image.enhance`
  - `file.convert`
- TIFF 输入：
  - `image.process_page`
  - `image.apply_color_mode`
  - `image.enhance`

其中 `sdk_private_image_enhance_provider_smoke_test` 当前还额外覆盖了：

- `rotate` step DPI 保持
- `crop_enhance` step DPI 保持
- `red_green_head` step DPI 保持

## 6. 一句话结论

当前改动已经完成两类收口：

- `sdk_open` 侧完成了上传后缀/MIME 与错误返回规则收口
- `sdk_private` 侧完成了 DPI 读取、JPEG 一次写出、`png/tiff/bmp` DPI 写回、拍照插值输出与测试目录结构收口

补充到 `file.convert` 后，当前可以确认：

- 图像处理链路 `image.process_page` / `image.apply_color_mode` / `image.enhance` 离线 step 已完成真实 DPI 回归
- `file.convert` 的图片源单页转 `jpg/png/tiff` 也已完成真实 DPI 回归，输出保持源图 DPI

核心收益是规则统一、错误更准确、JPEG/拍照链路减少重复读写，非 JPEG 图像处理链路、`image.enhance` 离线 step，以及 `file.convert` 图片源格式转换不再丢 DPI，同时 `sdk_private` 测试结构不再和根目录 `test/` 冲突。
