import base64
import hashlib
import io
import json
import socket
import ssl
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import sdk_open_demo as demo


class FakeTextConnection:
    def __init__(self, incoming):
        self.incoming = list(incoming)
        self.sent = []
        self.recv_calls = 0
        self.closed = False

    def send_text(self, text):
        self.sent.append(text)

    def wait_readable(self, timeout):
        return bool(self.incoming)

    def recv_text(self):
        self.recv_calls += 1
        if not self.incoming:
            raise AssertionError("unexpected recv_text call")
        return self.incoming.pop(0)

    def close(self):
        self.closed = True


class ScriptedCommand:
    def __init__(self, scripted_responses, events=()):
        self.events = iter(events)
        self.scripted_responses = list(scripted_responses)
        self.requests = []
        self.closed = False

    def request(self, method, params):
        self.requests.append((method, params))
        if not self.scripted_responses:
            raise AssertionError(f"unexpected request: {method} {params}")
        expected_method, response = self.scripted_responses.pop(0)
        self.assert_method(expected_method, method)
        return response

    def close(self):
        self.closed = True

    def next_event(self, timeout=None):
        event = next(self.events)
        if isinstance(event, BaseException):
            raise event
        return event

    @staticmethod
    def assert_method(expected, actual):
        if expected != actual:
            raise AssertionError(f"expected method {expected!r}, got {actual!r}")


class FakeVideoConnection:
    def __init__(self, frames):
        self.frames = list(frames)
        self.sent_pongs = []
        self.closed = False

    def recv_frame(self):
        if not self.frames:
            raise EOFError()
        frame = self.frames.pop(0)
        if isinstance(frame, Exception):
            raise frame
        return frame

    def send_pong(self, payload=b""):
        self.sent_pongs.append(payload)

    def close(self):
        self.closed = True


class DemoTests(unittest.TestCase):
    def test_main_does_not_print_video_statistics(self):
        command = mock.Mock()
        with mock.patch.object(demo, "create_command_session", return_value=(command, {})), \
                mock.patch.object(demo, "DemoApp") as app, \
                mock.patch("sys.stdout", new=io.StringIO()) as stdout:
            self.assertEqual(demo.main(["--api-key", "test", "--capture"]), 0)
        app.return_value.run.assert_called_once()
        self.assertEqual(stdout.getvalue(), "")
        command.close.assert_called_once()

    def test_websocket_handshake_terminates_headers_and_preserves_first_frame(self):
        for headers in (None, {"X-Demo-Test": "handshake"}):
            with self.subTest(headers=headers):
                sock = mock.Mock()

                def accept_handshake(request):
                    # A server cannot finish parsing until the empty line arrives.
                    self.assertTrue(request.endswith(b"\r\n\r\n"))
                    self.assertEqual(request.count(b"\r\n\r\n"), 1)
                    self.assertTrue(request.startswith(b"GET /command?test=1 HTTP/1.1\r\n"))
                    key_line = next(line for line in request.split(b"\r\n")
                                    if line.startswith(b"Sec-WebSocket-Key: "))
                    key = key_line.split(b": ", 1)[1]
                    accept = base64.b64encode(hashlib.sha1(
                        key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
                    ).digest())
                    # Split the HTTP response, with the first WS message coalesced.
                    sock.recv.side_effect = [
                        b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n",
                        b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept
                        + b"\r\n\r\n\x81\x02{}",
                    ]

                sock.sendall.side_effect = accept_handshake
                connection = demo.WebSocketConnection(
                    "ws://127.0.0.1:17090/command?test=1", headers=headers,
                    socket_factory=mock.Mock(return_value=sock),
                )
                sock.sendall.side_effect = None
                try:
                    self.assertEqual(connection.recv_text(), "{}")
                finally:
                    connection.close()
                sock.close.assert_called_once()

    def test_capture_event_before_response_is_not_lost(self):
        event = {"event": "capture.completed", "payload": {"task_id": "cap-1"}}
        conn = FakeTextConnection([
            json.dumps(event),
            json.dumps({"request_id": "req-0001-capture-take", "code": 0}),
        ])
        channel = demo.CommandChannel(conn)
        channel.request("capture.take")
        self.assertEqual(channel.next_event(timeout=1), event)
        self.assertEqual(conn.recv_calls, 2)

    def test_idle_and_interleaved_command_response_during_event_wait(self):
        event = {"event": "capture.completed", "payload": {"task_id": "cap-1"}}
        conn = FakeTextConnection([])
        channel = demo.CommandChannel(conn)
        self.assertIsNone(channel.next_event(timeout=0.01))
        conn.incoming.extend([
            json.dumps({"event": "capture.progress", "payload": {}}),
            json.dumps({"request_id": "req-0001-system-ping", "code": 0}),
            json.dumps(event),
        ])
        self.assertEqual(channel.next_event(timeout=1), event)
        self.assertEqual(channel.request("system.ping")["code"], 0)

    def test_multipage_and_separate_tasks_do_not_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            app = self.make_event_app(output_dir=Path(temp), downloader=lambda *a, **k: io.BytesIO(b"photo"))
            assets = [{"asset_id": f"final-{i}", "kind": "final", "content_type": "image/jpeg",
                       "download_url": f"http://example.test/final-{i}/download"} for i in range(2)]
            for task_id in ("cap-1", "cap-2"):
                app._save_capture_event({"event": "capture.completed", "payload": {
                    "task_id": task_id, "assets": assets}})
            paths = list(Path(temp).iterdir())
            self.assertEqual(len(paths), 4)
            self.assertTrue(all(p.suffix == ".jpg" for p in paths))

    def test_failed_download_cleans_partial_file_and_can_retry(self):
        with tempfile.TemporaryDirectory() as temp:
            downloader = mock.Mock(side_effect=OSError("download interrupted"))
            app = self.make_event_app(output_dir=Path(temp), downloader=downloader)
            event = {"event": "capture.completed", "payload": {"task_id": "cap-1", "assets": [{
                "asset_id": "final", "kind": "final", "content_type": "image/jpeg",
                "download_url": "http://example.test/final/download"}]}}
            with self.assertRaises(OSError):
                app._save_capture_event(event)
            self.assertEqual(list(Path(temp).iterdir()), [])
            downloader.side_effect = lambda *a, **k: io.BytesIO(b"photo")
            app._save_capture_event(event)
            self.assertEqual(len(list(Path(temp).iterdir())), 1)

    def test_loopback_websocket_event_downloads_over_http(self):
        auth_headers = []

        class AssetHandler(BaseHTTPRequestHandler):
            def do_GET(self):
                auth_headers.append(self.headers.get("Authorization"))
                self.send_response(200)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Content-Length", "5")
                self.end_headers()
                self.wfile.write(b"photo")

            def log_message(self, *args):
                pass

        http = ThreadingHTTPServer(("127.0.0.1", 0), AssetHandler)
        http_thread = threading.Thread(target=http.serve_forever, daemon=True)
        http_thread.start()
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(3)
        release = threading.Event()
        errors = []
        event = {"event": "capture.completed", "code": 0, "payload": {
            "task_id": "cap-real", "status": "succeeded", "assets": [{
                "asset_id": "final", "kind": "final", "content_type": "image/jpeg",
                "download_url": f"http://127.0.0.1:{http.server_port}/api/assets/cap-real/final/download",
            }]}}

        def serve_websocket():
            try:
                with listener.accept()[0] as sock:
                    sock.settimeout(3)
                    request = b""
                    while b"\r\n\r\n" not in request:
                        part = sock.recv(4096)
                        if not part:
                            raise EOFError("client closed during handshake")
                        request += part
                    key = next(line.split(b": ", 1)[1] for line in request.split(b"\r\n")
                               if line.startswith(b"Sec-WebSocket-Key:"))
                    accept = base64.b64encode(hashlib.sha1(key + demo._WS_GUID.encode()).digest())
                    sock.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                 b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
                    payload = json.dumps(event).encode()
                    sock.sendall(b"\x81\x7e" + len(payload).to_bytes(2, "big") + payload)
                    release.wait(3)
            except Exception as exc:
                errors.append(exc)

        ws_thread = threading.Thread(target=serve_websocket, daemon=True)
        ws_thread.start()
        connection = None
        try:
            connection = demo.WebSocketConnection(f"ws://127.0.0.1:{listener.getsockname()[1]}", timeout=3)
            channel = demo.CommandChannel(connection)
            with tempfile.TemporaryDirectory() as temp:
                app = self.make_event_app(output_dir=Path(temp))
                app.command = channel
                result = app._wait_for_capture("cap-real")
                self.assertEqual(Path(result[2]).read_bytes(), b"photo")
                self.assertTrue(Path(result[2]).is_absolute())
            self.assertEqual(auth_headers, ["Bearer ss-test"])
        finally:
            if connection is not None:
                connection.close()
            release.set()
            ws_thread.join(4)
            listener.close()
            http.shutdown()
            http.server_close()
            http_thread.join(4)
        self.assertEqual(errors, [])

    def test_custom_ca_extends_default_trust(self):
        context = mock.Mock()
        with mock.patch.object(demo.ssl, "create_default_context", return_value=context):
            self.assertIs(demo.create_tls_context("/tmp/local-runtime-root-ca.crt"), context)
        context.load_verify_locations.assert_called_once_with(cafile="/tmp/local-runtime-root-ca.crt")
        secure_default = demo.create_tls_context()
        self.assertTrue(secure_default.check_hostname)
        self.assertEqual(secure_default.verify_mode, ssl.CERT_REQUIRED)
        args = demo.build_arg_parser().parse_args(["--ca-file", "custom.crt"])
        self.assertEqual(args.ca_file, "custom.crt")

    def test_https_download_uses_custom_ca_and_bypasses_localhost_proxy(self):
        with tempfile.TemporaryDirectory() as temp:
            context = demo.create_tls_context()
            app = self.make_event_app(output_dir=Path(temp), tls_context=context)
            opener = mock.Mock()
            opener.open.return_value = io.BytesIO(b"photo")
            with mock.patch.object(demo, "build_opener", return_value=opener) as build:
                path = app._download_asset({
                    "asset_id": "final", "content_type": "image/jpeg",
                    "download_url": "https://sdk-runtime.localhost:18082/api/assets/cap-1/final/download",
                }, "cap-1")
            handlers = build.call_args.args
            https = next(h for h in handlers if isinstance(h, demo.HTTPSHandler))
            proxy = next(h for h in handlers if isinstance(h, demo.ProxyHandler))
            self.assertIs(https._context, context)
            self.assertEqual(proxy.proxies, {})
            self.assertEqual(Path(path).read_bytes(), b"photo")

    def test_certificate_failure_gives_ca_hint_and_removes_partial_file(self):
        error = ssl.SSLCertVerificationError(1, "certificate verify failed")
        error.verify_message = "unable to get local issuer certificate"
        with tempfile.TemporaryDirectory() as temp:
            app = self.make_event_app(output_dir=Path(temp), downloader=mock.Mock(side_effect=demo.URLError(error)))
            with self.assertRaisesRegex(demo.SdkOpenError, "--ca-file"):
                app._download_asset({"asset_id": "final", "download_url": "https://sdk-runtime.localhost/final/download"}, "cap-1")
            self.assertEqual(list(Path(temp).iterdir()), [])

    def test_command_and_video_share_tls_context(self):
        context = demo.create_tls_context()
        fake = FakeTextConnection([json.dumps({
            "request_id": "req-0001-auth-create_session", "code": 0,
            "data": {"session_token": "ss-test"}})])
        with mock.patch.object(demo, "WebSocketConnection", return_value=fake) as ws:
            demo.create_command_session("wss://sdk-runtime.localhost:18090", "test", 3, context)
            self.assertIs(ws.call_args.kwargs["tls_context"], context)
            app = self.make_event_app(output_dir=Path.cwd(), tls_context=context)
            app._default_video_connection_factory("wss://sdk-runtime.localhost:18091", 3)
            self.assertIs(ws.call_args.kwargs["tls_context"], context)

    def test_save_mode_cli_defaults_and_validation(self):
        parser = demo.build_arg_parser()
        self.assertEqual(parser.parse_args([]).save_mode, "download")
        self.assertEqual(parser.parse_args(["--save-mode", "copy"]).save_mode, "copy")
        with mock.patch("sys.stderr", new=io.StringIO()):
            with self.assertRaises(SystemExit):
                parser.parse_args(["--save-mode", "invalid"])

    def test_copy_mode_saves_final_asset_without_network(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "sdk" / "color_processed.jpg"
            source.parent.mkdir()
            source.write_bytes(b"final-photo")
            output = Path(temp) / "saved"
            downloader = mock.Mock(side_effect=AssertionError("copy must not use HTTP"))
            app = self.make_event_app(output_dir=output, save_mode="copy", downloader=downloader)
            event = {"event": "capture.completed", "payload": {"task_id": "cap-1", "assets": [{
                "asset_id": "asset-final", "kind": "final", "path": str(source),
                "download_url": "https://sdk-runtime.localhost/api/assets/cap-1/asset-final/download",
                "content_type": "image/jpeg"}]}}
            with mock.patch("sys.stdout", new=io.StringIO()) as stdout:
                result = app._save_capture_event(event)
                app._save_capture_event(event)
            saved = Path(result[2])
            self.assertEqual(saved.parent, output.resolve())
            self.assertEqual(saved.read_bytes(), b"final-photo")
            self.assertEqual(source.read_bytes(), b"final-photo")
            self.assertEqual(stdout.getvalue().count(str(saved)), 1)
            self.assertEqual(list(output.iterdir()), [saved])
            downloader.assert_not_called()

    def test_copy_mode_missing_or_unreadable_path_does_not_download(self):
        with tempfile.TemporaryDirectory() as temp:
            app = self.make_event_app(output_dir=Path(temp) / "out", save_mode="copy", downloader=mock.Mock())
            for path in (None, str(Path(temp) / "missing.jpg"), temp):
                with self.subTest(path=path), self.assertRaises(demo.SdkOpenError):
                    app._copy_asset({"path": path}, "cap-1")
            source = Path(temp) / "image.jpg"
            source.write_bytes(b"photo")
            with mock.patch.object(Path, "open", side_effect=PermissionError("no access")):
                with self.assertRaises(PermissionError):
                    app._copy_asset({"path": str(source)}, "cap-1")
            self.assertEqual(list(app.output_dir.iterdir()), [])
            app.downloader.assert_not_called()

    def test_download_mode_supports_url_without_local_path(self):
        with tempfile.TemporaryDirectory() as temp:
            downloader = mock.Mock(side_effect=lambda *a, **k: io.BytesIO(b"remote-photo"))
            app = self.make_event_app(output_dir=Path(temp), save_mode="download", downloader=downloader)
            with mock.patch.object(app, "_copy_asset", side_effect=AssertionError("must not copy")):
                result = app._save_capture_event({"event": "capture.completed", "payload": {
                    "task_id": "cap-1", "assets": [{"kind": "final", "asset_id": "final", "content_type": "image/jpeg",
                    "url": "https://example.test/api/assets/cap-1/final"}]}})
            self.assertEqual(Path(result[2]).read_bytes(), b"remote-photo")
            req = downloader.call_args.args[0]
            self.assertEqual(req.full_url, "https://example.test/api/assets/cap-1/final")
            self.assertEqual(req.get_header("Authorization"), "Bearer ss-test")

    def test_all_capture_events_are_preserved_before_and_after_responses(self):
        events = [{"event": name, "payload": {"task_id": "cap-1"}} for name in (
            "capture.started", "capture.stage.updated", "capture.completed",
            "capture.failed", "capture.hardgrab_detected",
        )]
        for before_reply in (False, True):
            with self.subTest(before_reply=before_reply):
                reply = json.dumps({"request_id": "req-0001-system-ping", "code": 0})
                wire = [json.dumps(event) for event in events]
                conn = FakeTextConnection(wire + [reply] if before_reply else [reply] + wire)
                channel = demo.CommandChannel(conn)
                channel.request("system.ping")
                self.assertEqual([channel.next_event(timeout=1) for _ in events], events)

    def test_listener_outputs_all_capture_statuses_and_only_saves_completed(self):
        app = self.make_event_app(output_dir=Path.cwd())
        app.command.next_event.side_effect = [
            {"event": "capture.hardgrab_detected", "payload": {"device_id": "ET24", "accepted": False,
                "code": 1110, "warning": "quota exhausted", "capture": {"payload": "SECRET_IMAGE_DATA"}}},
            {"event": "capture.started", "payload": {"task_id": "cap-1", "status": "running",
                "acquisition_status": "capturing", "processing_status": "queued"}},
            {"event": "capture.stage.updated", "payload": {"task_id": "cap-1", "status": "running",
                "stage": {"name": "color_processed", "status": "succeeded", "message": "done"}}},
            {"event": "capture.failed", "payload": {"task_id": "cap-failed", "status": "failed",
                "error": "device disconnected"}},
            {"event": "capture.completed", "payload": {"task_id": "cap-1", "status": "succeeded",
                "assets": [{"kind": "final", "asset_id": "final"}]}},
            KeyboardInterrupt(),
        ]
        with mock.patch.object(app, "_download_asset", return_value="/tmp/saved.jpg") as download, \
                mock.patch("sys.stdout", new=io.StringIO()) as stdout, \
                mock.patch("sys.stderr", new=io.StringIO()):
            with self.assertRaises(KeyboardInterrupt):
                app._listen_for_captures()
        output = stdout.getvalue()
        for name in ("capture.started", "capture.stage.updated", "capture.completed", "capture.failed", "capture.hardgrab_detected"):
            self.assertIn(name, output)
        for value in ("capturing", "color_processed", "succeeded", "device disconnected", "quota exhausted"):
            self.assertIn(value, output)
        self.assertIn('"accepted": false', output)
        self.assertNotIn("SECRET_IMAGE_DATA", output)
        download.assert_called_once()

    def test_single_capture_waits_through_nonterminal_events(self):
        app = self.make_event_app(output_dir=Path.cwd())
        app.command.next_event.side_effect = [
            {"event": "capture.hardgrab_detected", "payload": {"device_id": "ET24"}},
            {"event": "capture.started", "payload": {"task_id": "cap-1", "status": "running"}},
            {"event": "capture.stage.updated", "payload": {"task_id": "cap-1", "stage": {"name": "final", "status": "running"}}},
            {"event": "capture.completed", "payload": {"task_id": "cap-1", "status": "succeeded", "assets": [{"kind": "final"}]}},
        ]
        with mock.patch.object(app, "_download_asset", return_value="/tmp/saved.jpg") as download, \
                mock.patch("sys.stdout", new=io.StringIO()) as stdout:
            result = app._wait_for_capture("cap-1")
        self.assertEqual(result[2], "/tmp/saved.jpg")
        self.assertIn("capture.stage.updated", stdout.getvalue())
        self.assertEqual(app.command.next_event.call_count, 4)
        download.assert_called_once()

    def test_capture_count_interval_cli_defaults_and_invalid_values(self):
        parser = demo.build_arg_parser()
        args = parser.parse_args(["--capture"])
        self.assertEqual(args.capture_count, 1)
        self.assertEqual(args.capture_interval, 5.0)
        args = parser.parse_args(["--capture", "--capture-count", "3", "--capture-interval", "0.5"])
        self.assertEqual((args.capture_count, args.capture_interval), (3, 0.5))
        for flag, values in (("--capture-count", ("0", "-1", "1.5", "abc")),
                             ("--capture-interval", ("-1", "nan", "inf", "abc"))):
            for value in values:
                with self.subTest(flag=flag, value=value), mock.patch("sys.stderr", new=io.StringIO()):
                    with self.assertRaises(SystemExit):
                        parser.parse_args([flag, value])
        self.assertEqual(parser.parse_args(["--capture-interval", "0"]).capture_interval, 0)

    def prepare_batch_app(self, **kwargs):
        app = self.make_event_app(output_dir=Path.cwd(), capture=True, **kwargs)
        app._list_devices = mock.Mock(return_value=[{"device_id": "dev", "model": "ET24"}])
        app._open_device = mock.Mock()
        app._start_video = mock.Mock()
        app._cleanup = mock.Mock()
        return app

    def test_multiple_captures_are_serial_with_no_first_or_last_delay(self):
        app = self.prepare_batch_app(capture_count=3, capture_interval=2.5)
        actions = []
        def capture():
            actions.append("capture")
            return {"task_id": str(len(actions))}, {"kind": "final"}, "/tmp/photo.jpg"
        app._capture_and_download = mock.Mock(side_effect=capture)
        app._wait_capture_interval = mock.Mock(side_effect=lambda: actions.append("wait"))
        with mock.patch("sys.stdout", new=io.StringIO()) as stdout:
            result = app.run()
        self.assertEqual(actions, ["capture", "wait", "capture", "wait", "capture"])
        self.assertIn("3/3", stdout.getvalue())
        self.assertEqual(result.downloaded_path, "/tmp/photo.jpg")
        app._cleanup.assert_called_once()

    def test_default_capture_is_one_and_batch_stops_on_error_or_interrupt(self):
        app = self.prepare_batch_app()
        app._capture_and_download = mock.Mock(return_value=({}, {}, "/tmp/photo.jpg"))
        app._wait_capture_interval = mock.Mock()
        app.run()
        app._capture_and_download.assert_called_once()
        app._wait_capture_interval.assert_not_called()
        for error in (demo.SdkOpenError("capture failed"), KeyboardInterrupt()):
            with self.subTest(error=type(error).__name__):
                app = self.prepare_batch_app(capture_count=3)
                app._capture_and_download = mock.Mock(side_effect=error)
                app._wait_capture_interval = mock.Mock()
                with self.assertRaises(type(error)):
                    app.run()
                app._capture_and_download.assert_called_once()
                app._wait_capture_interval.assert_not_called()
                app._cleanup.assert_called_once()

    def test_capture_interval_keeps_receiving_events(self):
        app = self.prepare_batch_app(capture_count=2, capture_interval=5)
        event = {"event": "capture.stage.updated", "payload": {"task_id": "cap-1"}}
        app.command.next_event.side_effect = [event, None]
        with mock.patch.object(demo.time, "monotonic", side_effect=[10, 10, 11]), \
                mock.patch.object(app, "_save_capture_event") as handle:
            app._wait_capture_interval()
        handle.assert_called_once_with(event)
        self.assertEqual(app.command.next_event.call_args_list, [mock.call(timeout=5), mock.call(timeout=4)])
        app.capture_interval = 0
        app.command.reset_mock()
        app._wait_capture_interval()
        app.command.next_event.assert_not_called()

    def test_batch_captures_save_every_completed_task(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "source.jpg"
            source.write_bytes(b"photo")
            scripted = ScriptedCommand([
                ("device.list", {"code": 0, "data": {"devices": [{"device_id": "dev", "model": "ET24"}]}}),
                ("device.open", {"code": 0}),
                ("video.start", {"code": 0, "data": {"stream_id": "stream", "session_token": "ss-video"}}),
                *[("capture.take", {"code": 0, "data": {"task_id": f"cap-{i}"}}) for i in range(3)],
                ("video.stop", {"code": 0}), ("device.close", {"code": 0}), ("auth.destroy_session", {"code": 0}),
            ], events=[{"event": "capture.completed", "payload": {
                "task_id": f"cap-{i}", "status": "succeeded", "assets": [{"asset_id": "final", "kind": "final",
                "path": str(source), "content_type": "image/jpeg"}]}} for i in range(3)])
            output = Path(temp) / "saved"
            app = demo.DemoApp(
                command=scripted, command_session={"session_token": "ss-command"},
                command_url="ws://localhost:17090", video_url="ws://localhost:17091",
                output_dir=output, capture=True, capture_count=3, capture_interval=0, save_mode="copy",
                video_connection_factory=lambda *a: FakeVideoConnection([EOFError()]),
            )
            with mock.patch("sys.stdout", new=io.StringIO()):
                app.run()
            self.assertEqual(len(list(output.iterdir())), 3)
            self.assertTrue(all(path.read_bytes() == b"photo" for path in output.iterdir()))
            self.assertEqual([m for m, _ in scripted.requests].count("capture.take"), 3)
            self.assertEqual(scripted.scripted_responses, [])
            self.assertTrue(scripted.closed)

    def test_default_directory_is_cwd(self):
        args = demo.build_arg_parser().parse_args([])
        app = self.make_event_app(output_dir=Path(args.output_dir))
        self.assertEqual(app.output_dir, Path.cwd())

    def make_event_app(self, **kwargs):
        return demo.DemoApp(
            command=mock.Mock(), command_session={"session_token": "ss-test"},
            command_url="ws://localhost:17090", video_url="ws://localhost:17091",
            **kwargs,
        )

    def test_continuous_listener_saves_to_absolute_path_and_deduplicates(self):
        with tempfile.TemporaryDirectory() as temp:
            app = self.make_event_app(output_dir=Path(temp), downloader=lambda *a, **k: io.BytesIO(b"photo"))
            event = {"event": "capture.completed", "code": 0, "payload": {
                "task_id": "cap-1", "status": "succeeded", "assets": [{
                    "asset_id": "final", "kind": "final", "content_type": "image/jpeg",
                    "download_url": "http://example.test/api/assets/cap-1/final/download",
                }],
            }}
            app.command.next_event.side_effect = [event, event, KeyboardInterrupt()]
            with mock.patch("sys.stdout", new=io.StringIO()) as stdout:
                with self.assertRaises(KeyboardInterrupt):
                    app._listen_for_captures()
            saved = list(Path(temp).iterdir())
            self.assertEqual(len(saved), 1)
            self.assertEqual(saved[0].read_bytes(), b"photo")
            self.assertEqual(stdout.getvalue().count(str(saved[0].resolve())), 1)
            app.command.request.assert_not_called()

    def test_failed_event_and_timeout_do_not_download(self):
        app = self.make_event_app(output_dir=Path.cwd(), downloader=mock.Mock())
        app.command.next_event.return_value = {"event": "capture.failed", "payload": {
            "task_id": "cap-1", "status": "failed", "message": "capture error"}}
        with self.assertRaises(demo.SdkOpenError):
            app._wait_for_capture("cap-1")
        app.capture_timeout = 0
        with self.assertRaisesRegex(demo.SdkOpenError, "timeout"):
            app._wait_for_capture("cap-1")
        app.downloader.assert_not_called()

    def test_command_channel_matches_out_of_order_responses(self):
        response_2 = json.dumps({"request_id": "req-0002-beta", "code": 0, "data": {"ok": 2}})
        response_1 = json.dumps({"request_id": "req-0001-alpha", "code": 0, "data": {"ok": 1}})
        conn = FakeTextConnection([response_2, response_1])
        channel = demo.CommandChannel(conn)

        first = channel.request("alpha", {})
        second = channel.request("beta", {})

        self.assertEqual(first["request_id"], "req-0001-alpha")
        self.assertEqual(second["request_id"], "req-0002-beta")
        self.assertEqual(conn.recv_calls, 2)
        self.assertEqual(len(conn.sent), 2)

    def test_et_m_series_video_uses_fixed_dimensions(self):
        scripted = ScriptedCommand([
            ("video.start", {"code": 0, "data": {"session_token": "ss-v1", "stream_id": "stream-1"}}),
            ("video.stop", {"code": 0, "data": {"stopped": True}}),
            ("auth.destroy_session", {"code": 0, "message": "ok"}),
        ])
        fake_video = FakeVideoConnection([(0x2, b"frame"), EOFError()])
        created_urls = []

        def video_factory(url, timeout):
            created_urls.append((url, timeout))
            return fake_video

        app = demo.DemoApp(
            command=scripted,
            command_session={"session_token": "ss-cmd"},
            command_url="ws://127.0.0.1:17090",
            video_url="ws://127.0.0.1:17091",
            output_dir=Path(tempfile.mkdtemp()),
            video_connection_factory=video_factory,
        )
        app.device = {"device_id": "device-1", "model": "ET24", "display_name": "ET24"}
        app._start_video()
        self.assertEqual(len(created_urls), 1)
        self.assertIn("session_token=ss-v1", created_urls[0][0])
        self.assertIn("stream_id=stream-1", created_urls[0][0])
        self.assertEqual(scripted.requests[0][0], "video.start")
        self.assertEqual(scripted.requests[0][1]["width"], 1536)
        self.assertEqual(scripted.requests[0][1]["height"], 1152)
        self.assertEqual(scripted.requests[0][1]["fps"], 30)
        app._cleanup()

    def test_non_et_m_series_keeps_video_default_size(self):
        scripted = ScriptedCommand([
            ("video.start", {"code": 0, "data": {"session_token": "ss-v1", "stream_id": "stream-1"}}),
            ("video.stop", {"code": 0, "data": {"stopped": True}}),
            ("auth.destroy_session", {"code": 0, "message": "ok"}),
        ])
        fake_video = FakeVideoConnection([(0x2, b"frame"), EOFError()])

        app = demo.DemoApp(
            command=scripted,
            command_session={"session_token": "ss-cmd"},
            command_url="ws://127.0.0.1:17090",
            video_url="ws://127.0.0.1:17091",
            output_dir=Path(tempfile.mkdtemp()),
            video_connection_factory=lambda url, timeout: fake_video,
        )
        app.device = {"device_id": "device-2", "model": "A5", "display_name": "A5"}
        app._start_video()
        self.assertNotIn("width", scripted.requests[0][1])
        self.assertNotIn("height", scripted.requests[0][1])
        self.assertNotIn("fps", scripted.requests[0][1])
        app._cleanup()

    def test_run_skips_video_when_no_device(self):
        scripted = ScriptedCommand([
            ("device.list", {"code": 0, "data": {"devices": []}}),
            ("auth.destroy_session", {"code": 0, "message": "ok"}),
        ])
        called = {"video": False}

        def video_factory(*args, **kwargs):
            called["video"] = True
            raise AssertionError("video connection should not be created when no device exists")

        app = demo.DemoApp(
            command=scripted,
            command_session={"session_token": "ss-cmd"},
            command_url="ws://127.0.0.1:17090",
            video_url="ws://127.0.0.1:17091",
            output_dir=Path(tempfile.mkdtemp()),
            video_connection_factory=video_factory,
        )
        with mock.patch("sys.stdout", new=io.StringIO()) as stdout:
            with self.assertRaises(SystemExit) as cm:
                app.run()
        self.assertEqual(cm.exception.code, 1)
        self.assertIn("未检测到设备", stdout.getvalue())
        self.assertFalse(called["video"])
        self.assertEqual([req[0] for req in scripted.requests], ["device.list", "auth.destroy_session"])

    def test_capture_downloads_best_asset_atomically(self):
        tempdir = Path(tempfile.mkdtemp())
        scripted = ScriptedCommand([
            ("device.list", {"code": 0, "data": {"devices": [{"device_id": "device-1", "model": "ET24", "display_name": "ET24"}]}}),
            ("device.open", {"code": 0, "data": {"opened": True}}),
            ("video.start", {"code": 0, "data": {"session_token": "ss-video", "stream_id": "stream-1"}}),
            ("capture.take", {"code": 0, "data": {"task_id": "task-1"}}),
            ("video.stop", {"code": 0, "data": {"stopped": True}}),
            ("device.close", {"code": 0, "data": {"closed": True}}),
            ("auth.destroy_session", {"code": 0, "message": "ok"}),
        ])
        scripted.events = iter([{
            "event": "capture.completed", "code": 0, "payload": {
                "task_id": "task-1", "status": "succeeded", "assets": [
                    {"asset_id": "original", "kind": "original", "download_url": "http://example.test/original", "content_type": "image/jpeg"},
                    {"asset_id": "final", "kind": "final", "download_url": "http://example.test/final", "content_type": "image/jpeg"},
                ],
            },
        }])
        fake_video = FakeVideoConnection([(0x2, b"frame-1"), (0x1, json.dumps({"event": "video.ready", "payload": {"stream_id": "stream-1"}}).encode("utf-8")), EOFError()])
        download_requests = []

        class FakeDownloadResponse(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc, tb):
                self.close()
                return False

        def fake_downloader(request, timeout=30):
            download_requests.append((request.full_url, request.headers.get("Authorization"), timeout))
            return FakeDownloadResponse(b"captured-bytes")

        app = demo.DemoApp(
            command=scripted,
            command_session={"session_token": "ss-command"},
            command_url="ws://127.0.0.1:17090",
            video_url="ws://127.0.0.1:17091",
            output_dir=tempdir,
            capture=True,
            capture_poll_interval=0.0,
            capture_timeout=5.0,
            video_connection_factory=lambda url, timeout: fake_video,
            downloader=fake_downloader,
        )
        result = app.run()

        self.assertEqual(result.device["device_id"], "device-1")
        self.assertEqual(result.capture_task["status"], "succeeded")
        self.assertEqual(result.capture_asset["kind"], "final")
        self.assertTrue(result.downloaded_path)
        self.assertTrue(Path(result.downloaded_path).exists())
        self.assertEqual(Path(result.downloaded_path).read_bytes(), b"captured-bytes")
        self.assertEqual(download_requests[0][1], "Bearer ss-command")
        self.assertTrue(result.video_stats.binary_frames >= 1)
        self.assertEqual(result.video_stats.events[0]["event"], "video.ready")
        self.assertFalse(any(path.name.endswith(".part") for path in tempdir.iterdir()))
        self.assertEqual(
            [req[0] for req in scripted.requests],
            ["device.list", "device.open", "video.start", "capture.take", "video.stop", "device.close", "auth.destroy_session"],
        )


if __name__ == "__main__":
    unittest.main()
