# SDK Demo 演示站功能设计文档

> 状态：设计稿 v1
>
> 适用范围：`sdk_app` Demo 站 `http://127.0.0.1:17081/`；独立开发服务可通过 Asset API `http://127.0.0.1:17082/api/assets/...` 访问采集资源。
>
> 关联文档：`SDK_INTERNAL_ARCH_PLAN.md`、`Plugin_Capability.md`、`SDK_Auth_Error_Code.md`
>
> 最后更新：2026-04-02

## 1. 文档目标

本文档用于定义 `sdk_app` Demo 站的功能设计，目标是让后续网页 UI 设计和前端实现能够围绕统一能力工作流展开。

Demo 站的核心作用不是运维，而是能力验证。它需要把 SDK 协议中的主要能力域以可操作、可观测、可回放的方式组织成一个浏览器端演示站，用于：

- 验证 `sdk_app` 的 HTTP / WebSocket 接口是否可用
- 验证鉴权、设备、视频、拍照、OCR、图像处理、文件转换等能力链路
- 演示对外协议在 Web 页面中的典型使用方式
- 为后续 SDK 接入说明、样例站、UI 方案设计提供统一参照

## 2. 站点定位

### 2.1 目标用户

- SDK 内部开发人员
- QA 能力验证人员
- 实施/售前演示人员
- 后续第三方集成方参考用户

### 2.2 站点目标

- 提供一个“协议调用工作台”
- 让用户在不写代码的情况下验证关键能力
- 同时展示“友好视图”和“原始 JSON 视图”
- 覆盖主要能力域，并清晰标注 `ga / beta / planned`

### 2.3 非目标

- 不作为正式生产业务系统
- 不承担复杂项目配置管理
- 不替代管理站的系统监控职责
- 不假设所有协议能力在 v1 都已经完成真实 Provider 接入

## 3. 设计原则

### 3.1 全量能力域可见

Demo 站需要覆盖主要协议能力域，即使某些能力尚未在当前 runtime 中完全实现，也要在页面上占位并标注状态：

- `ga`：可优先演示
- `beta`：可试用或能力规则仍可能微调
- `planned`：显示协议语义，但按钮置灰或仅展示请求示例

### 3.2 统一工作流

所有能力页都应围绕统一步骤组织：

1. 建立连接
2. 完成鉴权
3. 配置输入参数
4. 发送请求或订阅事件
5. 查看结果
6. 查看原始 JSON / 错误码 / trace_id

### 3.3 双视图并存

每个能力页都建议同时提供：

- 业务友好视图
- 原始协议视图

便于不同角色使用：

- 产品/实施看业务结果
- 开发/测试看 JSON 协议细节

### 3.4 当前实现边界清晰

页面必须明确区分：

- 当前已有基础能力：`system.*`、部分 `auth.*`
- 当前仅有协议设计和 UI 预留的能力
- 需要真实设备或真实私有 Provider 才能演示的能力

## 4. 信息架构

推荐一级导航如下：

- 快速开始
- 连接与鉴权
- 设备与视频
- 拍照与采集
- 图像处理
- OCR 与识别
- 文件转换
- SANE 扫描
- 协议调试台
- 事件与结果中心

推荐全局固定区域如下：

- 顶部环境栏
  - 当前连接状态
  - 当前 ApiKey / session 状态摘要
  - 当前目标设备
  - Provider mode
- 左侧能力导航
- 右侧全局结果抽屉入口
  - 最近请求
  - 最近错误
  - 最近事件

## 5. 全局工作流设计

## 5.1 典型使用路径

推荐将 Demo 站首页引导为以下顺序：

1. 查看四个服务端点
2. 执行 `system.ping`
3. 查看 `system.info`
4. 加载 `system.capabilities`
5. 输入 `api_key` 并执行 `auth.validate`
6. 选择目标设备
7. 执行对应能力演示

### 5.2 全局状态定义

- 未连接
- 已连接未鉴权
- 已鉴权但无设备权限
- 已鉴权且可操作
- 执行中
- 执行成功
- 执行失败
- 功能未实现
- 协议预留

### 5.3 全局上下文

Demo 站需要维护统一会话上下文，至少包括：

- 指令 WS 连接状态
- 视频 WS 连接状态
- `api_key`
- `session_token`
- 当前 `auth_context`
- 当前选中设备
- 最近一次 `trace_id`

## 6. 页面详细功能设计

## 6.1 快速开始页

### 页面目标

让首次进入站点的用户在最短路径内完成服务连通性验证和能力目录浏览。

### 页面模块

#### 1. 服务端点卡片

展示：

- Admin HTTP
- Demo HTTP
- Command WS
- Video WS

每张卡片提供：

- 地址显示
- 复制按钮
- 连通性检查按钮

#### 2. 三步快速验证区

固定展示三个操作：

- `system.ping`
- `system.info`
- `system.capabilities`

展示方式建议为“步骤卡片 + 原始结果面板”。

#### 3. 能力地图区

按 domain 展示当前能力域和状态：

- `system`
- `auth`
- `device`
- `stream / event`
- `capture`
- `image`
- `ocr / recognition`
- `file`
- `sane`

### 交互动作

- 一键运行基础检查
- 加载能力清单
- 跳转到对应能力页

## 6.2 连接与鉴权页

### 页面目标

完成 Demo 站的连接初始化和授权上下文建立，为后续所有能力演示提供前置条件。

### 页面模块

#### 1. 指令通道连接区

展示：

- `ws://127.0.0.1:17090`
- 连接状态
- token 输入框
- 连接 / 断开按钮

#### 2. 视频通道连接区

展示：

- `ws://127.0.0.1:17091`
- 连接状态
- token 输入框
- 连接 / 断开按钮

#### 3. 鉴权表单区

支持：

- 输入 `api_key`
- 输入 `session_token`
- 执行 `auth.validate`
- 执行 `auth.refresh`
- 执行 `auth.get_context`

#### 4. 授权结果区

展示：

- `is_valid`
- `account_type`
- `account_type_code`
- `auth_scene`
- `license_mode`
- `device_scope`
- `expires_at`
- `capabilities`

#### 5. 授权失败说明区

展示常见错误码及建议：

- `1100` 未提供认证
- `1101` ApiKey 非法
- `1102` ApiKey 过期
- `1103` session 失效
- `1104` 账号等级不足
- `1105` 设备不在授权范围
- `1107` 当前能力未授权

### 交互动作

- 一键填充示例请求
- 保存当前授权上下文到 Demo 会话
- 将 `session_token` 应用到后续请求

## 6.3 设备与视频页

### 页面目标

验证设备发现、设备打开关闭、视频订阅和视频事件链路。

### 页面模块

#### 1. 设备列表区

支持调用：

- `device.list`
- `device.get`
- `device.get_models`

展示字段建议：

- `device_id`
- `model`
- `display_name`
- `vid`
- `pid`
- `status`
- `authorized`
- `cameras`

#### 2. 设备操作区

支持：

- `device.open`
- `device.close`
- `device.set_preview_resolution`

需要表单项：

- `device_id`
- 预览分辨率

#### 3. 视频订阅区

支持：

- `stream.subscribe`
- `stream.unsubscribe`
- `stream.set_options`

表单项建议：

- `device_id`
- `stream_id`
- 分辨率
- 帧率
- 质量

#### 4. 视频预览区

展示：

- 实时画面占位
- 最近帧时间
- `stream.frame_meta`
- 帧序号
- 分辨率

#### 5. 实时事件区

展示：

- `device.online`
- `device.offline`
- `stream.frame_meta`

### 状态设计

- 未选择设备时禁用流操作
- 设备未打开时禁止订阅
- `planned` 或未实现 method 显示为置灰态

## 6.4 拍照与采集页

### 页面目标

验证设备拍照和采集参数控制能力，形成从设备到文件/图像结果的最小闭环。

### 页面模块

#### 1. 拍照主表单

主操作：

- `capture.take`
- `capture.take_base64`

关键输入项：

- `device_id`
- 输出模式：文件 / Base64
- 输出路径
- `format`
- `dpi`
- `color_mode`
- `auto_crop`
- `auto_deskew`
- `curve_flatten`
- `detect_barcode`
- `detect_blank_page`
- `scan_wide_mode`
- `page_fill`
- `edge_repair`

#### 2. 采集策略设置区

支持：

- `capture.set_turn_detect`
- `capture.set_scan_area`
- `capture.set_page_fill`
- `capture.set_edge_repair`

#### 3. 拍照结果区

展示：

- 输出文件路径
- 图片缩略图
- 宽高
- 检测到的矩形框
- 条码识别结果
- 原始返回 JSON

#### 4. 实时采集事件区

展示：

- `capture.detected_rectangle`
- `capture.turn_detected`

### 交互动作

- 使用默认推荐参数
- 从最近一次参数重新执行
- 将拍照结果发送到图像处理页或 OCR 页

## 6.5 图像处理页

### 页面目标

验证图像后处理能力，重点体现“统一处理入口 + 可组合操作”的设计。

### 页面模块

#### 1. 输入资源区

支持：

- 本地文件路径输入
- 最近拍照结果导入
- 最近处理结果再次加工

#### 2. 统一处理工作台

主入口：

- `image.process`

页面建议支持操作链编辑器，允许按顺序添加处理步骤。

推荐预置操作模板：

- `normalize`
- `replace_background`
- `adjust`
- `add_watermark`
- `merge`
- `clip`
- `replace_text_color`
- `erase_handwriting`

#### 3. 参数区

根据操作类型动态展示参数，例如：

- 纸张尺寸
- 背景颜色
- 对比度 / 锐度
- 水印文字 / 图片 / 透明度 / 位置
- 拼接方向
- 切分规则

#### 4. 结果对比区

展示：

- 原图
- 处理后图
- 文件路径
- 操作链摘要
- 原始 JSON

### 状态设计

- `entitled` 能力需要额外显示授权门槛
- 对于 `planned` 或未接入的处理项，允许展示参数说明，但不允许提交

## 6.6 OCR 与识别页

### 页面目标

验证 OCR、条码识别和视频实时识别能力。

### 页面模块

#### 1. OCR 区

支持：

- `ocr.recognize`
- `ocr.recognize_batch`

输入项：

- 文件路径
- 语言
- 批量文件列表

结果区展示：

- `text`
- `language`
- `confidence`
- `page_count`
- `blocks`

#### 2. 条码识别区

支持：

- `recognition.barcode`

展示：

- 条码类型
- 文本内容
- 定位点

#### 3. 实时条码设置区

支持：

- `recognition.set_realtime_barcode`

并在事件区展示：

- `recognition.barcode_detected`

### 交互动作

- 从拍照结果直接导入识别
- 从图像处理结果导入识别
- 复制纯文本结果
- 导出识别结果 JSON

## 6.7 文件转换页

### 页面目标

验证文件转换和文件处理类能力，覆盖图像到 PDF / OFD / TIFF 等典型流程。

### 页面模块

#### 1. 转换表单区

主入口：

- `file.convert`

典型场景模板：

- `image -> pdf`
- `images -> pdf`
- `image -> ofd`
- `image -> tiff`
- `base64 -> pdf`

关键输入项：

- source type
- source files
- target type
- target path
- 转换选项

#### 2. 文件信息区

支持：

- `file.info`
- `file.rename`
- `file.delete`
- `file.mkdir`

#### 3. 结果区

展示：

- 输出路径
- 输出类型
- 文件大小
- 页数
- 原始响应 JSON

### 状态设计

- `file.upload`、`file.print`、`file.set_metadata` 当前按协议预留或 planned 处理
- UI 上显示能力说明和预期参数，但动作按钮禁用

## 6.8 SANE 扫描页

### 页面目标

验证第三方扫描仪接入能力，与 CZUR 自有设备能力分开建模。

### 页面模块

#### 1. SANE 设备列表区

支持：

- `sane.list`

展示：

- 设备名
- 厂商
- 型号
- 在线状态

#### 2. 参数设置区

支持：

- `sane.get_options`
- `sane.set_option`

#### 3. 扫描执行区

支持：

- `sane.scan`
- `sane.cancel`

#### 4. 事件区

展示：

- `sane.scan_status`
- `sane.scan_file`

### 状态设计

- 若宿主机未启用 SANE，显示 `1700 SANE_NOT_AVAILABLE`
- 与 CZUR 设备相关的白名单规则不在本页使用

## 6.9 协议调试台

### 页面目标

为开发和测试提供原始协议级调试能力，不受具体业务页面限制。

### 页面模块

#### 1. 请求编辑器

支持编辑完整 JSON：

- `request_id`
- `method`
- `params`
- `auth`
- `client`

#### 2. 示例模板区

提供常用模板：

- `system.ping`
- `system.info`
- `auth.validate`
- `device.list`
- `capture.take`
- `ocr.recognize`
- `file.convert`

#### 3. 响应查看区

展示：

- 格式化 JSON
- `code`
- `message`
- `trace_id`
- `ts`

#### 4. 调试历史区

记录最近若干次请求：

- method
- 结果码
- 时间
- 是否成功

### 交互动作

- 一键发送
- 格式化 JSON
- 复制请求
- 复制响应
- 将响应保存到结果中心

## 6.10 事件与结果中心

### 页面目标

集中查看整个 Demo 站生命周期内产生的请求结果、事件流和错误信息。

### 页面模块

#### 1. 最近请求列表

展示：

- method
- request_id
- `code`
- `trace_id`
- 执行时间

#### 2. 事件流面板

展示：

- `device.online`
- `device.offline`
- `stream.frame_meta`
- `capture.detected_rectangle`
- `capture.turn_detected`
- `recognition.barcode_detected`
- `sane.scan_status`
- `sane.scan_file`

#### 3. 最近错误面板

展示：

- 错误码
- 错误名
- message
- 关联 method
- 时间

### 交互动作

- 按类型过滤
- 清空本次会话缓存
- 导出结果摘要

## 7. 能力状态映射规则

Demo 站需要对协议中的能力状态有统一视觉映射：

- `ga`
  - 可直接操作
  - 展示推荐示例
  - 进入默认导航主路径
- `beta`
  - 可操作，但需显示“实验性 / 细节可能调整”
  - 参数面板允许更明显地展示说明
- `planned`
  - 仅展示协议占位、参数结构、预期返回
  - 操作按钮禁用
  - 页面说明中明确“当前版本未实现”

## 8. 错误展示规则

### 8.1 统一展示字段

所有失败结果都必须展示：

- `code`
- `message`
- `error.name`
- `trace_id`
- `request_id`

### 8.2 常见错误分组

- 通用请求错误：`1000-1099`
- 鉴权错误：`1100-1199`
- 设备错误：`1200-1299`
- 采集与视频错误：`1300-1399`
- 图像处理错误：`1400-1499`
- OCR 与识别错误：`1500-1599`
- 文件与 OFD 错误：`1600-1699`
- SANE 错误：`1700-1799`
- 内部系统错误：`1900-1999`

### 8.3 错误交互要求

- 错误提示不能只显示 `message`
- 必须保留查看原始响应的入口
- 对典型错误给出简短建议，如“检查设备是否在授权白名单内”

## 9. UI 设计要求

### 9.1 页面布局原则

- 每页至少包含：
  - 参数输入区
  - 操作按钮区
  - 结果展示区
  - 原始 JSON 区
- 涉及视频或图像的页面采用“左配置、右预览”结构更合适
- 涉及事件流的页面应保留底部或右侧事件面板

### 9.2 组件建议

- 步骤引导卡片
- 能力状态标签
- JSON 编辑器 / 查看器
- 图片预览器
- 视频画布区
- 表单模板切换器
- 请求历史列表
- 事件时间线

### 9.3 视觉标识建议

- 已连接 / 已鉴权 / 已选设备三类状态应始终可见
- `planned` 不能设计成与故障态相同
- 对“需要真实设备”“需要私有 Provider”“需要更高账号等级”要使用不同说明标签

## 10. 页面与协议映射

推荐的主映射关系如下：

- 快速开始页
  - `system.ping`
  - `system.info`
  - `system.capabilities`
- 连接与鉴权页
  - `auth.validate`
  - `auth.refresh`
  - `auth.get_context`
- 设备与视频页
  - `device.*`
  - `stream.*`
  - `device.online/offline`
  - `stream.frame_meta`
- 拍照与采集页
  - `capture.*`
  - `capture.detected_rectangle`
  - `capture.turn_detected`
- 图像处理页
  - `image.*`
- OCR 与识别页
  - `ocr.*`
  - `recognition.*`
- 文件转换页
  - `file.*`
- SANE 扫描页
  - `sane.*`

## 11. 验收标准

Demo 站功能设计完成后，应满足以下要求：

- UI 设计师可据此直接出 Demo 站页面结构和交互稿
- 研发可据此明确每个能力页对应的协议 method / event
- 测试可据此整理手工验证路径
- 文档能明确区分：
  - 当前已实现的基础能力
  - 需真实设备才可演示的能力
  - 仅协议预留的能力
- 所有主要能力域在页面结构中都有落位，不遗漏 `file`、`sane`、`event` 等域

## 12. 推荐的首批实现优先级

尽管 UI 设计需要覆盖全量能力域，但前端实现阶段建议优先落地以下页面闭环：

1. 快速开始
2. 连接与鉴权
3. 设备与视频
4. 拍照与采集
5. OCR 与识别
6. 协议调试台

图像处理、文件转换、SANE 扫描可在 UI 结构先落位，再按 Provider 接入进度逐步增强。
