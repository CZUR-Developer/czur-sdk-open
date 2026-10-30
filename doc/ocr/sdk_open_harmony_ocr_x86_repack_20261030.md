# SDK Open 鸿蒙 x86-64 模型重新打包验证（2026-10-30）

## 更新 OHOS 算法库后英文识别通过（16:11）

用户替换两个 OHOS OCR 库后，重新构建 SDK Open 鸿蒙安装包，在模拟器保留数据
卸载旧包、安装新包，并直接执行 `language=en`。本轮未重新编译三方库。
构建成功，31 个原生库 ABI/依赖检查通过。源库和包内 strip 后库的 Build ID 一致：

| 库 | 源文件 SHA-256 | Build ID |
| --- | --- | --- |
| `libczcv_ocr.so` | `1e374db6ba0a83d60f4f19bc9385bef91857e2b3a9b3345bce91421f4f63155b` | `fdcb847a5d2f1d0ef08d8640a46d34ee14e39958` |
| `libczcv_ocr_api.so` | `2349bfe08614381a0288b3ce4dc2a8151037a971ff349c8d7b605b54e8423b96` | `db2f5d0dfe3a311f4406eff5f012eb42750fe0e7` |

算法库相对上一轮发生变化，API 库内容相同。模型未变，x86-64 下 54 个模型/字典
文件与设备逐一核验 SHA-256 一致。实测 `auth.create_session` 返回 `code=0`、
图片上传 HTTP 200，`ocr.extract_text(language=en)` 返回 `code=0`、
`recognized=true`，准确识别以下三行（置信度约 0.983、0.985、0.990）：

```text
CZUR OCR TEST
Harmony Service 2026
Invoice 12345 Total 678.90
```

随后同一 `en` 参数的 PDF 识别任务完成，下载 PDF 为 8,480,289 字节，
`pdftotext -layout` 验证文字层包含上述全部三行。本轮没有新增 SDK Open 原生
崩溃日志，测试结束服务仍运行（PID 11006）。本轮样本的英文提取及 PDF 导出通过，
此前 Gemm 崩溃未复现；这不替代其他语种或长时间稳定性回归。

验证资料位于 `platform/harmony/build/validation/ocr-20261030-newlib-en/`，
包括库身份及模型校验、构建安装日志、真实请求结果、识别文本、PDF 和文字层校验。
以下为更换算法库前的历史失败记录。

## 再次更新后的直接英文测试（15:31）

再次按用户要求编译、保留用户数据卸载重装，并直接测试 `language=en`。
构建成功，31 个原生库 ABI/依赖检查通过。x86-64 目录新增 `ppocr_keys_v1.txt`，
合计 54 个模型、字典文件，包内及设备全量 SHA-256 核验通过。
该 keys 文件与此前由 windows64 补齐的包内文件内容相同，因此最终模型内容版本
仍为 `ae7c304e22a618657077b3b3c610eae646300af54780e4de555b3f206df5e55c`。
其余 53 个模型/字典及两个 OHOS OCR 库的 SHA-256 与上一轮相同。

实测鉴权 `code=0`、上传 HTTP 200，`ocr.extract_text(language=en)` 后仍发生 SIGSEGV，
WebSocket 断开码为 1006。崩溃时间 `2026-10-30 15:31:23.906`，
PC `0xba03a0`，位于同一 Build ID 的 `libczcv_ocr.so` 的 `ncnn::Gemm::forward`。
此次没有返回英文识别结果，英文模型兼容问题仍未解决。
资料保存在 `platform/harmony/build/validation/ocr-20261030-x86-update3/`。

## 最新补充模型后的直接英文测试（15:26）

用户再次补充模型后，重新编译并安装到 Harmony 6.0 模拟器，直接请求 `language=en`。
最新 x86-64 目录共 40 个 `.model` 和 13 个 `.dat`，53 个文件均已核对源文件、安装包、
设备沙箱的 SHA-256，一致。新增的 7 个文件为 `3_1_3.model`、`3_1_5.model`、
`3_2_1.model`、`3_2_15.model`、`3_2_16.model`、`3_3_1.model`、`3_5_1.model`。

本次新增模型内容哈希版本机制，设备已确认使用新版本标记：
`ae7c304e22a618657077b3b3c610eae646300af54780e4de555b3f206df5e55c`。
后续模型更新会自动刷新沙箱资源。构建成功，31 个原生库检查通过；安装遇到空间不足后，
保留用户数据卸载旧包，再安装成功。本轮没有重编译三方库，也没有使用 auto 模式替代 en。

实测鉴权返回 `code=0`，上传返回 HTTP 200，英文文字提取随后导致服务 SIGSEGV，
客户端收到 WebSocket 1006，没有取得识别结果。最新崩溃时间为
`2026-10-30 15:26:44.163`，PC `0xba0970` 落在同一算法库的
`ncnn::Gemm::forward` 内（函数范围 `0xba01c8` 至 `0xba0acc`），
Build ID 为 `d6b4fc1c4bb1db871016c6cf941fc33cc9c2d84f`。
该次 PC 与此前 `0xba03a0` 不同，但仍位于同一个函数。

英文实际使用的 `3_2_6.model`、`6.dat` 哈希仍为下文所列值，未因新增的 7 个文件而改变。
本轮未解决英文模型与 OHOS Gemm 实现的兼容问题；已排除遗漏拷贝及旧缓存。
测试资料保存在 `platform/harmony/build/validation/ocr-20261030-x86-update2/`，
包括包内/设备全量校验、真实请求结果、崩溃日志及构建、安装和恢复日志。

以下为当天较早一轮的 46 文件验证记录。

## 打包与安装

按用户补充 x86-64 模型后的要求重新执行了构建、模拟器覆盖安装及真实 API 验证。
此前只有 `3_1_1.model` 从 x86-64 覆盖，本次改为优先使用该目录的全部模型与字典。

- 来源：`third_party_repo/czocr/models/x86-64`。
- 全部 33 个 `.model`、13 个 `.dat` 均复制，共 46 个文件。
- 目录中缺少的 v3 模型和 keys 文件继续由 windows64 完整集补齐。
- 不复制 x86-64 的 Linux 动态库；原生运行库仍来自 `czocr/lib/ohos`。
- 授权文件沿用此前验证通过的 loongarch64 文件。
- 模型缓存标记更新为 `.sdkopen-czocr-model-20261030H`，确认设备已经重新释放模型。
- 每个源模型／字典均与 HAP 内文件及设备沙箱文件分别核对 SHA-256：46/46 一致，
  缺失 0、内容差异 0。
- `build_sdk_open_app.sh` 构建通过，31 个原生库 ABI / NEEDED 校验通过。
- `run_sdk_open_app.sh` 覆盖安装成功，用户数据保留。

## 实测结果

测试使用同一张 1600 × 600 图片，经真实鉴权、上传、WebSocket OCR 接口调用。

| 项目 | 结果 |
| --- | --- |
| `auth.create_session` | code 0 |
| 图片上传 | HTTP 200 |
| `ocr.extract_text`，auto | code 0，4 个文字块，中英文内容正确 |
| `ocr.recognize`，auto PDF | completed，8,480,511 字节 |
| 下载 PDF 后执行 `pdftotext` | 中英文文字层内容校验通过 |
| 服务 / health / status / Demo / asset 冒烟 | 通过 |
| `ocr.extract_text`，en | 仍崩溃，WebSocket 1006 |

自动模式实际返回并在 PDF 文字层中校验的内容：

```text
CZUR OCR TEST
Harmony Service 2026
Invoice 12345 Total 678.90
鸿蒙文字识别测试
```

本次英文崩溃时间为 **2026-10-30 15:17:48.208（设备时间）**，仍在
`libczcv_ocr.so` 偏移 `0xba03a0`（`ncnn::Gemm::forward +472`），信号 SIGSEGV。
算法 Build ID 仍为 `d6b4fc1c4bb1db871016c6cf941fc33cc9c2d84f`。

实际用于英文路径的两个文件与前一轮失败时哈希相同，并已确认安装包和设备都使用这些文件：

```text
3_2_6.model
2c1ff5ea54001b14ecdb3e6754f256f2dd0f8e92bb16cd4c397c0c2a635cf7cc
6.dat
5a7180ee079d06678be9743313698e7d21fed5a808327723a3d3c992a94d3925
```

本次已排除漏拷贝 x86-64 模型或设备缓存仍使用旧模型的情况。
英文故障仍符合此前确认的模型常量 Gemm 与 OHOS 实现不兼容问题，尚未修复。

## 验证资料

生成目录：`platform/harmony/build/validation/ocr-20261030-x86-repack/`。

- `packaged-models.json`：46 个文件的源、包内字节及哈希校验。
- `device-model-verification.json`：46 个设备文件的完整一致性校验结果。
- `result-auto.json`、`result-en.json`：真实接口验证结果。
- `recognized-auto.pdf`、`pdf-text-auto.txt`：实际导出 PDF 与文字层。
- `en-crash-full.log`：本次新崩溃的完整日志。
- `build.log`、`install.log`、`smoke.log`：构建、安装及冒烟记录。

上述资料为本地生成产物，不纳入 Git；清理构建目录前需要另行保存。
本次未修改 mock 或三方库二进制。

GBrain：当前环境未暴露工具，按本地源码、安装包与模拟器实测验证。
