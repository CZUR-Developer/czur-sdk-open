# CZUR Open SDK Python Demo

这个目录提供一个基于 Python 的 Open SDK 命令行示例，特点是：

- 先建立 Command WebSocket，再建立 Video WebSocket
- Video 只接收并丢弃，不做渲染
- 支持自动发现设备并选择目标设备
- 支持 `--device-id` 强制指定设备
- 持续监听 Command 通道的 `capture.completed` 事件，支持 URL 下载或本地 path 复制，并输出绝对保存路径
- 支持 `--capture` 主动拍照，按指定数量逐张采集并保存后退出（默认 1 张）

## 运行前提

- 本机已启动 `sdk_open_app`
- 准备好 Open SDK API Key
- Python 3.10+（脚本仅依赖标准库）

## 依赖安装

脚本本身不依赖第三方库。

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

## 启动示例

```bash
export SDK_OPEN_API_KEY="your-open-sdk-api-key"
python3 sdk_open_demo.py \
  --command-url ws://127.0.0.1:17090 \
  --video-url ws://127.0.0.1:17091
```

不带 `--capture` 时，脚本持续监听 `capture.completed`（例如 SDK 在当前会话发送的硬件拍照完成事件），同时接收并丢弃视频帧，直到按 `Ctrl-C` 退出。只处理当前 Command 连接收到的事件，不订阅其他客户端的采集任务。

不指定 `--output-dir` 时，图片保存到**启动命令时的当前工作目录**，不是脚本所在目录。指定保存目录：

```bash
python3 sdk_open_demo.py --output-dir ./captures
```

每张图片下载成功后输出，例如：

```text
图片已保存: /home/user/captures/cap-123-final.jpg
```

如果你要直接拍照并保存到本地：

```bash
python3 sdk_open_demo.py \
  --api-key "$SDK_OPEN_API_KEY" \
  --capture \
  --output-dir ./captures
```

## 指定采集数量和拍照间隔

`--capture` 默认采集 **1 张**。连续采集时使用：

- `--capture-count N`：主动拍照次数，正整数，默认 `1`。
- `--capture-interval SECONDS`：上一张完成并保存后，到下一张拍照前的等待秒数，默认 `5`；支持非负小数，`0` 表示不额外等待。

```bash
# 拍摄 10 张，默认每张保存后等待 5 秒
python3 sdk_open_demo.py --capture --capture-count 10 --save-mode copy --output-dir ./captures

# 拍摄 3 张，每张保存后等待 2 秒
python3 sdk_open_demo.py --capture --capture-count 3 --capture-interval 2 --save-mode copy
```

第一张在设备和视频通道建立后立即提交，最后一张保存后直接退出，不再等待。
采集串行执行，等待期间仍接收采集事件，视频帧继续接收并丢弃。
注意这不是固定频率定时器：每轮实际耗时还包括拍照、处理与文件保存时间。
间隔过短仍可能触发 SDK 的拍照冷却限制。

控制台会显示 `开始采集 1/3`、每张图片的保存路径和 `采集保存完成 1/3`。
主动任务失败、超时或保存失败时停止后续采集，不自动重试；Ctrl-C 会中断并清理资源。
数量按主动调用 `capture.take` 的次数计算；一次多页输出会保存多张结果图片，硬件事件不计入主动采集次数。
不带 `--capture` 时保持持续监听，数量和间隔参数不生效。

## 两种图片保存方式

通过 `--save-mode download|copy` 选择，默认为 `download`。两种方式均适用于持续监听和 `--capture` 主动拍照。

### 1. URL 下载（默认）

```bash
python3 sdk_open_demo.py --save-mode download --output-dir ./captures \
  --ca-file /usr/local/share/ca-certificates/local-runtime-root-ca.crt
```

优先使用 `asset.download_url`，缺失时使用 `asset.url`，携带 `Authorization: Bearer <session_token>`。
适用于本机或远程 SDK；HTTPS 使用自定义 CA 时需要 `--ca-file`。

### 2. 本地文件复制

```bash
python3 sdk_open_demo.py --save-mode copy --output-dir ./captures
```

直接读取 `asset.path` 并复制到保存目录，不发起图片 HTTP 请求，不移动或删除原图。
**要求 Python 能直接读取该路径**：通常是 SDK 与 Python 在同一台机器上，或两者共享同一路径的文件系统。
`path` 缺失、文件不存在或没有读取权限时明确报错，不自动切换成下载。
反过来，下载失败也不会自动改为复制。

若 Command/Video 使用 `wss://`，复制模式仍需满足 WSS 的证书校验要求；仅图片保存本身不涉及 HTTPS。

不传 `--output-dir` 时，两种方式都保存到运行命令的当前目录。保存成功均输出绝对路径。
最终图片根据 `kind: "final"` 选择，而不是根据文件名判断：即使 `path` 叫 `color_processed.jpg`，
它仍是最终图片。多页、事件去重和临时文件原子落盘规则保持一致。

## HTTPS 下载证书（Conda / 自定义 Python 环境）

指令和视频使用 `ws://` 不代表图片下载也是 HTTP。安装版 SDK 默认返回
`https://sdk-runtime.localhost:18082` 的图片地址，证书由本地 Runtime Root CA 签发。
Conda 等环境可能不使用系统已安装的 CA 信任库，出现
`CERTIFICATE_VERIFY_FAILED: unable to get local issuer certificate`。

用 `--ca-file` 显式加载受信任的根 CA（同时应用于 HTTPS 下载和 WSS）：

```bash
python3 sdk_open_demo.py \
  --ca-file /usr/local/share/ca-certificates/local-runtime-root-ca.crt \
  --output-dir ./captures
```

该路径适用于安装了默认 CA 的 Linux；其他环境请使用自己 SDK 部署对应的根 CA 文件。
在此源码目录中也可使用默认开发 CA：
`../../../../../packaging/sdk_open/tls/runtime/local-runtime-root-ca.crt`。
只使用根证书 `.crt`，不要使用私钥 `.key.pem`，也不要信任来自未知服务的证书。

程序保持证书和主机名校验开启，不自动降级 HTTP，也不修改全局证书库。
默认服务器证书只包含 `sdk-runtime.localhost`，不要把 HTTPS 地址直接替换成
`127.0.0.1`，否则可能出现主机名不匹配。远程部署请使用自己的受信任 CA 和匹配的域名。

## 设备选择规则

1. 如果指定了 `--device-id`，则必须在 `device.list` 结果中存在该设备，否则直接报错。
2. 未指定时，优先选择 `model` 或 `display_name` 中包含 `CZUR` 的设备。
3. 如果没有 `CZUR` 设备，则选择列表中的第一个设备。
4. 如果 `device.list` 为空，则输出“未检测到设备，已退出。”并在退出前结束会话；此时不会建立 Video WebSocket。

## ET / M 系列视频参数

脚本会识别设备 `model` / `display_name`。

- 如果识别为 ET / M 系列，`video.start` 固定使用：
  - `width=1536`
  - `height=1152`
  - `fps=30`
- 其他设备则使用设备/请求默认值，不强制覆盖。

## 拍照保存流程

### 采集事件与控制台状态

持续监听和 `--capture` 主动采集模式都会接收并输出以下事件：

| 事件 | 控制台说明 | 附带状态 |
| --- | --- | --- |
| `capture.started` | 采集开始 | 任务、设备、采集/处理状态 |
| `capture.stage.updated` | 处理阶段更新 | `stage.name`、`stage.status`、阶段消息 |
| `capture.completed` | 采集完成 | 任务最终状态；随后按选择的模式保存图片 |
| `capture.failed` | 采集失败 | 错误码、消息和错误原因 |
| `capture.hardgrab_detected` | 检测到硬件拍照 | 设备、`accepted`、任务 ID（若有）、警告或错误 |

每条事件输出本地时间、英文事件名、中文说明和状态 JSON；仅输出事件实际包含的字段。
不会打印图片二进制、Base64 或整个资产列表。示例：

```text
[14:30:01] capture.started 采集开始 {"task_id": "cap-1", "status": "running", "acquisition_status": "capturing"}
[14:30:02] capture.stage.updated 处理阶段更新 {"task_id": "cap-1", "stage": {"name": "color_processed", "status": "succeeded"}}
[14:30:03] capture.completed 采集完成 {"task_id": "cap-1", "status": "succeeded"}
图片已保存: /home/user/captures/cap-1-color_processed.jpg
```

进度事件和硬件拍照通知仅记录状态，不触发重复拍照或提前保存图片。
硬件拍照通知允许没有任务 ID；只有收到 `capture.completed` 才保存最终结果。

### 处理步骤

1. 在 Command 通道监听 `capture.completed`，事件的 `payload` 是任务快照，包含 `task_id` 和 `assets`，不是图片二进制。
2. 按 `final > page_processed > color_processed > original` 选择最佳处理阶段，保存该阶段的全部图片（多页都保存），不保存缩略图。
3. 根据 `--save-mode` 使用 URL 下载或 `path` 复制，先写临时文件，再原子替换。
4. 文件名加上任务 ID，避免不同拍照任务覆盖同名图片；重复完成事件不会重复保存已保存的资产。保存失败会清理临时文件并输出错误。
5. 每张图片保存后立即输出绝对路径。

本机回环地址（例如 `127.0.0.1`）的图片下载不经过系统 HTTP 代理，避免代理导致本地资源请求出现 502。

带 `--capture` 时，每次调用 `capture.take` 后等待该任务的完成事件并保存，完成指定采集次数后退出；不再轮询 `capture.get`。即使事件早于指令响应到达也会保留。默认等待上限为 60 秒，可用 `--capture-timeout` 设置（秒）。`capture.failed` 会报错，不把失败任务当成成功。

持续监听模式下，单个任务失败或下载失败会输出错误并继续监听；连接断开则退出并清理资源。兼容保留 `--capture-poll-interval` 参数，但现在不生效。

## 测试

```bash
python3 -m unittest discover -s tests -v
```

## 退出行为

- `video.stop`
- `device.close`
- `auth.destroy_session`

都会在退出时尽量执行。
