#!/bin/sh
set -eu
mkdir -p build
python3 tests/test_intercom_link.py
fixture=$(mktemp -d "${TMPDIR:-$PWD/build}/intercom-XXXXXX")
trap 'rm -rf "$fixture"' EXIT INT TERM
${CC:-cc} -D_POSIX_C_SOURCE=200809L -std=c99 -Wall -Wextra -Wpedantic -Werror -Isrc -Isrc/adapter \
    tests/test_intercom.c src/intercom_protocol.c src/adapter/ws_client.c src/adapter/live_b64.c \
    src/auth.c src/config_store.c src/json.c -o "$fixture/test"
"$fixture/test" "$fixture"
make build/libreecho-intercomd
python3 tests/test_intercom_native.py
python3 tests/test_intercom_config.py
node tests/test_intercom_browser.js
make build/libreecho-watchdogd build/test-intercom-mode-exit
python3 tests/test_intercom_watchdog.py
if [ "${LIBREECHO_INTERCOM_HTTPS_TEST:-0}" = 1 ]; then
    python3 tests/test_intercom_https.py
fi
