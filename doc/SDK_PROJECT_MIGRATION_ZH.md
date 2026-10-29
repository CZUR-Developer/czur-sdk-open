# SDK Open 子工程迁移与验证

2026-10-29 将 SDK 产品实现统一到 `src/sdk_open`。主仓迁移前基线为
`feature/harmony_1.0.0`，SDK 子模块基线为 `bf7e58e`。

## 归属与构建

- `src/sdk_private` 的 Provider、鉴权和运行支持迁入 `providers/private`。
- C ABI、Java/JNA、鉴权工具和测试分别位于 `bindings`、`tools/auth`、`test/private`。
- 根部鸿蒙工程迁入 `platform/harmony`；N-API 与宿主桥接位于 `native`。
- 旧 `sdk_app` 入口保留在 `runtime/legacy`，target 与对外 ABI 名称保持。
- SDK 版本、安装配置、发布工具和 CI helper 位于 `version`、`packaging`、`scripts`、`ci`。
- 主仓保留共享模块、三方库、Jenkins 入口及少量兼容转发脚本。
- `providers/mock` 源码、mock 开关和鸿蒙装配保持迁移前行为，不在本轮调整能力。

根路径由 `cmake/Dependencies.cmake` 和 `scripts/sdk-paths.sh` 统一：
`SDK_OPEN_SOURCE_ROOT` 为子工程，`SDK_OPEN_HOST_SOURCE_ROOT` 为主工程，
`SDK_OPEN_THIRD_PARTY_ROOT` 为三方库。后两个支持显式配置，嵌入主工程时按原目录默认检测。
鸿蒙不再改写 `CMAKE_SOURCE_DIR` 或创建源码符号链接；桌面和鸿蒙统一引用
`cmake/PrivateProviderSources.cmake`。使用现有 OHOS 三方产物，不重新编译三方库。

桌面真实能力继续从主仓配置，以取得共享模块 target：

```bash
cmake -S . -B build/sdk-open -G Ninja -DBUILD_SDK_OPEN=ON \
  -DBUILD_SDK_PRIVATE=ON -DSDK_OPEN_USE_PRIVATE_PROVIDER=ON
cmake --build build/sdk-open
```

鸿蒙从主仓执行：

```bash
src/sdk_open/scripts/harmony/build_sdk_open_app.sh
src/sdk_open/scripts/harmony/run_sdk_open_app.sh --target 127.0.0.1:5555
# 从托盘启动服务后
src/sdk_open/scripts/harmony/run_sdk_open_app.sh --target 127.0.0.1:5555 \
  --skip-install --no-launch --smoke
```

整包路径为 `platform/harmony/build/outputs/default/harmony-default-unsigned.app`。
该包用于模拟器；正式设备仍需发行签名。

## 版本预留与 CI

SDK 版本源为 `version/version.ini`，构建版本记录 SDK 子模块提交。
Jenkins 保留主仓入口，初始化子模块后加载子工程 helper；SDK 专用构建、冒烟和发布脚本
由 `ci` 管理。子模块初始化/重试等仓库管理脚本仍留在主仓。

`ci/scripts/reserve_build_number.py` 先在 SDK 远端分支预留版本、提交并推送，再以独立提交
更新主仓 `src/sdk_open` gitlink。返回的 `BUILD_COMMIT` 仍为主仓提交，远端 Linux、Windows
和 macOS 构建据此复现相同 SDK。并发更新时检查祖先关系，不回退子模块指针。
默认优先使用与主仓同名且包含固定提交的 SDK 分支，再使用 `.gitmodules` 声明分支；
可通过 `SDK_OPEN_BRANCH` 指定已有分支，不自动创建分支。
迁移验证使用本地 bare remotes，不触发远端 CI、版本预留或发布。

## 许可与本地配置

迁移的私有实现和旧入口保留原许可，不机械添加 Apache 许可头。
本地 TLS 私钥、签名配置、模型与构建产物不纳入新提交；TLS 历史测试密钥仍在本地忽略路径。
鉴权源码保留既有内置授权常量，本轮不改变鉴权语义。该实现从主仓迁入公开 SDK 仓库后，
在向公开 GitHub 推送前应先处理这些既有常量的发行方式；本轮不自动公开推送迁移内容。

## 验证记录

- 独立核心构建通过；mock 源码 diff 为空；鸿蒙 `Start` 装配实现与迁移前一致。
- 鸿蒙整包构建通过，30 个原生库 ABI/NEEDED 检查通过。
- 2in1 模拟器卸载旧包后重装成功，真实托盘注册、托盘启停与重启、健康/状态/Demo/资源 HTTP 冒烟通过。
- macOS arm64 真实 Provider、鉴权 C ABI、鉴权工具及全部配置的构建目标编译链接通过。
- SDK CI 测试 87 项中 78 项通过，9 项因缺少 PowerShell 跳过；签名脚本 2 项通过。
- macOS 安装脚本 TLS 回归通过；另一个既有 runtime-config 独立脚本在客户密钥保护断言处失败，
  对应测试和运行时代码本轮均未修改。
- 主仓 Jenkins 接入测试 23 项中 22 项通过，1 项因缺少 `pwsh` 无法执行。
- CTest 5 项中 4 项通过；状态测试仍要求 software 只有 3 字段，但基线已返回 platform 第四字段。
- C ABI、鉴权服务、图片写入与真实图片增强 smoke 通过。macOS 测试需设置构建输出
  `DYLD_LIBRARY_PATH` 和原有 `SDK_GRAPHIC_MODEL_DIR`。
- 两个既有鉴权 smoke 未通过：master-secret 测试仍硬编码旧默认值；离线激活测试在自动离线权益配额断言处失败，
  本机另有设备 helper 版本低于主仓最低版本的日志。本轮保留这些测试与业务语义。
- Windows/Linux 原生构建和真机签名验证尚未在本机执行。

OCR 的 `decrypt_model` 崩溃为迁移前已知问题，独立记录于
[sdk_open_harmony_ocr_initialization_20261029.md](sdk_open_harmony_ocr_initialization_20261029.md)。
本轮构建、服务连通性及 Provider 装配验证不代表 OCR 识别已通过。

GBrain：当前环境未暴露工具，使用本地文档和当前代码。
