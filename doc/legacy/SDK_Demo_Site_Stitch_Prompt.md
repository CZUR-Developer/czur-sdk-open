# SDK Demo 演示站 Stitch Prompt

本文档提供一个可直接粘贴到 Stitch 的 prompt，用于生成 `sdk_app` Demo 站的多页面 UI 设计稿。

## 推荐用法

- 将下方 `Stitch Prompt` 整段复制到 Stitch。
- 目标输出建议选择桌面端 Web 多页面设计。
- 若需要二次细化，可在 Stitch 中继续追加“强化某一页面”或“改为低保真/高保真”的补充指令。

## Stitch Prompt

```text
请为一个名为“SDK Demo 演示站”的桌面端 Web 应用设计一整套多页面 UI。这个站点不是营销官网，也不是后台运维系统，而是一个用于 SDK 能力验证、联调演示、QA 测试和第三方集成参考的“协议调用工作台”。

设计目标：
1. 让用户在不写代码的情况下验证 SDK 的 HTTP / WebSocket 接口。
2. 用统一工作流串联连接、鉴权、设备、视频、拍照、OCR、图像处理、文件转换、SANE 扫描、协议调试和结果追踪。
3. 每个能力页同时提供“友好业务视图”和“原始 JSON / 协议视图”。
4. 明确区分 ga、beta、planned 三类能力状态，尤其 planned 是协议预留态，不是故障态。

目标用户：
- SDK 内部开发人员
- QA 能力验证人员
- 售前 / 实施演示人员
- 后续第三方集成参考用户

整体产品气质：
- 专业控制台风格，偏工程化、协议工作台、联调工具感
- 信息密度中高，但层次清晰，可快速扫描状态和结果
- 现代、克制、可信，不做营销化大 Banner
- 桌面端优先，适配 1440px 宽度的主工作区
- 视觉语言避免默认紫色路线，建议使用冷静的蓝青色或深海蓝作为强调色

视觉系统建议：
- 主背景为浅色或浅灰白控制台风格
- 主强调色用于连接成功、可操作动作、高亮中的导航
- 成功、警告、错误、planned、beta 都要有明确但不过度刺眼的状态色
- ga：明确可操作、正向高亮
- beta：实验性提示，带轻度说明感
- planned：灰阶占位 + 协议预留标签 + 禁用按钮，不能和错误态混淆
- 使用清晰的人文无衬线字体做界面主文本，JSON / trace_id / method 名称使用等宽字体
- 可以加入轻量的网格、分区阴影、卡片层级和控制台式状态灯，但不要做赛博风

全局信息架构：
- 顶部固定环境栏
  - 当前连接状态
  - 当前鉴权状态
  - 当前选中设备
  - Provider mode
  - 最近一次 trace_id
  - 一组始终可见的状态徽标：已连接 / 已鉴权 / 已选设备
- 左侧固定能力导航
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
- 右侧或右下角提供全局结果抽屉入口
  - 最近请求
  - 最近错误
  - 最近事件

全局交互原则：
- 所有能力页围绕统一工作流组织：
  1. 建立连接
  2. 完成鉴权
  3. 配置输入参数
  4. 发送请求或订阅事件
  5. 查看友好结果
  6. 查看原始 JSON / 错误码 / trace_id
- 每页至少包含：
  - 参数输入区
  - 操作按钮区
  - 结果展示区
  - 原始 JSON 区
- 涉及视频或图像的页面采用“左配置、右预览”结构
- 涉及事件流的页面保留底部或右侧事件面板
- 错误提示不能只显示 message，必须能看到 code、error.name、trace_id、request_id

请输出以下页面，并保持统一设计系统与组件语言：

1. 快速开始页
- 页面目标：让首次进入的用户在最短路径内完成服务连通性验证和能力目录浏览
- 模块：
  - 四个服务端点卡片：Admin HTTP、Demo HTTP、Command WS、Video WS
  - 每张卡片显示地址、复制按钮、连通性检查按钮、状态标记
  - 三步快速验证区：system.ping、system.info、system.capabilities
  - 能力地图区：system、auth、device、stream / event、capture、image、ocr / recognition、file、sane
- 适合做成欢迎式工作台首页，但仍然是控制台而不是官网

2. 连接与鉴权页
- 页面目标：建立 Demo 站的连接初始化和授权上下文
- 模块：
  - 指令通道连接区：ws://127.0.0.1:17090、token 输入框、连接 / 断开、状态
  - 视频通道连接区：ws://127.0.0.1:17091、token 输入框、连接 / 断开、状态
  - 鉴权表单区：api_key、session_token、auth.validate、auth.refresh、auth.get_context
  - 授权结果区：is_valid、account_type、account_type_code、auth_scene、license_mode、device_scope、expires_at、capabilities
  - 授权失败说明区：展示典型错误码和建议
- 页面感觉应像“连接控制面板 + 会话状态面板”

3. 设备与视频页
- 页面目标：验证设备发现、设备打开关闭、视频订阅和视频事件链路
- 页面采用左配置右视频预览结构
- 模块：
  - 设备列表区：device.list、device.get、device.get_models，表格展示 device_id、model、display_name、vid、pid、status、authorized、cameras
  - 设备操作区：device.open、device.close、device.set_preview_resolution
  - 视频订阅区：stream.subscribe、stream.unsubscribe、stream.set_options
  - 视频预览区：实时画面占位、最近帧时间、frame meta、帧序号、分辨率
  - 实时事件区：device.online、device.offline、stream.frame_meta
- 状态约束要在 UI 中明确体现：未选设备时禁用流操作，设备未打开时禁止订阅

4. 拍照与采集页
- 页面目标：形成从设备拍照到文件 / 图像结果的最小闭环
- 页面采用左侧参数控制、右侧结果预览、底部事件流
- 模块：
  - 拍照主表单：capture.take、capture.take_base64
  - 参数包括：device_id、输出模式、输出路径、format、dpi、color_mode、auto_crop、auto_deskew、curve_flatten、detect_barcode、detect_blank_page、scan_wide_mode、page_fill、edge_repair
  - 采集策略设置区：capture.set_turn_detect、capture.set_scan_area、capture.set_page_fill、capture.set_edge_repair
  - 拍照结果区：输出文件路径、图片缩略图、宽高、检测矩形框、条码结果、原始返回 JSON
  - 实时采集事件区：capture.detected_rectangle、capture.turn_detected
- 要体现“推荐参数”“再次执行”“发送到图像处理页 / OCR 页”的联动入口

5. 图像处理页
- 页面目标：体现统一处理入口 + 可组合操作链的设计
- 模块：
  - 输入资源区：本地文件路径输入、最近拍照结果导入、最近处理结果再次加工
  - 统一处理工作台：image.process
  - 操作链编辑器：normalize、replace_background、adjust、add_watermark、merge、clip、replace_text_color、erase_handwriting
  - 动态参数区：根据不同操作显示纸张尺寸、背景颜色、对比度、锐度、水印、拼接方向、切分规则等
  - 结果对比区：原图、处理后图、文件路径、操作链摘要、原始 JSON
- 已授权但需要额外 entitlement 的能力要有额外门槛提示
- planned 操作项允许展示参数说明，但禁止提交

6. OCR 与识别页
- 页面目标：验证 OCR、条码识别和实时识别能力
- 模块：
  - OCR 区：ocr.recognize、ocr.recognize_batch，支持文件路径、语言、批量文件列表
  - OCR 结果：text、language、confidence、page_count、blocks
  - 条码识别区：recognition.barcode，展示条码类型、文本内容、定位点
  - 实时条码设置区：recognition.set_realtime_barcode
  - 事件区：recognition.barcode_detected
- 要预留从拍照结果或图像处理结果直接导入识别的入口

7. 文件转换页
- 页面目标：验证 image / base64 到 pdf / ofd / tiff 等文件转换流程
- 模块：
  - 转换表单区：file.convert
  - 模板场景：image -> pdf、images -> pdf、image -> ofd、image -> tiff、base64 -> pdf
  - 参数：source type、source files、target type、target path、转换选项
  - 文件信息区：file.info、file.rename、file.delete、file.mkdir
  - 结果区：输出路径、输出类型、文件大小、页数、原始响应 JSON
- file.upload、file.print、file.set_metadata 作为 planned 项展示说明和禁用态

8. SANE 扫描页
- 页面目标：验证第三方扫描仪接入能力，与自有设备能力分开建模
- 模块：
  - SANE 设备列表区：sane.list
  - 参数设置区：sane.get_options、sane.set_option
  - 扫描执行区：sane.scan、sane.cancel
  - 事件区：sane.scan_status、sane.scan_file
- 当宿主机未启用 SANE 时，要有明显但非灾难式的“1700 SANE_NOT_AVAILABLE”状态展示

9. 协议调试台
- 页面目标：给开发和测试提供原始协议级调试能力
- 模块：
  - 请求编辑器：完整 JSON，含 request_id、method、params、auth、client
  - 示例模板区：system.ping、system.info、auth.validate、device.list、capture.take、ocr.recognize、file.convert
  - 响应查看区：格式化 JSON、code、message、trace_id、ts
  - 调试历史区：method、结果码、时间、是否成功
- 页面应更偏开发者工具感，可以有强一点的编辑器和日志面板视觉

10. 事件与结果中心
- 页面目标：集中查看整个 Demo 站生命周期内产生的请求、事件和错误
- 模块：
  - 最近请求列表：method、request_id、code、trace_id、执行时间
  - 事件流面板：device.online、device.offline、stream.frame_meta、capture.detected_rectangle、capture.turn_detected、recognition.barcode_detected、sane.scan_status、sane.scan_file
  - 最近错误面板：错误码、错误名、message、关联 method、时间
- 支持按类型过滤、清空本次会话缓存、导出结果摘要

关键组件请统一设计：
- 步骤引导卡片
- 能力状态标签
- JSON 编辑器 / JSON 查看器
- 图片预览器
- 视频画布区
- 表单模板切换器
- 请求历史列表
- 事件时间线
- 错误详情卡片
- 全局结果抽屉

请重点表现以下状态体系：
- 未连接
- 已连接未鉴权
- 已鉴权但无设备权限
- 已鉴权且可操作
- 执行中
- 执行成功
- 执行失败
- 功能未实现
- 协议预留

请额外使用不同标签区分：
- 需要真实设备
- 需要私有 Provider
- 需要更高账号等级

输出要求：
- 生成一组统一风格的桌面端高保真页面
- 至少覆盖首页、连接与鉴权、设备与视频、拍照与采集、图像处理、OCR 与识别、协议调试台、事件与结果中心
- 界面需要体现真实可用的工作台感，不要过度概念稿
- 同时兼顾产品演示效果和开发测试可读性
- 每个页面保留友好结果区与原始协议区
- 布局要考虑实际工程实现，避免过度花哨或无法落地的装饰
```

## 备注

- 该 prompt 以 [`SDK_Demo_Site_Function_Design.md`](/home/pangkk/CZURWork/PC_Client/czur-capeditor/src/sdk_open/runtime/legacy/SDK_Demo_Site_Function_Design.md) 为依据整理。
- 当前默认方向是“桌面端、多页面、专业控制台、高保真”。
- 如果后续需要，我可以继续补一版：
  - 更偏 CZUR 品牌风格的 prompt
  - 更偏低保真线框图的 prompt
  - 只针对首页和关键链路的精简版 prompt
