#!/usr/bin/env python3
"""Real intercomd + watchdog + shipped init; idle, crash recovery and mode exit."""
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def wait_for(predicate, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            value = predicate()
            if value:
                return value
        except (OSError, ValueError):
            pass
        time.sleep(.05)
    raise AssertionError("intercom lifecycle condition timed out")


with tempfile.TemporaryDirectory(prefix="intercom-watchdog-", dir=os.environ.get("TMPDIR")) as temp:
    d = Path(temp)
    daemon = d / "intercomd"
    shutil.copy2(ROOT / "build/libreecho-intercomd", daemon)
    daemon.chmod(0o700)
    cfg, pidfile, sock = d / "config", d / "intercom.pid", d / "intercom.sock"
    lease, ack, actions = d / "lease", d / "ack", d / "actions"
    cfg.write_text(json.dumps(dict(voice_pipeline_mode="home-assistant", integrations=1)))
    launcher = d / "intercom.init"
    args = ["--socket", str(sock), "--config", str(cfg), "--lease", str(lease), "--assist-ack", str(ack),
            "--privacy-state", str(d / "no-physical-device"), "--audio-socket", str(d / "absent-audio"),
            "--wake-socket", str(d / "absent-wake"), "--led-socket", str(d / "absent-led")]
    launcher.write_text("#!/bin/sh\n" +
                        'printf "%s\\n" "$1" >> ' + shlex.quote(str(actions)) + "\n" +
                        "DAEMON=" + shlex.quote(str(daemon)) + " PIDFILE=" + shlex.quote(str(pidfile)) +
                        " SOCKET=" + shlex.quote(str(sock)) + " LOGFILE=" + shlex.quote(str(d / "intercom.log")) +
                        " CONFIG=" + shlex.quote(str(cfg)) + " ARGS=" + shlex.quote(shlex.join(args)) +
                        " exec sh " + shlex.quote(str(ROOT / "init/libreecho-intercomd.init")) + ' "$1"\n')
    launcher.chmod(0o700)
    passive = d / "local.init"
    passive.write_text("#!/bin/sh\nexit 0\n")
    passive.chmod(0o700)
    wd = None
    log = (d / "watchdog.log").open("w")
    try:
        subprocess.run([str(launcher), "start"], check=True, timeout=5, stdout=subprocess.DEVNULL)
        original = wait_for(lambda: int(pidfile.read_text()) if sock.exists() else None)
        # Permission is missing/off: idle recovery must not create opt-in or
        # need a physical microphone/audio/LED device.
        assert not Path(str(cfg) + ".intercom").exists()
        wd = subprocess.Popen([str(ROOT / "build/libreecho-watchdogd"), "--interval", "1", "--passes", "30",
                               "--rearm-grace", "0", "--config", str(cfg), "--service",
                               f"intercomd:pidfile={pidfile}:{launcher}::home-assistant"], stdout=log, stderr=log)
        time.sleep(1.5)
        lease.write_text(str(int(time.monotonic() * 1000) + 2000))
        lease.chmod(0o600)
        os.kill(original, signal.SIGKILL)
        recovered = wait_for(lambda: int(pidfile.read_text()) if int(pidfile.read_text()) != original and sock.exists() else None)
        assert recovered != original
        wait_for(lambda: not lease.exists())
        assert actions.read_text().splitlines().count("start") == 2
        assert not Path(str(cfg) + ".intercom").exists()
        # The real API transition calls the shipped init stop, then local
        # owner starts. The watchdog must respect that saved selection.
        cfg.write_text(json.dumps(dict(voice_pipeline_mode="local", integrations=0)))
        subprocess.run([str(ROOT / "build/test-intercom-mode-exit"), str(launcher), str(passive)], check=True, timeout=10)
        wait_for(lambda: not sock.exists() and not pidfile.exists())
        before = actions.read_text().splitlines().count("start")
        time.sleep(4)
        assert actions.read_text().splitlines().count("start") == before
        assert not lease.exists() and not ack.exists()
        print("intercom actual watchdog/native/init crash recovery, default-off and API mode-exit ownership: PASS")
    finally:
        if wd and wd.poll() is None:
            wd.terminate()
            wd.wait(timeout=5)
        subprocess.run([str(launcher), "stop"], timeout=8, stdout=subprocess.DEVNULL, check=False)
        log.close()
