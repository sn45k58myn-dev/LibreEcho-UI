#!/usr/bin/env python3
"""Real HTTPS relay/WSS, actual login, synthetic local endpoint; no hardware."""
import base64
import http.client
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
if os.environ.get("ESPHOMED_PRIVATE_NAMESPACE") != "1" or list(Path("/sys").iterdir()):
    raise SystemExit("Run this test in a private container/namespace with empty /sys and private /run")


def port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def exact(s, count):
    out = b""
    while len(out) < count:
        part = s.recv(count - len(out))
        if not part:
            raise AssertionError("unexpected end of secure stream")
        out += part
    return out


def frame(s, payload, opcode=1):
    mask = b"test"
    header = bytes([128 | opcode, 128 | (len(payload) if len(payload) < 126 else 126)])
    if len(payload) >= 126:
        header += struct.pack("!H", len(payload))
    s.sendall(header + mask + bytes(v ^ mask[i % 4] for i, v in enumerate(payload)))


def read_frame(s):
    h = exact(s, 2)
    assert not h[1] & 128
    n = h[1] & 127
    if n == 126:
        n = struct.unpack("!H", exact(s, 2))[0]
    assert n <= 1024
    return h[0] & 15, exact(s, n)


with tempfile.TemporaryDirectory(prefix="intercom-https-", dir=os.environ.get("TMPDIR", "/tmp")) as tmp:
    d = Path(tmp)
    cfg = d / "config.json"
    cfg.write_text(json.dumps(dict(voice_pipeline_mode="home-assistant", integrations=1)))
    with (d / "users").open("w") as users:
        subprocess.run(["sh", str(ROOT / "tools/create-user.sh"), "caller", "test-password-only"],
                       stdout=users, check=True, timeout=5)
    (d / "users").chmod(0o600)
    plain, secure = port(), port()
    origin = f"https://127.0.0.1:{secure}"
    context = ssl._create_unverified_context()  # Private generated fixture cert, not Safari qualification.
    log = (d / "web.log").open("w")
    proc = subprocess.Popen([str(ROOT / "build/libreecho-web"), "--backend", "mock",
                             "--config", str(cfg), "--users-file", str(d / "users"),
                             "--web-root", str(ROOT / "web"), "--listen", f"127.0.0.1:{plain}",
                             "--tls-port", str(secure), "--tls-dir", str(d / "tls")],
                            stdout=log, stderr=log)
    token = csrf = ""
    listener = peer = ws = None

    def api(path, method="GET", body=None, transport=True, extra=None):
        conn = http.client.HTTPSConnection("127.0.0.1", secure, context=context, timeout=3) if transport else http.client.HTTPConnection("127.0.0.1", plain, timeout=3)
        headers = dict(Accept="application/json")
        if token:
            headers["Authorization"] = "Bearer " + token
        if method != "GET":
            headers.update({"Origin": origin, "X-LibreEcho-CSRF": csrf, "Content-Type": "application/json"})
        if extra:
            for k, v in extra.items():
                if v is None:
                    headers.pop(k, None)
                else:
                    headers[k] = v
        conn.request(method, "/api/v1" + path, json.dumps(body) if body is not None else None, headers)
        r = conn.getresponse()
        status, data = r.status, json.loads(r.read())
        conn.close()
        return status, data

    def upgrade(request_origin):
        connection = context.wrap_socket(socket.create_connection(("127.0.0.1", secure), timeout=3), server_hostname="localhost")
        key = base64.b64encode(b"synthetic-key123").decode()
        # 16-byte key; no credentials in the request target or handshake.
        connection.sendall((f"GET /api/v1/intercom/stream HTTP/1.1\r\nHost: 127.0.0.1:{secure}\r\n"
                            f"Origin: {request_origin}\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
                            f"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n\r\n").encode())
        head = b""
        while not head.endswith(b"\r\n\r\n"):
            head += exact(connection, 1)
        return connection, head

    try:
        deadline = time.monotonic() + 8
        while True:
            try:
                code, data = api("/config")
                assert code == 200
                csrf = data["data"]["csrf_token"]
                break
            except (OSError, http.client.HTTPException):
                if proc.poll() is not None or time.monotonic() > deadline:
                    raise
                time.sleep(.05)
        code, data = api("/auth/login", "POST", dict(username="caller", password="test-password-only"))
        assert code == 200
        token = data["data"]["token"]
        assert api("/intercom", transport=False, extra={"X-Forwarded-Proto": "https"})[0] == 403
        assert api("/intercom", "PUT", dict(enabled=True), extra={"Origin": None})[0] == 403
        assert api("/intercom", "PUT", dict(enabled=True), extra={"X-LibreEcho-CSRF": "wrong"})[0] == 403
        assert api("/intercom", "PUT", dict(enabled=True))[0] == 200
        code, data = api("/intercom/call", "POST", {})
        assert code == 201, data
        ticket = data["data"]["ticket"]
        bad, head = upgrade("https://another.test")
        assert head.startswith(b"HTTP/1.1 403")
        bad.close()
        endpoint = Path("/run/libreecho/intercom.sock")
        endpoint.parent.mkdir(parents=True, exist_ok=True)
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        listener.settimeout(3)
        listener.bind(str(endpoint))
        listener.listen(1)
        ws, head = upgrade(origin)
        assert head.startswith(b"HTTP/1.1 101"), head
        frame(ws, json.dumps(dict(ticket=ticket)).encode())
        peer, _ = listener.accept()
        peer.settimeout(3)
        assert peer.recv(1024) == b"LEIC1"
        peer.send(b"READY")
        assert read_frame(ws) == (1, b"ready")
        pcm = struct.pack("<320h", *([123] * 320))
        frame(ws, b"\0\0\0\0" + pcm, 2)
        assert peer.recv(1024) == pcm
        peer.send(pcm)
        assert read_frame(ws) == (2, b"\0\0\0\0" + pcm)
        assert api("/auth/logout", "POST", {})[0] == 200
        assert ws.recv(1024) == b""
        assert peer.recv(1024) == b""
        print("intercom actual HTTPS/WSS login, exact-origin/CSRF/transport gates, synthetic duplex and logout cleanup: PASS")
    finally:
        for client in (ws, peer, listener):
            if client:
                client.close()
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=3)
        log.close()
        Path("/run/libreecho/intercom.sock").unlink(missing_ok=True)
