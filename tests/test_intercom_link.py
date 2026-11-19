#!/usr/bin/env python3
"""The native companion must inherit the release linker's static/GC flags."""
import shlex
import subprocess

result = subprocess.run(["make", "-B", "-n", "build/libreecho-intercomd",
                         "LDFLAGS=-static -Wl,--gc-sections"],
                        check=True, capture_output=True, text=True)
links = [shlex.split(line) for line in result.stdout.splitlines()
         if " -o build/libreecho-intercomd " in line]
assert len(links) == 1, links
assert "-static" in links[0] and "-Wl,--gc-sections" in links[0], links[0]
print("intercom native release link flags: PASS")
