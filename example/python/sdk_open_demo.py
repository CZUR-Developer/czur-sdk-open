#!/usr/bin/env python3
"""CZUR Open SDK Python demo.

This demo uses the command WebSocket for control flow and the video WebSocket
for frame output. Video frames are discarded on purpose; the example only keeps
basic stats and optionally downloads a captured asset to a local directory.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import ipaddress
import json
import math
import os
import shutil
import select
from collections import deque
import socket
import ssl
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple
from urllib.parse import urljoin, urlparse, urlsplit, urlencode
from urllib.error import URLError
from urllib.request import HTTPSHandler, ProxyHandler, Request, build_opener, urlopen

_WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
_CAPTURE_EVENTS = {
    "capture.started": "采集开始",
    "capture.stage.updated": "处理阶段更新",
    "capture.completed": "采集完成",
    "capture.failed": "采集失败",
    "capture.hardgrab_detected": "检测到硬件拍照",
}
_ASSET_PRIORITY = {
    "final": 0,
    "page_processed": 1,
    "color_processed": 2,
    "original": 3,
}


class SdkOpenError(RuntimeError):
    pass


def create_tls_context(ca_file: Optional[str] = None) -> ssl.SSLContext:
    context = ssl.create_default_context()
    if ca_file:
        context.load_verify_locations(cafile=str(Path(ca_file).expanduser()))
    return context


@dataclass
class VideoDiscardStats:
    binary_frames: int = 0
    binary_bytes: int = 0
    text_messages: int = 0
    events: List[Dict[str, Any]] = field(default_factory=list)


@dataclass
class DemoResult:
    device: Dict[str, Any]
    video_session_token: str
    stream_id: str
    capture_task: Optional[Dict[str, Any]] = None
    capture_asset: Optional[Dict[str, Any]] = None
    downloaded_path: Optional[str] = None
    video_stats: VideoDiscardStats = field(default_factory=VideoDiscardStats)


class WebSocketConnection:
    def __init__(
        self,
        url: str,
        timeout: float = 10.0,
        headers: Optional[Dict[str, str]] = None,
        socket_factory: Callable[..., socket.socket] = socket.create_connection,
        tls_context: Optional[ssl.SSLContext] = None,
    ) -> None:
        self.url = url
        self.timeout = timeout
        self._headers = headers or {}
        self._socket_factory = socket_factory
        self._tls_context = tls_context
        self._sock: Optional[socket.socket] = None
        self._file = None
        self._recv_buffer = b""
        self._connect()

    def _connect(self) -> None:
        parsed = urlsplit(self.url)
        if parsed.scheme not in {"ws", "wss"}:
            raise SdkOpenError(f"unsupported websocket scheme: {parsed.scheme}")
        host = parsed.hostname
        if not host:
            raise SdkOpenError("websocket host missing")
        port = parsed.port or (443 if parsed.scheme == "wss" else 80)
        sock = self._socket_factory((host, port), timeout=self.timeout)
        if parsed.scheme == "wss":
            context = self._tls_context or create_tls_context()
            sock = context.wrap_socket(sock, server_hostname=host)
        sock.settimeout(self.timeout)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        path = parsed.path or "/"
        if parsed.query:
            path += "?" + parsed.query
        request_lines = [
            f"GET {path} HTTP/1.1",
            f"Host: {host}:{port}",
            "Upgrade: websocket",
            "Connection: Upgrade",
            f"Sec-WebSocket-Key: {key}",
            "Sec-WebSocket-Version: 13",
        ]
        for name, value in self._headers.items():
            request_lines.append(f"{name}: {value}")
        # HTTP headers end with an empty line, not just the last header's CRLF.
        sock.sendall(("\r\n".join(request_lines) + "\r\n\r\n").encode("ascii"))

        response = self._read_http_response(sock)
        status_line = response[0]
        if not status_line.startswith("HTTP/1.1 101") and not status_line.startswith("HTTP/1.0 101"):
            raise SdkOpenError(f"websocket handshake failed: {status_line}")
        headers = {}
        for line in response[1:]:
            if not line:
                continue
            if ":" in line:
                name, value = line.split(":", 1)
                headers[name.strip().lower()] = value.strip()
        accept = headers.get("sec-websocket-accept")
        expected = base64.b64encode(hashlib.sha1((key + _WS_GUID).encode("ascii")).digest()).decode("ascii")
        if accept != expected:
            raise SdkOpenError("websocket handshake validation failed")
        self._sock = sock
        self._file = sock.makefile("rb")

    def _read_http_response(self, sock: socket.socket) -> List[str]:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = sock.recv(4096)
            if not chunk:
                raise SdkOpenError("websocket handshake closed unexpectedly")
            data += chunk
        head, rest = data.split(b"\r\n\r\n", 1)
        self._recv_buffer = rest
        return head.decode("iso-8859-1").split("\r\n")

    def _recv_exact(self, size: int) -> bytes:
        if self._sock is None:
            raise SdkOpenError("websocket is closed")
        chunks = []
        remaining = size
        if self._recv_buffer:
            head = self._recv_buffer[:remaining]
            chunks.append(head)
            self._recv_buffer = self._recv_buffer[len(head):]
            remaining -= len(head)
        while remaining > 0:
            chunk = self._sock.recv(remaining)
            if not chunk:
                raise SdkOpenError("websocket closed")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _send_frame(self, opcode: int, payload: bytes = b"") -> None:
        if self._sock is None:
            raise SdkOpenError("websocket is closed")
        first = 0x80 | (opcode & 0x0F)
        mask_bit = 0x80
        length = len(payload)
        if length < 126:
            header = bytes([first, mask_bit | length])
        elif length < (1 << 16):
            header = bytes([first, mask_bit | 126]) + length.to_bytes(2, "big")
        else:
            header = bytes([first, mask_bit | 127]) + length.to_bytes(8, "big")
        mask = os.urandom(4)
        masked = bytes(payload[i] ^ mask[i % 4] for i in range(length))
        self._sock.sendall(header + mask + masked)

    def send_text(self, text: str) -> None:
        self._send_frame(0x1, text.encode("utf-8"))

    def send_binary(self, payload: bytes) -> None:
        self._send_frame(0x2, payload)

    def send_pong(self, payload: bytes = b"") -> None:
        self._send_frame(0xA, payload)

    def recv_frame(self) -> Tuple[int, bytes]:
        if self._sock is None:
            raise SdkOpenError("websocket is closed")
        while True:
            first_two = self._recv_exact(2)
            first, second = first_two[0], first_two[1]
            fin = bool(first & 0x80)
            opcode = first & 0x0F
            masked = bool(second & 0x80)
            length = second & 0x7F
            if length == 126:
                length = int.from_bytes(self._recv_exact(2), "big")
            elif length == 127:
                length = int.from_bytes(self._recv_exact(8), "big")
            mask = self._recv_exact(4) if masked else b""
            payload = self._recv_exact(length) if length else b""
            if masked:
                payload = bytes(payload[i] ^ mask[i % 4] for i in range(length))
            if opcode == 0x8:
                raise EOFError("websocket closed")
            if opcode == 0x9:
                self.send_pong(payload)
                continue
            if opcode == 0xA:
                continue
            if not fin:
                # The public SDK traffic we consume is expected to be unfragmented.
                # If a fragment arrives anyway, treat it as a protocol error.
                raise SdkOpenError("fragmented websocket frames are not supported")
            return opcode, payload

    def wait_readable(self, timeout: Optional[float]) -> bool:
        if self._sock is None:
            raise SdkOpenError("websocket is closed")
        if self._recv_buffer or (isinstance(self._sock, ssl.SSLSocket) and self._sock.pending()):
            return True
        return bool(select.select([self._sock], [], [], timeout)[0])

    def recv_text(self) -> str:
        while True:
            opcode, payload = self.recv_frame()
            if opcode == 0x1:
                return payload.decode("utf-8")
            if opcode == 0x2:
                raise SdkOpenError("unexpected binary frame on command channel")

    def close(self) -> None:
        if self._sock is None:
            return
        try:
            self._send_frame(0x8)
        except Exception:
            pass
        try:
            self._sock.close()
        finally:
            self._sock = None
            if self._file is not None:
                try:
                    self._file.close()
                except Exception:
                    pass
                self._file = None


class CommandChannel:
    def __init__(self, connection: Any, source: str = "python-demo", protocol_version: str = "1.0.0") -> None:
        self._connection = connection
        self._source = source
        self._protocol_version = protocol_version
        self._request_seq = 0
        self._pending: Dict[str, Dict[str, Any]] = {}
        self._events = deque()

    def close(self) -> None:
        close = getattr(self._connection, "close", None)
        if close is not None:
            close()

    def _next_request_id(self, method: str) -> str:
        self._request_seq += 1
        return f"req-{self._request_seq:04d}-{method.replace('.', '-')}"

    def request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        request_id = self._next_request_id(method)
        payload = {
            "request_id": request_id,
            "method": method,
            "params": params or {},
            "client": {
                "source": self._source,
                "protocol_version": self._protocol_version,
                "trace_id": request_id,
            },
        }
        self._connection.send_text(json.dumps(payload, ensure_ascii=False))
        if request_id in self._pending:
            return self._pending.pop(request_id)
        while True:
            message = self._connection.recv_text()
            response = json.loads(message)
            if not isinstance(response, dict):
                continue
            response_request_id = response.get("request_id")
            if response_request_id == request_id:
                return response
            if response_request_id:
                self._pending[str(response_request_id)] = response
                continue
            if response.get("event") in _CAPTURE_EVENTS:
                self._events.append(response)

    def next_event(self, timeout: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Single-reader dispatcher: preserve events arriving before request replies."""
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            if self._events:
                return self._events.popleft()
            remaining = None if deadline is None else deadline - time.monotonic()
            if remaining is not None and remaining <= 0:
                return None
            if not self._connection.wait_readable(remaining):
                return None
            message = json.loads(self._connection.recv_text())
            if not isinstance(message, dict):
                continue
            if message.get("request_id"):
                self._pending[str(message["request_id"])] = message
            elif message.get("event") in _CAPTURE_EVENTS:
                return message



class VideoDiscarder:
    def __init__(self, connection: Any) -> None:
        self._connection = connection
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self.stats = VideoDiscardStats()

    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(target=self._run, name="sdk-open-video-discard", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=5.0)
            self._thread = None
        close = getattr(self._connection, "close", None)
        if close is not None:
            close()

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                opcode, payload = self._connection.recv_frame()
            except EOFError:
                break
            if opcode == 0x2:
                self.stats.binary_frames += 1
                self.stats.binary_bytes += len(payload)
                continue
            if opcode == 0x1:
                self.stats.text_messages += 1
                try:
                    message = json.loads(payload.decode("utf-8"))
                except Exception:
                    continue
                if isinstance(message, dict) and message.get("event"):
                    self.stats.events.append(message)
                continue
            if opcode == 0x9:
                self._connection.send_pong(payload)
            # Other frame types are ignored.


class DemoApp:
    def __init__(
        self,
        command: CommandChannel,
        command_session: Dict[str, Any],
        command_url: str,
        video_url: str,
        output_dir: Path,
        device_id: Optional[str] = None,
        capture: bool = False,
        capture_poll_interval: float = 1.0,
        capture_timeout: float = 60.0,
        video_connection_factory: Callable[[str, float], Any] = None,
        downloader: Callable[..., Any] = urlopen,
        tls_context: Optional[ssl.SSLContext] = None,
        save_mode: str = "download",
        capture_count: int = 1,
        capture_interval: float = 5.0,
    ) -> None:
        self.command = command
        self.command_session = command_session
        self.command_url = command_url
        self.video_url = video_url
        self.output_dir = output_dir.expanduser().resolve()
        self.device_id_override = device_id
        self.capture_enabled = capture
        if not isinstance(capture_count, int) or capture_count < 1:
            raise ValueError("capture_count must be a positive integer")
        if not math.isfinite(capture_interval) or capture_interval < 0:
            raise ValueError("capture_interval must be finite and non-negative")
        self.capture_count = capture_count
        self.capture_interval = capture_interval
        self.capture_poll_interval = capture_poll_interval
        self.capture_timeout = capture_timeout
        self.video_connection_factory = video_connection_factory or self._default_video_connection_factory
        if save_mode not in {"download", "copy"}:
            raise ValueError("save_mode must be download or copy")
        self.save_mode = save_mode
        self.downloader = downloader
        self.tls_context = tls_context
        self.device: Optional[Dict[str, Any]] = None
        self.device_opened = False
        self.video_started = False
        self.video_discarder: Optional[VideoDiscarder] = None
        self.video_session_token: Optional[str] = None
        self.stream_id: Optional[str] = None
        self._session_destroyed = False
        self._saved_assets = set()

    def _default_video_connection_factory(self, url: str, timeout: float) -> WebSocketConnection:
        headers = {}
        return WebSocketConnection(url, timeout=timeout, headers=headers, tls_context=self.tls_context)

    def run(self) -> DemoResult:
        try:
            devices = self._list_devices()
            if not devices:
                print("未检测到设备，已退出。")
                raise SystemExit(1)
            self.device = self._select_device(devices)
            if self.device is None:
                raise SdkOpenError("failed to select device")
            print(f"选择设备: {self.device.get('device_id')} / {self.device.get('model', '')} / {self.device.get('display_name', '')}")
            self._open_device()
            self._start_video()
            if not self.capture_enabled:
                print(f"正在监听采集事件，保存目录: {self.output_dir}；按 Ctrl-C 退出。")
                self._listen_for_captures()
            capture_task = None
            downloaded_path = None
            capture_asset = None
            if self.capture_enabled:
                for index in range(self.capture_count):
                    if index:
                        print(f"等待 {self.capture_interval:g} 秒后拍摄下一张。", flush=True)
                        self._wait_capture_interval()
                    print(f"开始采集 {index + 1}/{self.capture_count}", flush=True)
                    capture_task, capture_asset, downloaded_path = self._capture_and_download()
                    print(f"采集保存完成 {index + 1}/{self.capture_count}", flush=True)
            return DemoResult(
                device=self.device,
                video_session_token=self.video_session_token or "",
                stream_id=self.stream_id or "",
                capture_task=capture_task,
                capture_asset=capture_asset,
                downloaded_path=downloaded_path,
                video_stats=self.video_discarder.stats if self.video_discarder else VideoDiscardStats(),
            )
        finally:
            self._cleanup()

    def _list_devices(self) -> List[Dict[str, Any]]:
        response = self.command.request("device.list", {})
        self._ensure_ok(response, "device.list")
        data = response.get("data") or {}
        devices = data.get("devices") or []
        if not isinstance(devices, list):
            raise SdkOpenError("device.list returned invalid devices payload")
        return [device for device in devices if isinstance(device, dict)]

    def _select_device(self, devices: List[Dict[str, Any]]) -> Optional[Dict[str, Any]]:
        if self.device_id_override:
            for device in devices:
                if str(device.get("device_id", "")) == self.device_id_override:
                    return device
            raise SdkOpenError(f"device_id not found: {self.device_id_override}")
        czur_candidates = [
            device for device in devices
            if self._device_text(device).find("czur") >= 0
        ]
        if czur_candidates:
            return czur_candidates[0]
        return devices[0] if devices else None

    @staticmethod
    def _device_text(device: Dict[str, Any]) -> str:
        return f"{device.get('model', '')} {device.get('display_name', '')}".lower()

    def _is_et_or_m_series(self) -> bool:
        if self.device is None:
            return False
        text = self._device_text(self.device)
        import re
        return re.search(r"(?:^|[^a-z0-9])(et\d*|et|m\d*|m)(?:[^a-z0-9]|$)", text) is not None

    def _open_device(self) -> None:
        assert self.device is not None
        response = self.command.request("device.open", {"device_id": self.device["device_id"]})
        self._ensure_ok(response, "device.open")
        self.device_opened = True

    def _start_video(self) -> None:
        assert self.device is not None
        video_params = {
            "device_id": self.device["device_id"],
            "pixel_format": "mjpeg",
        }
        if self._is_et_or_m_series():
            video_params.update({"width": 1536, "height": 1152, "fps": 30})
        response = self.command.request(
            "video.start",
            video_params,
        )
        self._ensure_ok(response, "video.start")
        data = response.get("data") or {}
        self.stream_id = str(data.get("stream_id", ""))
        self.video_session_token = str(data.get("session_token") or self.command_session.get("session_token") or "")
        if not self.stream_id:
            raise SdkOpenError("video.start did not return stream_id")
        if not self.video_session_token:
            raise SdkOpenError("video.start did not return session_token")
        video_conn = self._open_video_connection()
        self.video_discarder = VideoDiscarder(video_conn)
        self.video_discarder.start()
        self.video_started = True
        print(f"视频通道已建立: stream_id={self.stream_id}")

    def _open_video_connection(self) -> Any:
        assert self.video_session_token is not None
        assert self.stream_id is not None
        query = urlencode({"session_token": self.video_session_token, "stream_id": self.stream_id})
        url = self.video_url + ("&" if "?" in self.video_url else "?") + query
        return self.video_connection_factory(url, 10.0)

    def _capture_and_download(self) -> Tuple[Dict[str, Any], Optional[Dict[str, Any]], Optional[str]]:
        assert self.device is not None
        capture_profile = {
            "profile_version": "capture.profile.v1",
            "capture": {
                "page_processing": "single_page",
                "single_page": {
                    "auto_rotate": True,
                    "smart_black_edge_optimize": True,
                    "multi_target_paging": False,
                    "realtime_detect_rects": False,
                },
            },
        }
        response = self.command.request(
            "capture.take",
            {
                "device_id": self.device["device_id"],
                "profile": capture_profile,
            },
        )
        self._ensure_ok(response, "capture.take")
        task_id = str((response.get("data") or {}).get("task_id") or "")
        if not task_id:
            raise SdkOpenError("capture.take did not return task_id")
        print(f"capture.take 已提交: task_id={task_id}")
        return self._wait_for_capture(task_id)

    def _listen_for_captures(self) -> None:
        while True:
            event = self.command.next_event(timeout=1.0)
            if event is None:
                continue
            try:
                self._save_capture_event(event)
            except (SdkOpenError, OSError, ValueError) as exc:
                print(f"采集图片保存失败: {exc}", file=sys.stderr)

    def _wait_capture_interval(self) -> None:
        # Wait after saving the previous capture, while still receiving events.
        deadline = time.monotonic() + self.capture_interval
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            event = self.command.next_event(timeout=remaining)
            if event is None:
                return
            try:
                self._save_capture_event(event)
            except (SdkOpenError, OSError, ValueError) as exc:
                print(f"采集图片保存失败: {exc}", file=sys.stderr)

    def _wait_for_capture(self, task_id: str) -> Tuple[Dict[str, Any], Optional[Dict[str, Any]], Optional[str]]:
        deadline = time.monotonic() + self.capture_timeout
        while time.monotonic() < deadline:
            event = self.command.next_event(timeout=max(0.0, deadline - time.monotonic()))
            if event is None:
                break
            event_task_id = str((event.get("payload") or {}).get("task_id") or "")
            try:
                result = self._save_capture_event(event)
            except (SdkOpenError, OSError, ValueError) as exc:
                if event_task_id == task_id:
                    raise
                print(f"采集图片保存失败: {exc}", file=sys.stderr)
                continue
            if event_task_id == task_id and result is not None:
                return result
        raise SdkOpenError(f"capture.completed timeout for task {task_id}")

    def _save_capture_event(self, event: Dict[str, Any]) -> Optional[Tuple[Dict[str, Any], Optional[Dict[str, Any]], Optional[str]]]:
        if event.get("event") not in _CAPTURE_EVENTS:
            return None
        self._log_capture_event(event)
        # Progress and hardware notifications aren't completed image assets.
        # In particular, hardgrab_detected can arrive without a task_id.
        if event.get("event") not in {"capture.completed", "capture.failed"}:
            return None
        task = event.get("payload")
        if not isinstance(task, dict) or not task.get("task_id"):
            raise SdkOpenError("capture event missing task_id/payload")
        task_id = str(task["task_id"])
        if event.get("event") == "capture.failed" or event.get("code", 0) != 0 or task.get("code", 0) != 0:
            raise SdkOpenError(f"capture failed: {task_id}: {task.get('message') or event.get('message') or task.get('error')}")
        best = self._select_capture_asset(task)
        if best is None:
            raise SdkOpenError(f"capture.completed has no image assets: {task_id}")
        # Save all pages of the best available processing stage, not thumbnails.
        assets = [asset for asset in task["assets"]
                  if isinstance(asset, dict) and asset.get("kind") == best.get("kind")]
        first_path = None
        for asset in assets:
            key = (task_id, str(asset.get("asset_id") or asset.get("download_url") or asset.get("url")))
            if key in self._saved_assets:
                continue
            if self.save_mode == "copy":
                path = self._copy_asset(asset, task_id)
            else:
                path = self._download_asset(asset, task_id)
            self._saved_assets.add(key)
            print(f"图片已保存: {path}", flush=True)
            first_path = first_path or path
        return task, best, first_path

    @staticmethod
    def _log_capture_event(event: Dict[str, Any]) -> None:
        name = event["event"]
        payload = event.get("payload")
        payload = payload if isinstance(payload, dict) else {}
        # Only print status metadata, never large assets/base64 image payloads.
        summary = {key: payload[key] for key in (
            "task_id", "device_id", "status", "acquisition_status", "processing_status",
            "accepted", "auto_capture", "code", "message", "warning", "error",
        ) if key in payload}
        for key in ("code", "message"):
            if key not in summary and key in event:
                summary[key] = event[key]
        stage = payload.get("stage")
        if isinstance(stage, dict):
            summary["stage"] = {key: stage[key] for key in ("name", "status", "message") if key in stage}
        print(f"[{time.strftime('%H:%M:%S')}] {name} {_CAPTURE_EVENTS[name]} "
              f"{json.dumps(summary, ensure_ascii=False)}", flush=True)

    def _select_capture_asset(self, task: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        assets = task.get("assets") or []
        if not isinstance(assets, list) or not assets:
            return None
        candidates = [asset for asset in assets if isinstance(asset, dict)
                      and asset.get("kind") in _ASSET_PRIORITY]
        if not candidates:
            return None
        return sorted(
            candidates,
            key=lambda asset: (
                _ASSET_PRIORITY.get(str(asset.get("kind") or "").lower(), 99),
                int(asset.get("index") or 0),
                str(asset.get("asset_id") or ""),
            ),
        )[0]

    def _download_asset(self, asset: Dict[str, Any], task_id: str) -> str:
        download_url = str(asset.get("download_url") or asset.get("url") or "")
        if not download_url:
            raise SdkOpenError("selected asset has no download_url")
        headers = {"Authorization": f"Bearer {self.command_session['session_token']}"}
        request = Request(download_url, headers=headers)
        downloader = self.downloader
        host = urlsplit(download_url).hostname or ""
        try:
            is_loopback = ipaddress.ip_address(host).is_loopback
        except ValueError:
            is_loopback = host.lower() == "localhost" or host.lower().endswith(".localhost")
        if downloader is urlopen:
            handlers = [HTTPSHandler(context=self.tls_context or create_tls_context())]
            if is_loopback:
                # Includes sdk-runtime.localhost, used by the installed runtime.
                handlers.append(ProxyHandler({}))
            downloader = build_opener(*handlers).open
        try:
            return self._save_asset_from_stream(asset, task_id, lambda: downloader(request, timeout=30))
        except Exception as exc:
            reason = exc.reason if isinstance(exc, URLError) else exc
            if isinstance(reason, ssl.SSLCertVerificationError):
                raise SdkOpenError(
                    f"HTTPS certificate verification failed for {host}: {reason.verify_message}. "
                    "Use --ca-file /path/to/local-runtime-root-ca.crt to trust the SDK CA; "
                    "the download hostname must also match the certificate "
                    "(default: sdk-runtime.localhost)."
                ) from exc
            raise

    def _copy_asset(self, asset: Dict[str, Any], task_id: str) -> str:
        source_path = asset.get("path")
        if not isinstance(source_path, str) or not source_path:
            raise SdkOpenError("copy mode requires asset.path")
        source = Path(source_path)
        if not source.is_file():
            raise SdkOpenError(f"asset.path is not a local file: {source}; use --save-mode download for remote SDK assets")
        return self._save_asset_from_stream(asset, task_id, lambda: source.open("rb"))

    def _save_asset_from_stream(self, asset: Dict[str, Any], task_id: str, open_source: Callable[[], Any]) -> str:
        """Share filenames and atomic writes between HTTP downloads and local copies."""
        download_url = str(asset.get("download_url") or asset.get("url") or "")
        self.output_dir.mkdir(parents=True, exist_ok=True)
        # Server paths may use either platform's separators. Keep filenames local
        # and include the task ID so separate captures cannot overwrite each other.
        safe_task_id = "".join(c if c.isalnum() or c in "-_" else "_" for c in task_id)
        filename = self._derive_local_filename(asset, download_url).replace("\\", "/").rsplit("/", 1)[-1]
        if filename in {"", ".", ".."}:
            filename = "capture.bin"
        target_name = f"{safe_task_id}-{filename}"
        final_path = self.output_dir / target_name
        if final_path.exists() and final_path.is_dir():
            raise SdkOpenError(f"output path is a directory: {final_path}")
        fd, tmp_name = tempfile.mkstemp(prefix=f".{target_name}.", suffix=".part", dir=str(self.output_dir))
        os.close(fd)
        tmp_path = Path(tmp_name)
        try:
            with open_source() as source, tmp_path.open("wb") as writer:
                shutil.copyfileobj(source, writer)
                writer.flush()
                os.fsync(writer.fileno())
            os.replace(tmp_path, final_path)
        except BaseException:
            # Also clean up if Ctrl-C interrupts a download/copy.
            try:
                tmp_path.unlink()
            except FileNotFoundError:
                pass
            raise
        return str(final_path)

    @staticmethod
    def _derive_local_filename(asset: Dict[str, Any], download_url: str) -> str:
        candidate = Path(urlparse(download_url).path).name
        if not candidate or candidate == "download":
            candidate = str(asset.get("path") or "").replace("\\", "/").rsplit("/", 1)[-1]
        asset_id = candidate or str(asset.get("asset_id") or "capture")
        content_type = str(asset.get("content_type") or "").lower()
        extension = {
            "image/jpeg": ".jpg",
            "image/jpg": ".jpg",
            "image/png": ".png",
            "image/tiff": ".tiff",
            "application/pdf": ".pdf",
            "application/ofd": ".ofd",
        }.get(content_type, "")
        if not Path(asset_id).suffix and extension:
            asset_id += extension
        return asset_id or "capture.bin"

    def _ensure_ok(self, response: Dict[str, Any], method: str) -> None:
        code = int(response.get("code") or 0)
        if code != 0:
            message = str(response.get("message") or f"{method} failed")
            raise SdkOpenError(f"{method} failed: {code} {message}")

    def _cleanup(self) -> None:
        errors: List[Exception] = []
        if self.video_started and self.device is not None and self.video_session_token and self.stream_id:
            try:
                self.command.request("video.stop", {"device_id": self.device["device_id"]})
            except Exception as exc:
                errors.append(exc)
        if self.video_discarder is not None:
            try:
                self.video_discarder.stop()
            except Exception as exc:
                errors.append(exc)
        if self.device_opened and self.device is not None:
            try:
                self.command.request("device.close", {"device_id": self.device["device_id"]})
            except Exception as exc:
                errors.append(exc)
        if not self._session_destroyed:
            try:
                self._destroy_session()
            except Exception as exc:
                errors.append(exc)
        self.command.close()
        if errors:
            # Keep cleanup best-effort; surface the first problem only if nothing else
            # has already failed.
            pass

    def _destroy_session(self) -> None:
        response = self.command.request("auth.destroy_session", {})
        self._ensure_ok(response, "auth.destroy_session")
        self._session_destroyed = True


def positive_count(value: str) -> int:
    try:
        count = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("capture count must be a positive integer")
    if count < 1:
        raise argparse.ArgumentTypeError("capture count must be a positive integer")
    return count


def nonnegative_seconds(value: str) -> float:
    try:
        seconds = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("capture interval must be finite and non-negative")
    if not math.isfinite(seconds) or seconds < 0:
        raise argparse.ArgumentTypeError("capture interval must be finite and non-negative")
    return seconds


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="CZUR Open SDK Python demo")
    parser.add_argument("--api-key", default=os.environ.get("SDK_OPEN_API_KEY", ""), help="Open SDK API key")
    parser.add_argument("--command-url", default=os.environ.get("SDK_OPEN_COMMAND_URL", "ws://127.0.0.1:17090"), help="Command WebSocket URL")
    parser.add_argument("--video-url", default=os.environ.get("SDK_OPEN_VIDEO_URL", "ws://127.0.0.1:17091"), help="Video WebSocket URL")
    parser.add_argument("--device-id", default="", help="Preferred device_id")
    parser.add_argument("--output-dir", default=".", help="Save directory (default: current working directory)")
    parser.add_argument("--save-mode", choices=("download", "copy"), default="download", help="Save via asset URL (download) or local asset.path (copy)")
    parser.add_argument("--capture", action="store_true", help="Run capture.take, wait for completion and save each capture")
    parser.add_argument("--capture-count", type=positive_count, default=1, help="Number of captures with --capture (default: 1)")
    parser.add_argument("--capture-interval", type=nonnegative_seconds, default=5.0, help="Seconds to wait after saving before next capture (default: 5)")
    parser.add_argument("--capture-timeout", type=float, default=60.0, help="Maximum seconds to wait for capture.completed with --capture")
    parser.add_argument("--capture-poll-interval", type=float, default=1.0, help="Deprecated; ignored (capture completion is event-driven)")
    parser.add_argument("--request-timeout", type=float, default=10.0, help="WebSocket request timeout")
    parser.add_argument("--ca-file", help="Additional trusted root CA PEM file for HTTPS downloads and WSS")
    return parser


def create_command_session(command_url: str, api_key: str, request_timeout: float, tls_context: Optional[ssl.SSLContext] = None) -> Tuple[CommandChannel, Dict[str, Any]]:
    connection = WebSocketConnection(command_url, timeout=request_timeout, tls_context=tls_context)
    command = CommandChannel(connection)
    try:
        response = command.request("auth.create_session", {"token": api_key})
        code = int(response.get("code") or 0)
        if code != 0:
            raise SdkOpenError(f"auth.create_session failed: {code} {response.get('message')}")
        data = response.get("data") or {}
        session_token = str(data.get("session_token") or "")
        if not session_token:
            raise SdkOpenError("auth.create_session did not return session_token")
        return command, data
    except Exception:
        command.close()
        raise


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    if not args.api_key:
        parser.error("--api-key is required (or set SDK_OPEN_API_KEY)")
    command = None
    try:
        tls_context = create_tls_context(args.ca_file)
        command, session = create_command_session(args.command_url, args.api_key, args.request_timeout, tls_context)
        app = DemoApp(
            command=command,
            command_session=session,
            command_url=args.command_url,
            video_url=args.video_url,
            output_dir=Path(args.output_dir),
            device_id=args.device_id or None,
            capture=args.capture,
            capture_poll_interval=args.capture_poll_interval,
            capture_timeout=args.capture_timeout,
            tls_context=tls_context,
            save_mode=args.save_mode,
            capture_count=args.capture_count,
            capture_interval=args.capture_interval,
        )
        app.run()
        return 0
    except (SdkOpenError, OSError, TimeoutError, ValueError) as exc:
        print(f"错误: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("已中断，正在退出。", file=sys.stderr)
        return 130
    finally:
        if command is not None:
            try:
                command.close()
            except Exception:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
