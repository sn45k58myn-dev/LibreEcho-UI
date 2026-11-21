"""Run with python3 tests/test_wake_trial_duration.py on a Linux build host."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CaptureDuration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="wake-trial-test-")
        cls.addClassCleanup(temp.cleanup)
        work = Path(temp.name)
        cls.socket_path = work / "wake.sock"
        cls.binary = work / "trial"
        # Use the real production worker and its existing deterministic test engine.
        # Only the socket destination and the model-file preflight are replaced.
        socket_header = work / "socket.h"
        socket_header.write_text('#include "adapter/adapter.h"\n#undef LE_ADAPTER_WAKEWORD_SOCK\n'
                                 f'#define LE_ADAPTER_WAKEWORD_SOCK "{cls.socket_path}"\n')
        stub = work / "model.c"
        stub.write_text('#include "wake_model.h"\n'
                        'int __wrap_le_wake_model_validate_directory(const char *p, char *r, size_t n) '
                        '{ (void)p; (void)r; (void)n; return 0; }\n')
        cc = [os.environ.get("CC", "cc"), "-std=c99", "-O2", "-D_POSIX_C_SOURCE=200809L",
              "-Isrc", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
        engine = work / "engine.o"
        subprocess.run(cc + ["-Dmain=decode_test_main", "-c", "tests/test_wake_decode.c", "-o", engine],
                       cwd=ROOT, check=True)
        subprocess.run(cc + ["-include", socket_header, "tools/wake_trial.c", "src/adapter/wake_worker.c",
                             "src/adapter/voice_dsp.c", "src/adapter/voice_stream.c", "src/wake_model.c",
                             "src/json.c", stub, engine, "-Wl,--wrap=le_wake_model_validate_directory",
                             "-lpthread", "-lm", "-o", cls.binary], cwd=ROOT, check=True)

    def trial(self, interval, count):
        with socket.socket(socket.AF_UNIX) as server:
            self.socket_path.unlink(missing_ok=True)
            server.bind(str(self.socket_path))
            server.listen(1)
            server.settimeout(3)
            with subprocess.Popen([self.binary, "/fixture", "5", "music", "0.53", "16"],
                                  stdout=subprocess.PIPE, text=True) as proc:
                with server.accept()[0] as stream:
                    self.assertEqual(stream.makefile("rb", buffering=0).readline(),
                                     b'{"v":1,"id":1,"cmd":"stream_audio","args":{}}\n')
                    stream.sendall(b'{"v":1,"id":1,"ok":true}\n')
                    start = time.monotonic()
                    for i in range(count):
                        time.sleep(max(0, start + interval * i - time.monotonic()))
                        try:
                            stream.sendall(struct.pack("<4sHHQII", b"LVS1", 1, 0, 160*i, 160, 0)
                                           + bytes(320))
                        except (BrokenPipeError, ConnectionResetError):
                            break
                out, _ = proc.communicate(timeout=3)
                return proc.returncode, out

    def test_waits_for_the_last_frames_after_nominal_wall_duration(self):
        # Only ~490 contiguous 10 ms frames by wall second five. The entire requested
        # 500 frames arrive a fraction later. Do not count the extra frames offered.
        code, out = self.trial(0.0102, 510)
        self.assertEqual(code, 0, out)
        self.assertIn("valid=1 reason=complete frames=500 ", out)

    def test_incomplete_stream_fails(self):
        code, out = self.trial(0.010, 80)
        self.assertNotEqual(code, 0, out)
        self.assertIn("valid=0 reason=stream_closed frames=80 ", out)

    def test_slow_continuous_stream_hits_absolute_watchdog(self):
        code, out = self.trial(0.020, 410)
        self.assertNotEqual(code, 0, out)
        self.assertIn("valid=0 reason=deadline", out)


if __name__ == "__main__":
    unittest.main()
