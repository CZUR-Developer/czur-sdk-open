# SDK Open 鸿蒙 OCR 验证记录（2026-10-30）

## 后续更新

16:11 用户更新 OHOS 算法库后，显式 `language=en` 的文字提取、PDF 导出及文字层
核验均已通过，本轮未新增崩溃日志。最新结果见
[重新打包验证记录](sdk_open_harmony_ocr_x86_repack_20261030.md)。
下文保留更换算法库前的适配及验证过程，其中“当前”均指当时使用的旧库。

## 更换算法库前的结论

鸿蒙模拟器已安装修复包，托盘控制的服务正常运行。原先的
`1902 / OCR初始化失败` 已解决，自动/中文模式的中英文混合文字提取、双层 PDF 导出
和 PDF 文字层提取均通过。**指定 `language=en` 的推理仍会使服务崩溃，整体 OCR 验收尚未全部通过。**

## 已确认的问题与修正

1. CZOCR 包装层按 `modelDir + "./libczcv_ocr.so"` 从应用数据目录加载算法，
   在鸿蒙 AppService 中失败。单纯 chmod、创建指向 native 库的符号链接或预加载 native
   库均未解决包装层的再次加载。现在仅在 SDK Open 鸿蒙宿主链接时包装初始化函数：
   从 HAP native 目录调用原 CZOCR loader 建立分发表，再调用同一算法初始化入口。
   上层原有授权检查、初始化参数和后续 SDK API 调用保持原流程。
2. 算法依赖的 `libz.so` 需要以该名称进入 HAP native 目录。
3. 原 x86 模型目录缺少算法请求的 v3 模型。缺失 `3_1_1.model` 时，日志报打开失败，
   算法继续处理空数据，在 `dissolve_v2` 越界读取。打包使用完整通用模型集，并明确
   使用用户补充的 `third_party_repo/czocr/models/x86-64/3_1_1.model` 覆盖检测模型。
   HAP 内该文件与指定源文件逐字节一致，SHA-256：

   ```text
   8a04ffae2d1c565e678215b95286ab79050c2c76d56b86fbe20854835921c6c8
   ```

4. PDF 文字层需要 `SourceHanSansCN-Normal.otf`。字体随 HAP rawfile 释放，
   共享 OCR 模块通过 `SDK_OCR_FONT_DIR` 查找，其他平台保留原有字体查找路径。
5. 构建校验增加算法库位置、zlib、必要模型及字体的缺失检查。

模型数据使用 `windows64` 完整集补齐（37 个 `.model` / `.dat` 文件与 `mac-arm64`
逐个相同），授权文件沿用 loongarch64 目录；**运行库仅使用 `czocr/lib/ohos`**。
未修改 mock、三方库二进制或用户提供的检测模型。

## 最终安装包的实际验证

- `build_sdk_open_app.sh`：构建通过，31 个 native 库的 ABI / NEEDED 检查通过。
- `run_sdk_open_app.sh`：保留用户数据卸载并重装成功，从系统托盘启动服务。
- `auth.create_session`：`code=0`。
- 图片上传：HTTP 200。
- `ocr.extract_text`，`language=auto`：`code=0`，返回 4 个文字块：

  ```text
  CZUR OCR TEST
  Harmony Service 2026
  Invoice 12345 Total 678.90
  鸿蒙文字识别测试
  ```

- `ocr.recognize`，PDF：任务达到 `completed`、进度 100，输出 8,480,511 字节。
- 使用 `pdftotext` 从设备实际生成的 PDF 提取出以上中英文文字，确认存在文字层。
- 服务、health、status、Demo 页面和 asset API 冒烟检查通过。
- SDK 许可证头检查、相关 Git diff 空白检查通过。

本地完整验证资料位于：

```text
platform/harmony/build/validation/ocr-20261030-final/
```

包括请求结果、构建/安装/冒烟日志、运行日志、实际导出 PDF 与提取文本。
该目录为生成产物，执行 `--clean` 后会被删除。

## 尚未解决的指定英文模式

`language=en` 进入 SVTR / NCNN Gemm 推理后崩溃，常见位置为
`ncnn::Gemm::forward`，当前算法 Build ID：

```text
d6b4fc1c4bb1db871016c6cf941fc33cc9c2d84f
```

API 库 Build ID：

```text
db2f5d0dfe3a311f4406eff5f012eb42750fe0e7
```

完整模型和字典的设备哈希已与源文件核对。独立探针的部分单线程运行通过，但应用进程
中仍可复现崩溃。限制线程数、关闭 OpenMP 亲和性、禁用 vendor 库 strip 均未稳定解决
应用中的失败，已撤回这些试验性调整。不能据此宣称其中任一项是英文崩溃的根因，
也不能以自动模式能识别拉丁文字替代指定英文模型的验收。

后续模型结构探针已确认实际英文路径加载 `3_2_6.model`，其单输入常量 Gemm 层
使用参数 4～10，而本次 OHOS 库的 Gemm 只读取参数 0～3，且没有常量权重加载实现。
该模型 / 运行库能力不匹配导致第二个动态输入越界访问。
详见 [指定英文识别失败分析](sdk_open_harmony_english_failure_analysis_20261030.md)。

当前仓库仅有 OCR 头文件和预编译库，未找到对应算法 / NCNN 实现源码；继续定位需要
对应源代码、调试产物或供应商对该推理崩溃的修复。当前未更换算法库。
