# SDK Open 鸿蒙 CZOCR 初始化排查（2026-10-29）

范围：独立 `sdk_open_harmony` 服务；未修改桌面端 OCR 初始化流程。

GBrain：当前环境未暴露工具，以下结论来自本地代码、模拟器对照实验和原生崩溃报告。

## 已修复：模型目录缺少末尾分隔符

鸿蒙宿主原来将 `SDK_OCR_MODEL_DIR` 设置为 `runtime/models`。
新 CZOCR API 的授权检查直接拼接 `license.lic`，需要目录以 `/` 结尾。

在同一个模拟器中，使用本次新 API 库、同一份授权文件和项目现有授权参数对照：

| 路径 | 返回值 | isPositive | limit |
| --- | --- | --- | --- |
| `.../models` | -1 | 0 | 0 |
| `.../models/` | 1 | 1 | 20691001 |

鸿蒙宿主现已显式补上末尾 `/`。SDK Open 会话和配额鉴权原本就成功，
页面 `1902 授权验证失败` 来自底层 OCR 错误 16，并非 API Key 无效。

## 未解决：新库初始化崩溃

安装新 HAP 后确认沙箱标记为 `.sdkopen-czocr-model-20261029B`，算法库哈希为：

```text
libczcv_ocr.so SHA-256:
28acdec9e148e2e3e6ae660656ee7739661eed5f755ea6adcd3afda3f602011e
libczcv_ocr_api.so BuildID:
f460e53cfc114da29f6a973cc7ace6c8f519bdb1
```

修正路径后实际调用 `ocr.extract_text`，服务在初始化阶段 SIGSEGV：

```text
memcpy
czcv_ocr::decrypt_model(...) +556
czocr_load_library +164
czocr_init_in_dev_stage +228
editor::ocr::Ocr::initModule(...)
```

崩溃报告记录算法数据长度相关寄存器 `x22=0x464c457f`，恰为输入算法库的 ELF 文件头。
该证据支持当前 API 的解密加载路径与提供的普通 ELF 算法库格式不匹配的判断，
仍需库提供方结合实现确认。
进一步用仅 dlopen 新 API 库并调用现有授权检查和初始化接口的独立小程序验证：
授权检查成功，随后 `czocr_init_in_dev_stage` 同样触发 Signal 11，无需 SDK Open Provider。

原生崩溃报告设备路径：
`/data/log/faultlog/faultlogger/cppcrash-com.czur.sdkopen-20020054-20261029154306987.log`。

需由库提供方确认普通 ELF 算法库对应的鸿蒙初始化接口，或提供与当前
`czocr_init_in_dev_stage` 解密加载方式配套的库产物。头文件中虽有 Android 专用接口，
当前未将其作为鸿蒙替代接口，也未改写三方二进制或绕过授权。

## 构建、安装与恢复结果

`src/sdk_open/scripts/harmony/build_sdk_open_app.sh` 构建成功，30 个原生库的 ABI/NEEDED 检查通过。
经用户授权卸载旧包后，`src/sdk_open/scripts/harmony/run_sdk_open_app.sh --target 127.0.0.1:5555`
安装成功并启动托盘控制入口。本次复用已编译的鸿蒙三方库产物。

崩溃后已通过托盘重新启动服务，管理接口 `/api/status` 返回 `running=true`，
恢复后的进程 PID 为 `16672`。这只证明服务恢复运行，OCR 初始化崩溃仍未解决。
