#!/usr/bin/env python3
"""Verify the macOS SDK Open HTTPS and WSS runtime endpoints.

This script only performs read-only health/protocol checks. It does not create
an SDK authorization session or start capture/video operations.
"""

import argparse
import base64
import hashlib
import json
import os
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import time
import urllib.parse


WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
INSTALLED_ROOT_CA = "/Library/CZUR/sdk-open/tls/local-runtime-root-ca.crt"
MAX_HTTP_HEADER = 64 * 1024
MAX_FRAME_PAYLOAD = 8 * 1024 * 1024


class VerificationError(RuntimeError):
    pass


def build_ssl_context(ca_file):
    context = ssl.create_default_context()
    if ca_file:
        context.load_verify_locations(cafile=ca_file)
    elif os.path.isfile(INSTALLED_ROOT_CA):
        # Python distributions on macOS do not consistently consult the
        # System keychain. Load the installed root for the TLS handshake, then
        # independently prove native trust with `security verify-cert` below.
        context.load_verify_locations(cafile=INSTALLED_ROOT_CA)
    return context


def verify_macos_native_trust(tls_socket, host, timeout):
    der_certificate = tls_socket.getpeercert(binary_form=True)
    if not der_certificate:
        raise VerificationError("TLS peer did not provide a certificate")
    pem_certificate = ssl.DER_cert_to_PEM_cert(der_certificate)
    with tempfile.NamedTemporaryFile(mode="w", suffix=".pem") as certificate_file:
        certificate_file.write(pem_certificate)
        certificate_file.flush()
        result = subprocess.run(
            [
                "/usr/bin/security",
                "verify-cert",
                "-c",
                certificate_file.name,
                "-p",
                "ssl",
                "-s",
                host,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            timeout=timeout,
        )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        if detail:
            raise VerificationError("certificate is not trusted by the macOS keychain: " + detail)
        raise VerificationError("certificate is not trusted by the macOS keychain")


def tls_connect(context, host, port, timeout, verify_native_trust):
    raw_socket = socket.create_connection((host, port), timeout=timeout)
    tls_socket = None
    try:
        tls_socket = context.wrap_socket(raw_socket, server_hostname=host)
        tls_socket.settimeout(timeout)
        if verify_native_trust:
            verify_macos_native_trust(tls_socket, host, timeout)
        return tls_socket
    except Exception:
        if tls_socket is not None:
            tls_socket.close()
        else:
            raw_socket.close()
        raise


def recv_until(sock, marker, initial=b"", limit=MAX_HTTP_HEADER):
    data = initial
    while marker not in data:
        if len(data) >= limit:
            raise VerificationError("HTTP response headers exceed the size limit")
        chunk = sock.recv(4096)
        if not chunk:
            raise VerificationError("connection closed before HTTP headers completed")
        data += chunk
    before, after = data.split(marker, 1)
    return before, after


def parse_http_response(header_bytes):
    try:
        lines = header_bytes.decode("iso-8859-1").split("\r\n")
        status_parts = lines[0].split(" ", 2)
        status = int(status_parts[1])
    except (IndexError, ValueError) as exc:
        raise VerificationError("invalid HTTP response status line") from exc

    headers = {}
    for line in lines[1:]:
        if not line:
            continue
        if ":" not in line:
            raise VerificationError("invalid HTTP response header")
        name, value = line.split(":", 1)
        headers[name.strip().lower()] = value.strip()
    return status, headers


def verify_https(context, host, port, timeout, verify_native_trust):
    sock = tls_connect(context, host, port, timeout, verify_native_trust)
    try:
        request = (
            "GET /healthz HTTP/1.1\r\n"
            "Host: {}:{}\r\n"
            "Accept: application/json\r\n"
            "Connection: close\r\n\r\n"
        ).format(host, port)
        sock.sendall(request.encode("ascii"))
        header_bytes, _ = recv_until(sock, b"\r\n\r\n")
        status, _ = parse_http_response(header_bytes)
        if status != 200:
            raise VerificationError("HTTPS /healthz returned HTTP {} (expected 200)".format(status))
    finally:
        sock.close()


def header_has_token(headers, name, expected):
    return expected.lower() in [part.strip().lower() for part in headers.get(name, "").split(",")]


class WebSocketConnection:
    def __init__(self, sock, initial_data=b""):
        self.sock = sock
        self.buffer = initial_data

    def _recv_exact(self, size, deadline):
        while len(self.buffer) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise VerificationError("timed out while reading a WebSocket message")
            self.sock.settimeout(remaining)
            chunk = self.sock.recv(max(4096, size - len(self.buffer)))
            if not chunk:
                raise VerificationError("WebSocket connection closed unexpectedly")
            self.buffer += chunk
        value = self.buffer[:size]
        self.buffer = self.buffer[size:]
        return value

    def send_frame(self, opcode, payload=b""):
        mask_key = os.urandom(4)
        payload_length = len(payload)
        if payload_length < 126:
            header = struct.pack("!BB", 0x80 | opcode, 0x80 | payload_length)
        elif payload_length <= 0xFFFF:
            header = struct.pack("!BBH", 0x80 | opcode, 0x80 | 126, payload_length)
        else:
            header = struct.pack("!BBQ", 0x80 | opcode, 0x80 | 127, payload_length)
        masked = bytes(value ^ mask_key[index % 4] for index, value in enumerate(payload))
        self.sock.sendall(header + mask_key + masked)

    def send_text(self, value):
        self.send_frame(0x1, value.encode("utf-8"))

    def receive_message(self, deadline):
        message_opcode = None
        message_payload = bytearray()
        while True:
            first, second = struct.unpack("!BB", self._recv_exact(2, deadline))
            final = bool(first & 0x80)
            opcode = first & 0x0F
            masked = bool(second & 0x80)
            payload_length = second & 0x7F
            if payload_length == 126:
                payload_length = struct.unpack("!H", self._recv_exact(2, deadline))[0]
            elif payload_length == 127:
                payload_length = struct.unpack("!Q", self._recv_exact(8, deadline))[0]
            if payload_length > MAX_FRAME_PAYLOAD:
                raise VerificationError("WebSocket frame exceeds the size limit")
            mask_key = self._recv_exact(4, deadline) if masked else b""
            payload = self._recv_exact(payload_length, deadline)
            if masked:
                payload = bytes(value ^ mask_key[index % 4] for index, value in enumerate(payload))

            if opcode == 0x8:
                raise VerificationError("WebSocket server closed the connection")
            if opcode == 0x9:
                self.send_frame(0xA, payload)
                continue
            if opcode == 0xA:
                continue
            if opcode in (0x1, 0x2):
                if message_opcode is not None:
                    raise VerificationError("received a new WebSocket message during fragmentation")
                message_opcode = opcode
                message_payload.extend(payload)
            elif opcode == 0x0:
                if message_opcode is None:
                    raise VerificationError("received an unexpected WebSocket continuation frame")
                message_payload.extend(payload)
            else:
                raise VerificationError("received unsupported WebSocket opcode {}".format(opcode))

            if len(message_payload) > MAX_FRAME_PAYLOAD:
                raise VerificationError("WebSocket message exceeds the size limit")

            if final:
                return message_opcode, bytes(message_payload)

    def close(self):
        try:
            self.send_frame(0x8, struct.pack("!H", 1000))
        except (OSError, VerificationError):
            pass
        self.sock.close()


def websocket_handshake(context, host, port, path, timeout, expected_status, verify_native_trust):
    sock = tls_connect(context, host, port, timeout, verify_native_trust)
    websocket_key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        "GET {} HTTP/1.1\r\n"
        "Host: {}:{}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: {}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Origin: https://{}\r\n\r\n"
    ).format(path, host, port, websocket_key, host)
    try:
        sock.sendall(request.encode("ascii"))
        header_bytes, remaining = recv_until(sock, b"\r\n\r\n")
        status, headers = parse_http_response(header_bytes)
        if status != expected_status:
            raise VerificationError(
                "WSS port {} returned HTTP {} (expected {})".format(port, status, expected_status)
            )
        if expected_status != 101:
            sock.close()
            return None
        if not header_has_token(headers, "upgrade", "websocket"):
            raise VerificationError("WSS response is missing Upgrade: websocket")
        if not header_has_token(headers, "connection", "upgrade"):
            raise VerificationError("WSS response is missing Connection: Upgrade")
        expected_accept = base64.b64encode(
            hashlib.sha1((websocket_key + WEBSOCKET_GUID).encode("ascii")).digest()
        ).decode("ascii")
        if headers.get("sec-websocket-accept") != expected_accept:
            raise VerificationError("WSS response has an invalid Sec-WebSocket-Accept")
        return WebSocketConnection(sock, remaining)
    except Exception:
        sock.close()
        raise


def receive_json_until(ws, deadline, predicate, description):
    while time.monotonic() < deadline:
        ws.sock.settimeout(max(0.1, deadline - time.monotonic()))
        opcode, payload = ws.receive_message(deadline)
        if opcode != 0x1:
            continue
        try:
            message = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            continue
        if isinstance(message, dict) and predicate(message):
            return message
    raise VerificationError("timed out waiting for {}".format(description))


def verify_command_wss(context, host, port, timeout, verify_native_trust):
    ws = websocket_handshake(context, host, port, "/", timeout, 101, verify_native_trust)
    request_id = "verify-ping-" + os.urandom(8).hex()
    try:
        ws.send_text(json.dumps({"request_id": request_id, "method": "system.ping", "params": {}}))
        response = receive_json_until(
            ws,
            time.monotonic() + timeout,
            lambda value: value.get("request_id") == request_id,
            "the system.ping response",
        )
        if response.get("code") != 0:
            raise VerificationError("system.ping returned code {} (expected 0)".format(response.get("code")))
        if not isinstance(response.get("data"), dict) or response["data"].get("pong") is not True:
            raise VerificationError("system.ping response does not contain data.pong=true")
    finally:
        ws.close()


def verify_video_auth_rejection(context, host, port, timeout, verify_native_trust):
    websocket_handshake(context, host, port, "/", timeout, 401, verify_native_trust)


def verify_video_session(context, host, port, timeout, session_token, stream_id, verify_native_trust):
    query = urllib.parse.urlencode({"session_token": session_token, "stream_id": stream_id})
    ws = websocket_handshake(context, host, port, "/?" + query, timeout, 101, verify_native_trust)
    try:
        event = receive_json_until(
            ws,
            time.monotonic() + timeout,
            lambda value: value.get("event") == "video.ready",
            "the video.ready event",
        )
        if event.get("code") != 0:
            raise VerificationError("video.ready returned code {} (expected 0)".format(event.get("code")))
        payload = event.get("payload")
        if not isinstance(payload, dict) or payload.get("stream_id") != stream_id:
            raise VerificationError("video.ready returned an unexpected stream_id")
    finally:
        ws.close()


def parse_args():
    parser = argparse.ArgumentParser(description="Verify SDK Open macOS runtime HTTPS/WSS TLS endpoints")
    parser.add_argument("--host", default="sdk-runtime.localhost", help="runtime TLS hostname")
    parser.add_argument("--https-port", type=int, default=18082, help="HTTPS health port")
    parser.add_argument("--command-port", type=int, default=18090, help="command WSS port")
    parser.add_argument("--video-port", type=int, default=18091, help="video WSS port")
    parser.add_argument("--ca-file", help="extra CA certificate for diagnostics; system trust is used by default")
    parser.add_argument("--timeout", type=float, default=5.0, help="per-check timeout in seconds")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.timeout <= 0:
        print("FAIL: --timeout must be greater than zero", file=sys.stderr)
        return 2

    session_token = os.environ.get("SDK_VERIFY_SESSION_TOKEN", "")
    stream_id = os.environ.get("SDK_VERIFY_STREAM_ID", "")
    if bool(session_token) != bool(stream_id):
        print(
            "FAIL: SDK_VERIFY_SESSION_TOKEN and SDK_VERIFY_STREAM_ID must be set together",
            file=sys.stderr,
        )
        return 2

    verify_native_trust = not bool(args.ca_file)
    if args.ca_file:
        print("INFO: --ca-file is a diagnostic trust override; native macOS keychain trust is not checked")

    checks = [
        ("HTTPS /healthz", lambda: verify_https(
            build_ssl_context(args.ca_file), args.host, args.https_port, args.timeout, verify_native_trust
        )),
        ("command WSS system.ping", lambda: verify_command_wss(
            build_ssl_context(args.ca_file), args.host, args.command_port, args.timeout, verify_native_trust
        )),
        ("video WSS TLS + unauthenticated rejection (HTTP 401)", lambda: verify_video_auth_rejection(
            build_ssl_context(args.ca_file), args.host, args.video_port, args.timeout, verify_native_trust
        )),
    ]
    if session_token:
        checks.append(("video WSS authenticated video.ready", lambda: verify_video_session(
            build_ssl_context(args.ca_file),
            args.host,
            args.video_port,
            args.timeout,
            session_token,
            stream_id,
            verify_native_trust,
        )))

    failed = False
    for label, check in checks:
        try:
            check()
            print("PASS: " + label)
        except (OSError, ssl.SSLError, subprocess.TimeoutExpired, VerificationError) as exc:
            failed = True
            print("FAIL: {}: {}".format(label, exc), file=sys.stderr)

    if not session_token:
        print(
            "INFO: authenticated video WSS check skipped; set both SDK_VERIFY_SESSION_TOKEN "
            "and SDK_VERIFY_STREAM_ID to enable it"
        )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
