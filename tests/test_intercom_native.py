#!/usr/bin/env python3
"""Actual companion, synthetic sockets/PCM only. Never opens a microphone."""
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]


def eventually(predicate, timeout=2):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(.01)
    raise AssertionError("bounded condition did not become true")


with tempfile.TemporaryDirectory(prefix="intercom-native-", dir=os.environ.get("TMPDIR", ROOT / "build")) as temp:
    d = Path(temp)
    alive = True
    status = dict(muted=False, do_not_disturb=False, output_available=True,
                  volume=50, notification_volume=50)
    cue_busy = False
    subscriptions = []
    notices = []
    speaker_pcm = bytearray()
    ack_enabled = True
    drain_allowed = threading.Event()
    drain_allowed.set()
    drain_paused = threading.Event()
    skip_bus_bytes = [0]
    config = d / "config.json"
    # A stale/reused PID must never kill or adopt an unrelated process.
    unrelated = subprocess.Popen(["sleep", "10"])
    try:
        (d / "stale.pid").write_text(str(unrelated.pid))
        init_env = dict(os.environ, DAEMON=str(d / "absent-daemon"),
                        PIDFILE=str(d / "stale.pid"), SOCKET=str(d / "absent.sock"))
        init = str(ROOT / "init/libreecho-intercomd.init")
        assert subprocess.run(["sh", init, "status"], env=init_env,
                              stdout=subprocess.DEVNULL, timeout=3).returncode == 1
        subprocess.run(["sh", init, "stop"], env=init_env, check=True,
                       stdout=subprocess.DEVNULL, timeout=3)
        assert unrelated.poll() is None
    finally:
        unrelated.terminate()
        unrelated.wait(timeout=2)
    config.write_text(json.dumps(dict(voice_pipeline_mode="home-assistant")))
    permission = Path(str(config) + ".intercom")
    permission.write_text('{"enabled":false}')
    (d / "privacy").write_text("0\n")
    os.mkfifo(d / "bus")
    reader = os.open(d / "bus", os.O_RDONLY | os.O_NONBLOCK)
    listeners = []

    def adapter(path, kind):
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(path))
        listener.listen(4)
        listener.settimeout(.1)
        listeners.append(listener)
        while alive:
            try:
                client, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with client:
                client.settimeout(1)
                raw = b""
                try:
                    while b"\n" not in raw:
                        part = client.recv(4096)
                        if not part:
                            break
                        raw += part
                    request = json.loads(raw)
                    cmd = request["cmd"]
                    data = status if cmd == "status" else {}
                    if kind == "audio" and cmd == "wake_chirp":
                        notices.append(cmd)
                        data = {"playing": False, "throttled": True} if cue_busy else {}
                    if kind == "led":
                        notices.append(request["args"])
                    if kind == "wake":
                        assert cmd == "stream_audio"
                        subscriptions.append(time.monotonic())
                        data = dict(streaming=True, format="pcm_s16_le", sample_rate=16000,
                                    channels=1, frame_header_bytes=24, sample_indexed=True)
                    client.sendall((json.dumps(dict(v=1, id=request["id"], ok=True, data=data), separators=(",", ":")) + "\n").encode())
                    if kind == "wake":
                        index = 1
                        while alive:
                            client.sendall(struct.pack("<4sHHQII", b"LVS1", 1, 0, index, 320, 0) + struct.pack("<320h", *([1234] * 320)))
                            index += 320
                            time.sleep(.02)
                except (OSError, ValueError):
                    pass

    def maintain():
        while alive:
            lease = d / "lease"
            if ack_enabled and lease.exists():
                try:
                    expiry = int(lease.read_text())
                    if expiry > time.monotonic() * 1000:
                        tmp = d / "ack.tmp"
                        tmp.write_text(str(int(time.monotonic() * 1000) + 2000))
                        tmp.chmod(0o600)
                        tmp.replace(d / "ack")
                except (ValueError, FileNotFoundError):
                    pass
            if not drain_allowed.is_set():
                drain_paused.set()
                time.sleep(.002)
                continue
            drain_paused.clear()
            try:
                pcm = os.read(reader, 8192)
                skipped = min(len(pcm), skip_bus_bytes[0])
                skip_bus_bytes[0] -= skipped
                pcm = pcm[skipped:]
                if len(speaker_pcm) < 65536:
                    speaker_pcm.extend(pcm)
            except BlockingIOError:
                pass
            time.sleep(.002)

    threads = [threading.Thread(target=adapter, args=(d / name, name), daemon=True)
               for name in ("audio", "led", "wake")]
    threads.append(threading.Thread(target=maintain, daemon=True))
    for thread in threads:
        thread.start()
    eventually(lambda: all((d / name).exists() for name in ("audio", "led", "wake")))
    proc = subprocess.Popen([str(ROOT / "build/libreecho-intercomd"), "--socket", str(d / "call"),
                             "--config", str(config), "--privacy-state", str(d / "privacy"),
                             "--audio-socket", str(d / "audio"), "--led-socket", str(d / "led"),
                             "--wake-socket", str(d / "wake"), "--audio-bus", str(d / "bus"),
                             "--lease", str(d / "lease"), "--assist-ack", str(d / "ack")],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        eventually(lambda: (d / "call").exists())

        def connect():
            client = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            client.settimeout(2)
            client.connect(str(d / "call"))
            client.send(b"LEIC1")
            return client

        def refused():
            before = len(subscriptions)
            with connect() as client:
                assert client.recv(1024) == b""
            assert len(subscriptions) == before, "refused call subscribed to the microphone"
            eventually(lambda: not (d / "lease").exists())

        def hold_full_bus():
            # Keep ACK renewal active while the simulated output owner stops
            # reading. Fill to EAGAIN rather than assume a host pipe capacity.
            drain_allowed.clear()
            eventually(drain_paused.is_set)
            try:
                while os.read(reader, 8192):
                    pass
            except BlockingIOError:
                pass
            speaker_pcm.clear()
            filler = os.open(d / "bus", os.O_WRONLY | os.O_NONBLOCK)
            filled = 0
            try:
                while filled < 1024 * 1024:
                    try:
                        filled += os.write(filler, bytes(4096))
                    except BlockingIOError:
                        break
                else:
                    raise AssertionError("synthetic FIFO did not saturate within its bound")
            finally:
                os.close(filler)
            assert filled >= 4096
            skip_bus_bytes[0] = filled

        def stream_phone(client, duration, value, expect_close=False):
            # Send at real-time 20 ms cadence and drain room audio throughout;
            # backpressure must be isolated to the output FIFO, not the socket.
            started = time.monotonic()
            deadline, next_send = started + duration, started
            packet = struct.pack("<320h", *([value] * 320))
            while time.monotonic() < deadline:
                if select.select([client], [], [], .002)[0]:
                    mic = client.recv(1024)
                    if not mic:
                        assert expect_close, "output stall unexpectedly ended the call"
                        return time.monotonic() - started
                    assert len(mic) == 640 and struct.unpack("<h", mic[:2])[0] == 1234
                now = time.monotonic()
                if now >= next_send:
                    try:
                        client.send(packet)
                    except BrokenPipeError:
                        assert expect_close, "output stall unexpectedly closed the caller socket"
                        return time.monotonic() - started
                    next_send = now + .02
            assert not expect_close, "a permanently stalled sink did not end the call"

        refused()  # Default off.
        permission.write_text('{"enabled":true}')
        status["output_available"] = False
        refused()
        del status["output_available"]
        refused()
        status["output_available"] = True
        for level in ("volume", "notification_volume"):
            status[level] = 0
            refused()
            status[level] = 50
        status["do_not_disturb"] = True
        refused()
        status["do_not_disturb"] = False
        (d / "privacy").write_text("unknown\n")
        refused()
        (d / "privacy").write_text("0\n")
        cue_busy = True
        refused()  # A throttled cue must not silently open the room microphone.
        cue_busy = False
        client = connect()
        assert client.recv(1024) == b"READY"
        assert notices and subscriptions
        packet = struct.pack("<320h", *([9000] * 320))
        for _ in range(5):
            client.send(packet)
            mic = client.recv(1024)
            assert len(mic) == 640 and struct.unpack("<h", mic[:2])[0] == 1234
            time.sleep(.02)
        eventually(lambda: len(speaker_pcm) >= 3000)
        assert any(v for v in struct.unpack("<" + "h" * (len(speaker_pcm) // 2), speaker_pcm[:len(speaker_pcm) // 2 * 2]))
        status["muted"] = True
        # Drain already bounded synthetic packets until the privacy close.
        eventually(lambda: client.recv(1024) == b"", timeout=1)
        client.close()
        eventually(lambda: not (d / "lease").exists())
        status["muted"] = False
        hold_full_bus()
        client = connect()
        assert client.recv(1024) == b"READY"
        stream_phone(client, 1.45, 1000)
        stream_phone(client, .15, 12000)
        assert drain_paused.is_set() and int((d / "lease").read_text()) > time.monotonic() * 1000
        drain_allowed.set()
        stream_phone(client, .2, 14000)
        eventually(lambda: skip_bus_bytes[0] == 0 and len(speaker_pcm) >= 3840)
        assert struct.unpack("<h", speaker_pcm[:2])[0] > 10000, "sink replayed stale queued speech"
        client.close()
        eventually(lambda: not (d / "lease").exists())
        print("intercom native full FIFO held1.6s, current-audio recovery and cleanup: PASS")
        hold_full_bus()
        client = connect()
        assert client.recv(1024) == b"READY"
        closed_after = stream_phone(client, 3.3, 9000, expect_close=True)
        assert 2.4 <= closed_after < 3.3, "sink deadline did not bound continuous output starvation"
        client.close()
        eventually(lambda: not (d / "lease").exists())
        drain_allowed.set()
        eventually(lambda: skip_bus_bytes[0] == 0)
        print("intercom native sustained FIFO stall ends despite ongoing phone audio: PASS")
        ack_enabled = False
        (d / "ack").unlink(missing_ok=True)
        refused()  # Assist did not acknowledge suspension.
        ack_enabled = True
        client = connect()
        assert client.recv(1024) == b"READY"
        deadline = int((d / "lease").read_text())
        proc.send_signal(signal.SIGKILL)
        proc.wait(timeout=2)
        assert deadline - time.monotonic() * 1000 <= 2000
        assert deadline > time.monotonic() * 1000
        client.close()
        print("intercom native synthetic duplex, default-off, DND/privacy/mute/cue/Assist gates and crash lease: PASS")
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=3)
        alive = False
        for listener in listeners:
            listener.close()
        for thread in threads:
            thread.join(timeout=2)
        os.close(reader)
