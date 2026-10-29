# SDK 内部架构实施方案（仅内部使用）

> 状态：内部实施草案
>
> 范围：仅用于 `czur-capeditor` 仓库内部，不对外公开
>
> 最后更新：2026-03-11

## 1. 背景与目标

本文档定义新 C++ SDK 子进程（`sdk_app`）的内部实施蓝图，作为 SDK 项目的雏形基础。

### 1.1 目标

- 构建一个独立于 CEF UI 主进程运行的后台 SDK 进程。
- 提供四个网络入口：
  - 两个 HTTP 站点：
    - 运行状态管理/检查站点
    - 能力演示 B/S 站点
  - 两个 WebSocket 站点（不同端口）：
    - 指令/服务/设备控制通道
    - 视频流通道
- 复用仓库内现有私有能力：
  - `graphic` 能力
  - `ocr` 能力
  - `ofd` 能力
  - `devices` 能力
- 通过稳定服务契约封装私有库和模型，不对外暴露底层实现。

### 1.2 非目标（v1）

- v1 默认不开放公网访问。
- v1 不引入 WebRTC/H.264 链路。
- v1 不替换现有 `graphic_app/publicd_app`。
- v1 不直接从当前单仓对外开源。

---

## 2. 当前仓库基线

### 2.1 已有进程架构

- 现有子进程框架位于 `src/app/process/`：
  - `graphic/`（图像处理子进程）
  - `publicd/`（公共服务子进程）
  - `interface/`（IPC 接口定义）
- 进程生命周期管理位于：
  - `src/app/common/process_utils.{h,cpp}`
- 进程类型枚举已预留 `OCR`、`OFD`：
  - `src/app/common/app_defs.h`

### 2.2 已有可复用能力库

- `src/module/devices` -> `devices_lib`
- `src/module/ocr` -> `ocr_lib`
- `src/module/ofd` -> `ofd_lib`
- `src/app/process/graphic` -> `graphic_service_lib`（已存在静态库目标）

### 2.3 工程与风格约束

- 项目整体基于 C++11 和现有 CMake 组织。
- 日志规范沿用 `spdlog`。
- JSON 处理沿用 `nlohmann/json`。

---

## 3. SDK 目标架构

### 3.1 进程模型

新增独立后台子进程：`sdk_app`。

- `sdk_app` 职责：
  - 承载 HTTP 管理站和演示站
  - 承载 WS 指令通道和视频通道
  - 统一编排 SDK 服务 API
  - 路由请求到 Facade / Provider
- 独立进程收益：
  - 避免与 UI 主进程耦合
  - 便于后续独立打包与升级
  - 便于未来开源拆分

### 3.2 分层设计

采用四层架构：

1. 传输层（Transport Layer）
- HTTP 站点托管
- WS 指令处理
- WS 视频二进制推流
- 连接与会话基础能力

2. 应用层（Application Layer）
- 请求参数校验
- 鉴权（token）校验
- 任务编排与限流
- 统一错误码映射

3. 门面层（Facade Layer）
- `DeviceFacade`
- `GraphicFacade`
- `OcrFacade`
- `OfdFacade`

4. 适配层（Provider Adapter Layer）
- 对接内部能力库：
  - `devices_lib`
  - `graphic_service_lib`
  - `ocr_lib`
  - `ofd_lib`
- 传输层不直接暴露私有结构体。

### 3.3 默认端口规划

- HTTP 管理站：`127.0.0.1:17080`
- HTTP 演示站：`127.0.0.1:17081`
- HTTP 资源服务：`127.0.0.1:17082`
- WS 指令通道：`127.0.0.1:17090`
- WS 视频通道：`127.0.0.1:17091`

所有入口默认仅监听 localhost。

### 3.4 协议基线

### 指令 WS 消息封装（JSON）

请求：

```json
{
  "request_id": "uuid-or-seq",
  "method": "device.list",
  "params": {},
  "auth": { "token": "..." }
}
```

响应：

```json
{
  "request_id": "uuid-or-seq",
  "code": 0,
  "message": "ok",
  "data": {}
}
```

事件：

```json
{
  "event": "device.online",
  "ts": 1710000000,
  "payload": {}
}
```

### 视频 WS 负载（binary）

- 视频帧采用解码后的 BGR24 二进制。
- 需附带最小元数据（可采用前导头或配套文本事件）：
  - `device_id`
  - `frame_seq`
  - `timestamp_ms`
  - `width`
  - `height`
  - `pixel_format`（v1 固定 bgr24）

---

## 4. 技术选型

### 4.1 v1 推荐技术栈

- HTTP 服务：`cpp-httplib`
- WebSocket 服务：`websocketpp + standalone asio`
- JSON：`nlohmann/json`
- 日志：`spdlog`

### 4.2 选型理由

- 与 C++11 兼容。
- 接入当前仓库成本低，落地速度快。
- 便于实现指令与视频双通道拆分。
- 若协议层稳定，后续可平滑迁移到 Beast/libhv/WebRTC。

### 4.3 延后能力

- WebRTC 视频传输：延后至 v2+。
- H.264 编码链路：延后至 v2+。

---

## 5. 未来开源准备方案（内部规划）

本节仅用于未来规划，当前产物仍为内部私有。

### 5.1 目标形态

未来采用双仓模式：

- `sdk-open`：
  - 核心传输与协议
  - 编排逻辑
  - 公共接口
  - Mock providers
  - 可直接运行的 `sdk_main.cpp`
- `sdk-private`：
  - 对接私有库/私有模型的真实 providers

### 5.2 开源前必须完成的边界

- 定义 provider 接口：
  - `ISdkDeviceProvider`
  - `ISdkGraphicProvider`
  - `ISdkOcrProvider`
  - `ISdkOfdProvider`
- 保证 `sdk-open` 在仅使用 Mock provider 时可编译可运行。
- 保证 open-core 目标中不出现私有 include path。

### 5.3 法务与合规清单

对外开源前需补齐：

- `LICENSE`（建议 Apache-2.0）
- `NOTICE`
- `THIRD_PARTY_LICENSES.md`
- `SECURITY.md`
- CI 安全扫描：
  - Secret 扫描
  - 依赖漏洞扫描
  - SBOM 导出

---

## 6. 目录结构规划

为保证未来可开源拆分，推荐采用“进程入口薄壳 + 开放核心 + 私有 Provider”目录布局。

```text
src/
  app/
    process/
      sdk/
        SDK_INTERNAL_ARCH_PLAN.md
        sdk_main.cpp
        CMakeLists.txt
  sdk_open/
    runtime/
      sdk_app.h
      sdk_app.cpp
      sdk_http_server.h
      sdk_http_server.cpp
      sdk_ws_command_server.h
      sdk_ws_command_server.cpp
      sdk_ws_video_server.h
      sdk_ws_video_server.cpp
      sdk_config.h
      sdk_config.cpp
    interfaces/
      i_sdk_device_provider.h
      i_sdk_graphic_provider.h
      i_sdk_ocr_provider.h
      i_sdk_ofd_provider.h
    providers/
      mock/
        mock_device_provider.cpp
        mock_graphic_provider.cpp
        mock_ocr_provider.cpp
        mock_ofd_provider.cpp
    frontend/
      admin-site/
        package.json
        pnpm-lock.yaml
        vite.config.ts
        src/
        public/
      demo-site/
        package.json
        pnpm-lock.yaml
        vite.config.ts
        src/
        public/
    web/
      admin/
      demo/
  sdk_private/
    providers/
      private_device_provider.cpp
      private_graphic_provider.cpp
      private_ocr_provider.cpp
      private_ofd_provider.cpp
```

### 6.1 文件归属原则（针对当前问题）

以下文件默认归入 `sdk_open/runtime/`：

- `sdk_app.h`
- `sdk_app.cpp`
- `sdk_http_server.h`
- `sdk_http_server.cpp`
- `sdk_ws_command_server.h`
- `sdk_ws_command_server.cpp`
- `sdk_ws_video_server.h`
- `sdk_ws_video_server.cpp`
- `sdk_config.h`
- `sdk_config.cpp`

前提约束：

- 上述文件不得直接依赖 `devices_lib/ocr_lib/ofd_lib/graphic_service_lib` 的私有头文件和私有类型。
- 业务能力调用必须通过 `sdk_open/interfaces/` 中的 `ISdk*Provider` 抽象接口完成。
- 任何真实私有实现仅允许出现在 `sdk_private/providers/`。

说明：

- `src/sdk_open/runtime/legacy/sdk_main.cpp` 保留为可执行入口薄壳，负责装配 provider 并启动 `sdk_app`。
- `src/sdk_open/` 承载可开源发布的核心代码。
- `src/sdk_open/providers/private/` 承载内部闭源适配实现。

### 6.2 两个 HTTP 站点前端项目归属

两个 HTTP 站点对应的 Vue 项目纳入 `sdk_open` 管理，不放在 `sdk_private`：

- 管理站前端：`src/sdk_open/frontend/admin-site/`
- 演示站前端：`src/sdk_open/frontend/demo-site/`

运行期静态资源约定：

- 管理站构建产物输出到：`src/sdk_open/web/admin/`
- 演示站构建产物输出到：`src/sdk_open/web/demo/`
- `sdk_app` 分别挂载静态路由：
  - `http://127.0.0.1:17080/` -> `web/admin`
  - `http://127.0.0.1:17081/` -> `web/demo`
- 资源服务独立监听 `http://127.0.0.1:17082/`，仅提供 `/api/assets/...` 等二进制资源接口，不挂载静态站点，避免独立前端开发服务返回 `index.html`。

约束：

- 前端项目仅通过 HTTP/WS 协议与 `sdk_app` 通信，不直接依赖私有库。
- 前端产物可在开源模式下独立构建与运行（配合 Mock provider）。

---

## 7. CMake 改造规划

### 7.1 根 CMake（`CMakeLists.txt`）

新增构建开关：

- `BUILD_SDK_OPEN`（默认 ON）
- `BUILD_SDK_PRIVATE`（默认 OFF）
- `BUILD_SDK_WEB`（默认 ON）

目的：

- 默认构建开源核心能力。
- 通过显式开关控制私有 provider 是否参与链接。

### 7.2 `src/CMakeLists.txt`

新增子目录：

- `add_subdirectory(sdk_open)`
- 当 `BUILD_SDK_PRIVATE=ON` 时再 `add_subdirectory(sdk_private)`

### 7.3 `src/app/process/CMakeLists.txt`

新增：

- `add_subdirectory(sdk)`

### 7.4 `src/sdk_open/runtime/legacy/CMakeLists.txt`

定义可执行目标与链接规则：

- 目标：`sdk_app`
- 始终链接：
  - `sdk_open_core`
  - 公共依赖（`spdlog`、JSON 头、`pthread` 等）
- 条件链接：
  - 若 `BUILD_SDK_PRIVATE=ON`：链接 `sdk_private_providers`
  - 否则：链接 `sdk_mock_providers`
- 若 `BUILD_SDK_WEB=ON`：依赖前端资源目标（见 7.5），并将 `web/admin`、`web/demo` 拷贝到运行目录

### 7.5 Target 清单与依赖矩阵（执行版）

建议新增目标：

- `sdk_open_runtime`：`src/sdk_open/runtime/*.cpp`
- `sdk_open_interfaces`：`src/sdk_open/interfaces/*.h`（`INTERFACE` 目标）
- `sdk_mock_providers`：`src/sdk_open/providers/mock/*.cpp`
- `sdk_private_providers`：`src/sdk_open/providers/private/*.cpp`
- `sdk_web_admin_build`：构建 `frontend/admin-site`（`pnpm install && pnpm build`）
- `sdk_web_demo_build`：构建 `frontend/demo-site`（`pnpm install && pnpm build`）
- `sdk_web_assets`：汇总并拷贝前端产物到 `src/sdk_open/web/{admin,demo}` 或构建输出目录
- `sdk_app`：`src/sdk_open/runtime/legacy/sdk_main.cpp`

建议依赖关系：

- `sdk_open_runtime` -> `sdk_open_interfaces`
- `sdk_mock_providers` -> `sdk_open_interfaces`
- `sdk_private_providers` -> `sdk_open_interfaces` + 私有能力库（`devices_lib`、`ocr_lib`、`ofd_lib`、`graphic_service_lib`）
- `sdk_web_assets` -> `sdk_web_admin_build` + `sdk_web_demo_build`
- `sdk_app` -> `sdk_open_runtime` + (`sdk_mock_providers` 或 `sdk_private_providers`)
- `sdk_app` -> `sdk_web_assets`（仅 `BUILD_SDK_WEB=ON`）

建议构建模式：

- 开放模式：`BUILD_SDK_OPEN=ON` + `BUILD_SDK_PRIVATE=OFF`
  - 产物：`sdk_app` + Mock provider + 两个前端站点（可直接运行演示）
- 内部完整模式：`BUILD_SDK_OPEN=ON` + `BUILD_SDK_PRIVATE=ON`
  - 产物：`sdk_app` + 私有 provider（接入真实能力）

### 7.6 前端构建命令规范（建议）

- 管理站：在 `src/sdk_open/frontend/admin-site/` 执行 `pnpm install && pnpm build`
- 演示站：在 `src/sdk_open/frontend/demo-site/` 执行 `pnpm install && pnpm build`
- CI 推荐缓存：`~/.pnpm-store`
- 若构建机未安装 Node/pnpm，可采用预构建产物模式（`BUILD_SDK_WEB=OFF` + 预置 `web/admin`、`web/demo`）

---

## 8. 实施步骤（里程碑）

### M1：SDK 进程骨架与传输层脚手架

交付：

- `sdk_app` 进程主循环
- 五个监听端口（3 HTTP + 2 WS）空实现
- 配置读取与优雅退出基础
- 两个 Vue 前端项目骨架（admin-site / demo-site）及静态资源挂载占位页

验收：

- 进程可启动，localhost 五个端口可连通。
- 两个 HTTP 端口均可返回对应前端首页（哪怕是占位页面）。

### M2：设备与视频链路接入

交付：

- `DeviceFacade` + `devices_lib` 适配器
- 通过 WS 指令启动/停止采流
- WS 视频通道连续推送 JPEG 帧

验收：

- 客户端可稳定收帧，并可正确停止。

### M3：OCR / OFD / Graphic 能力接入

交付：

- `OcrFacade`：提交任务/查询进度/取消
- `OfdFacade`：基础打开/取页/渲染
- `GraphicFacade`：选定图像处理能力透出

验收：

- 关键方法链路可完成“请求 -> 执行 -> 响应”。

### M4：运行时加固

交付：

- 统一错误码体系
- token 鉴权与请求 schema 校验
- 限流、超时、任务队列保护
- 监控指标与日志增强

验收：

- 在并发指令 + 视频推流压测下保持稳定。

### M5：开源边界就绪

交付：

- Provider 接口稳定
- Mock provider 默认可运行
- 条件构建与符号边界检查

验收：

- 开源模式无需私有库即可编译运行。

---

## 9. Capture Pipeline 当前实现约定

### 9.1 多输出页面处理

页面处理结果不再假设只有单图或固定左右页。`SdkPageProcessResult` 保留 `output_path` 兼容旧单输出调用，同时新增 `outputs[]`：

- `output_id`：稳定资源后缀，例如 `left`、`right`、`page-001`。
- `role`：页面角色，例如 `page`、`left`、`right`、`region`。
- `index`：同一任务内的顺序。
- `path/content_type/width/height/size`：资产输出元数据。

`CapturePipelineService` 在页面处理产生 `outputs[]` 后，按输出顺序串行处理每一张图，避免同一任务内多个图片处理链路并发抢占图像 codec 或覆盖中间状态。

当前工作流为：

1. 拍照。
2. 生成原始图缩略图。
3. 纸张处理。
4. 对每个页面输出依次执行：页面处理缩略图 -> 色彩模式处理 -> 色彩结果缩略图 -> 格式转换 -> 最终输出缩略图。
5. 当前页面输出全流程完成后，再处理下一个页面输出。

多输出时 asset id 使用 `{stage}-{output_id}` 后缀；单输出场景继续使用既有 asset id（如 `asset-page-processed`、`asset-final`），避免破坏现有 demo 和第三方集成。最终输出缩略图使用 `asset-thumbnail-final` 或 `asset-thumbnail-final-{output_id}`，对应 kind 为 `final_thumbnail` 或 `final_thumbnail_{output_id}`。

### 9.2 曲面书籍处理

真实实现位于 `src/sdk_open/providers/private/graphic/curved_book_processor.*`，由 `PrivateGraphicProvider::ProcessPage` 在 `page_processing=curved_book` 时调用。

处理流程：

1. 初始化 `czcv`，默认从 SDK 运行目录加载 `czcv_model`，可通过 `SDK_GRAPHIC_MODEL_DIR` 覆盖。
2. 设备打开成功后，如果设备配置包含 `Laser=true`，SDK private device provider 会调用 `DeviceCommand::readLaserConfig`，将标定文件刷新到 `<sdk_runtime>/devices/<safe_device_id>/laser/config.data`。读取失败只降级，不阻断 `device.open`。
3. 如果拍照结果包含辅助激光图，曲面处理阶段按图像尺寸从该 SDK 设备私有路径加载激光标定配置。`SDK_GRAPHIC_LASER_CONFIG_PATH` 仅作为调试覆盖项。
4. 优先调用 `czcv::dewarp_by_laser`。
5. 激光图、标定或激光展平失败时，回退 `czcv::dewarp_by_edge_a3`。
6. 按算法输出中线拆分为 left/right 两个输出；后续 pipeline 按多输出结果继续生成色彩结果、最终格式和缩略图。

说明：当前 private provider 直接调用 `czcv` 曲面 API，而不是直接链接完整 `graphic_service_lib`。原因是 `graphic_service_impl.cpp` 会带入 OCR/PDF/MuPDF 依赖，当前 MuPDF 静态库不是 PIC，不能链接进 shared provider。SDK 侧实现保持与 `graphic_service_impl.cpp` 中曲面处理主流程一致，后续若拆出独立 graphic core 静态库，可再切换为共享实现。

### 9.3 色彩模式处理

真实实现位于 `src/sdk_open/providers/private/graphic/color_mode_processor.*`，由 `PrivateGraphicProvider::ApplyColorMode` 调用。

- CZCV 初始化由 `GraphicAlgorithmRuntime` 统一负责，色彩模式和曲面书籍处理共享同一套初始化、模型路径、日志路径和 License 还原逻辑。
- `auto_optimize` 映射为 `FILTER_AUTO`，`color` 映射为 `FILTER_COLOR_DEFAULT`，`black_white` 映射为 `FILTER_BW_DEFAULT`，`white_paper_seal` 映射为 `FILTER_SEAL_RED`。
- 兼容 `NoOpti/FILTER_NOTHING/none/original` 作为透传模式；未知值按客户端既有逻辑回退到 `FILTER_COLOR_DEFAULT` 并记录告警。

### 9.4 运行资源

`sdk_private_runtime_deps` 会复制曲面处理需要的运行库和模型到 SDK 运行目录：

- `libczcv_scan`
- `libopencv_world`
- `libzbar`
- `libZXingCore`
- `libjpeg`
- `libpng16/libpng12`
- `libtiff`
- `libfreeimage`
- `czcv_model/`

没有复制 OpenSSL 1.1 的短名 `libssl.so/libcrypto.so`，避免和 SDK 授权链路使用的 OpenSSL3 运行库混用。

### 9.5 MuPDF shared provider 依赖策略

后续 PDF/OFD 或 ImgEngine 深度复用如果需要在 `sdk_private_providers.so` 内链接 MuPDF，必须使用 PIC 版本的 MuPDF 静态库。现有主应用使用的 `third_party_repo/mupdf/lib/<arch>/libmupdf*.a` 保持不动。

Linux PIC 产物通过项目现有 Docker 环境生成：

```bash
./third_party_repo/src/build_mupdf_pic_docker.sh --arch x86-64
```

产物放在 `third_party_repo/mupdf/lib/<arch>/pic/`，供 SDK private shared provider 专用。不要把非 PIC 的 `libmupdf.a` / `libmupdf-third.a` 直接链接进 shared provider。

---

## 10. 风险清单与缓解措施

1. 视频与指令互相阻塞
- 措施：视频/指令分端口、分 worker 池。

2. 私有依赖泄漏到开放层
- 措施：严格 provider 接口边界 + CI 边界检查。

3. 设备并发冲突
- 措施：单设备会话锁与所有权策略。

4. 开放层与私有层 ABI 漂移
- 措施：接口版本化与兼容性回归测试。

---

## 11. 内部使用说明

- 本文档为内部资料，不应同步到外部公开仓库。
- 项目进入实施后，应按里程碑持续更新实际决策、实现差异与提交记录。
