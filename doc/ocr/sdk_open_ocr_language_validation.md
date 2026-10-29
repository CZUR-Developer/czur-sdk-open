# Open SDK OCR 语言参数实现与样张验证

日期：2026-10-21。验证平台：macOS arm64。样张目录：`/Users/pangkk/Documents/Images/采编王样本数据/OCR识别/`，共六张 JPG。样张内容仅作为识别数据。

## 接口与实现

`ocr.extract_text`、`ocr.recognize` 均新增可选的 `request.params.language` 字符串字段，缺省为 `auto`。白名单为客户端已启用的 89 个 CZOCR 代码加 `auto`，包含 `ch`、`en`、`chinese_cht`、`japan`、`korean`；客户端暂缓的 20 个代码不可用。空串、非字符串、未知或暂缓代码返回 `InvalidParams`（1002），不会进入识别。不接受 ABBYY 名称或语言数组。

```json
{
  "request_id": "req-ocr-en",
  "method": "ocr.extract_text",
  "params": {
    "input_upload_id": "<upload_id>",
    "language": "en"
  }
}
```

`ocr.recognize` 同样使用顶层 `params.language`，与 `format`、`exportType` 并列；同一次导出的全部输入使用一种语言。顶层字段优先于扩展对象中的同名字段，省略顶层字段也不会采用嵌套语言。

`auto` 保持当前兼容行为，进入引擎前显式恢复 `ch`，不承诺全语种自动检测。公开 DTO、命令解析、Windows JSON bridge、private C API、mock、Demo 与中英文协议文档均已更新。导出复用既有 `ext_params_json` 参数链，在初始化成功后、逐页容错循环前设置语言；设置失败直接返回任务错误。

联调修复了以下实际问题：

- 取字持有缓存 handler 后，导出再建 handler 会在 CZOCR 内触发 `InitGoogleLogging() twice`。显式 `CzOcr` 导出现在与取字共用管理器级 handler 和互斥锁，完整导出串行执行。结果缓存在锁内清理，进度回调使用弱引用。默认/ABBYY 任务仍使用原路径。
- Runtime 静态析构时释放缓存 handler 会晚于算法库日志状态的析构。POSIX private Runtime 现在于服务停止后显式调用 `ShutdownOcrRuntime()`，先排空 worker 并释放 handler，再返回 main。
- macOS SDK 原打包只复制 `czocr/models/mac-arm64`，其中同名算法库是旧版（SHA-256 `96eb8ffc…`）。该产物在首个请求直接选择 `en` 时发生 detector 内空地址访问；两个工作目录均可复现。现在在复制模型目录之后，用 `czocr/lib/${LIB_ARCH}/libczcv_ocr.dylib` 覆盖它，与父应用已有规则一致，并增加源文件存在性校验。新版产物 SHA 与源文件一致；两个工作目录的首英文请求及正常退出均已通过。未增加中文预热或语言回退。旧库运行保留为诊断资料，不作为最终能力验收。
- OCR C API 边界测试拆为独立 executable，仅链接 providers；认证 smoke 恢复仅链接 auth C API，消除测试同时加载两套设备 Objective-C 类的问题。

## 环境与证据

- SDK Runtime：`0.0.9 / build 66`，由当前工作区构建，真实 private provider 与 CZOCR。
- 父仓库产品实现提交：`c0fd0c6be`（含前序 `e069527b4`、`7c71ab49d`）；测试拆分提交：`2ab30bee2`。SDK Open 子模块：`611f2d7`（含语言功能提交 `ed29996`）。子模块指针已同步；均为本地提交，未推送。
- 三方仓库 pin：`65ac443`。本报告以下表格全部来自修复打包后加载新版算法库的 `upgraded/` 运行。
- 独立测试端口：WSS `28190`、HTTPS 资产服务 `28182`；使用匹配的临时证书与客户端显式 CA，未关闭 TLS 校验、未修改系统信任。
- 试用账户验证中，一次中文取字成功；后续会话创建命中 `1110 / 请求过于频繁`。限流不计为能力通过。
- 完整回归使用独立 Runtime 随机测试签名密钥签发的短期离线测试 Key。密钥仅适用于隔离测试实例，产品认证逻辑未改动，未影响已安装的 Runtime。测试结束后删除本次凭据。
- 诊断客户端来自本机 `@czur/open-cli` 的临时副本，只补出内部连接函数供测试调用；未修改全局 CLI。
- 请求、响应、任务轮询、下载资产、耗时、SHA-256、日志、版本信息与诊断脚本保存在 [本地验证目录](../../output/sdk-open-ocr-language-validation-20261021/)，以其中 `README.md` 为索引。样张及二进制输出不提交 Git；测试凭据、TLS 私钥及授权状态不归档。

最终引擎文件 SHA-256：

| 文件 | SHA-256 |
|---|---|
| `libczcv_ocr_api.dylib` | `eca9532177c22ff8609e73d387b4e131df1da3a8dba4d9ec7bda80ff659384e0` |
| `models/libczcv_ocr.dylib` | `f3f3e9c0e1a22b95bfb861572fa4f82d0b80fe1e63e37011044117a6e5432762` |

## 自动化与协议回归

- SDK Open CTest：5/5 通过，包含新增的 `sdk_open.ocr_language_contract` 以及原有 capture、status、TLS、provider bundle 测试。
- `sdk_open_upload_smoke_test`、`sdk_private_c_api_smoke_test`、独立的 `sdk_private_ocr_language_smoke_test`：均通过；最后一项覆盖两个 OCR JSON 入口在初始化识别前拒绝非法语言。三个测试均未再出现重复 Objective-C 类提示。
- Demo：`pnpm test` 3/3 通过、`pnpm build` 成功；Demo、SDK、客户端的启用语言集合完全相同，共 90 个值。页面实际选择 `en` 后，请求预览同步为 `"language": "en"`，截图见验证目录。
- 新版引擎真实 WSS：两个方法各测试空串、数字、null、数组、对象、未知代码、ABBYY 名称、两类暂缓代码，共 18/18 返回 1002。另验证省略字段，以及顶层省略、嵌套试图设置 `dar/en` 的覆盖规则。
- `sdk_open_app`、private provider 与相关测试目标构建成功。新增业务代码保持 C++11；无新增依赖。`git diff --check` 通过。
- 构建存在既有 OpenSSL 最低系统版本、OCR 数组地址判断、Demo chunk 大小提示；最终构建无错误。

## 六张样张与语言切换

每张图片分别执行显式语言和 `auto` 的轻量取字、TXT 导出，共 24 个正常请求。耗时含协议与下载开销，不能视为纯算法性能基准。

| 样张 | 显式语言 | 文字块数（显式 / auto） | 取字耗时 ms（显式 / auto） | TXT 耗时 ms（显式 / auto） | TXT 字节（显式 / auto） |
|---|---|---|---|---|---|
| 中文_样本1.jpg | `ch` | 26 / 26 | 700 / 509 | 1214 / 1213 | 1995 / 1995 |
| 中文_样本2.jpg | `ch` | 27 / 27 | 468 / 470 | 1209 / 1210 | 1948 / 1948 |
| 英文_样本1.jpg | `en` | 41 / 41 | 1605 / 849 | 2421 / 1616 | 2533 / 2622 |
| 英文_样本2.jpg | `en` | 39 / 39 | 1676 / 869 | 2416 / 1615 | 2763 / 2824 |
| 繁体_样本1.jpg | `chinese_cht` | 19 / 19 | 1316 / 500 | 2017 / 1212 | 1943 / 1941 |
| 繁体_样本2.jpg | `chinese_cht` | 15 / 15 | 1076 / 433 | 1611 / 1210 | 1453 / 1462 |

人工从样张预先固定以下代表性片段，使用 NFKC 规范化全角标点并去除空白后核对；未使用完整真值计算准确率。片段在新版运行前已固定，未按输出调整。

| 样张 | 预设片段 | 显式语言（取字 / TXT） | auto（取字 / TXT） |
|---|---|---|---|
| 中文_样本1.jpg | 中华人民共和国个人信息保护法 | 匹配 / 匹配 | 匹配 / 匹配 |
| 中文_样本2.jpg | 国家网络空间安全战略 | 匹配 / 匹配 | 匹配 / 匹配 |
| 英文_样本1.jpg | Have you written Test Plan? | 匹配 / 匹配 | 匹配 / 匹配 |
| 英文_样本2.jpg | Interview Questions for QA Tester | 匹配 / 匹配 | 匹配 / 匹配 |
| 繁体_样本1.jpg | 而出版繁體書可能是平衡 | 未匹配 / 未匹配 | 匹配 / 匹配 |
| 繁体_样本2.jpg | 他們喜歡的課題 | 未匹配 / 未匹配 | 未匹配 / 未匹配 |

繁体 1 显式语言输出为“出版繁體書可能是平衡……”（漏“而”），后文“方案”也漏“方”；繁体 2 输出含“他們喜歡的課题”，仍出现简繁混排，并有其他漏字。两张繁体样张的固定片段均未在显式语言输出中完整匹配。接口成功、文字块与文件完整性通过，不代表繁体识别质量验收通过；需引擎侧进一步复核，不宣称识别准确率百分比。

同一进程、同一缓存 handler 连续执行 `en → ch → chinese_cht → auto → en`，日志确认传入 setter 的序列为 `en → ch → chinese_cht → ch → en`，各次识别成功，`auto` 未沿用繁中。运行也交替调用两个 OCR 方法，覆盖导出后继续取字，以及取字后继续导出。

新版引擎的两张中文样张，显式 `ch` 与 `auto` TXT 逐字节相同，符合 `auto` 映射 `ch` 的约定；两张英文与两张繁体样张，显式语言与 `auto` TXT 均不同。参数接线依据包括请求捕获、实际 setter 调用日志、成功返回及同 handler 切换链路，不单凭输出差异判断。当前引擎没有公开的活动模型 getter，不对内部模型效果作更强推断。

## 导出、冷启动与退出回归

除 TXT 外，英文样张各执行一次 PDF、DOCX、XLSX、OFD、JSON 真实导出，均到达 `completed` 且资产可下载。

| 格式 | 资产字节数 | 文件可读性检查 |
|---|---|---|
| PDF | 9367864 | pdfinfo 读取成功，1 页 A4，PDF 1.7 |
| DOCX | 32137 | ZIP CRC 正常，word/document.xml 等 5 个 XML 可解析 |
| XLSX | 20389 | ZIP CRC 正常，xl/workbook.xml 等 7 个 XML 可解析 |
| OFD | 8110604 | ZIP CRC 正常，OFD.xml 等 5 个 XML 可解析 |
| JSON | 42171 | JSON 可解析，含 format/images/version |

两张英文图片的 `single-page` 返回两个 TXT 资产，`multi-page` 返回一个合并 TXT；合并文件 5296 字节，与两个单独文件按顺序字节拼接完全一致。正常接口用例合计 36/36 成功。

另在仓库目录和 Release 目录各新建一个隔离进程，首个 OCR 请求均直接执行 `en` 取字，均返回 0 和 41 个文字块；两次 Runtime 均正常退出，覆盖了旧打包的稳定崩溃触发条件。

最终完整运行的 Runtime 在识别、导出和资产下载后收到 SIGTERM，显式排空 OCR 并释放 handler，退出码为 0。所有本次隔离测试进程均已停止，已安装的 Runtime 未改动。旧打包首英文、原混用及静态析构崩溃日志保留在历史诊断目录，最终运行日志用于对照。

## 验证边界

- 已完成 macOS arm64 本地验证，未完成 Windows/Linux 原生构建及真机验证；Windows bridge 字段与接收端已接通并经过源码审查。mac-x86-64 复用同一打包规则，未原生运行。
- 白名单中的 89 个具体语言，本次真实样张覆盖 `ch/en/chinese_cht`；其他语言仅验证集合与协议，不代表逐语种质量验收。
- 未人为损坏模型注入 `czocr_set_lang` 失败；该错误在逐页循环前直接终止任务的路径由源码审查确认。
- 非 SDK 的 `PlatformDefault` 最终选择 CZOCR、手写及方向识别仍有既有多 handler 风险，本次只修复 SDK 明确使用的 CZOCR 导出/取字链路。
- GBrain：已查询，未命中直接相关的语言字段决策或本次打包缺陷说明；以当前仓库代码与实测为准。
