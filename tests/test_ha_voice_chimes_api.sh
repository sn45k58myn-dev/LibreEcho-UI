#!/bin/sh
# #540: real HTTP API, persisted opt-in, invalid-body atomicity and restart.
set -eu
PORT=${LIBREECHO_CHIMES_TEST_PORT:-18154}
URL="http://127.0.0.1:$PORT"
dir=$(mktemp -d)
cleanup(){ [ -z "${pid:-}" ] || { kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; }; rm -rf "$dir"; }
trap cleanup EXIT INT TERM
start(){
 ./build/libreecho-web --backend mock --config "$dir/config.json" --web-root ./web --listen "127.0.0.1:$PORT" --seed 42 >"$dir/server.log" 2>&1 &
 pid=$!; i=0
 while ! curl -fsS "$URL/api/v1/status" >/dev/null 2>&1; do i=$((i+1)); [ "$i" -lt 50 ] || { cat "$dir/server.log"; exit 1; }; sleep 0.1; done
 CSRF="X-LibreEcho-CSRF: $(curl -fsS "$URL/api/v1/config" | jq -r '.data.csrf_token')"
}
put(){ curl -fsS -X PUT "$URL/api/v1/voice-pipeline" -H "$CSRF" -H 'Content-Type: application/json' --data "$1"; }
expect(){ want=$1; shift; code=$(curl -sS -o "$dir/out" -w '%{http_code}' "$@"); [ "$code" = "$want" ] || { cat "$dir/out"; echo "expected $want got $code"; exit 1; }; }
start
curl -fsS "$URL/api/v1/voice-pipeline" | jq -e '.data.home_assistant.chimes == false' >/dev/null
expect 403 -X PUT "$URL/api/v1/voice-pipeline" -H 'Content-Type: application/json' --data '{"home_assistant_chimes":true}'
put '{"home_assistant_chimes":true}' | jq -e '.data.home_assistant.chimes == true and .data.mode == "local"' >/dev/null
jq -e '.esphome_voice_chimes == true' "$dir/config.json" >/dev/null
curl -fsS "$URL/api/v1/config/export" | jq '.data' > "$dir/export.json"
jq -e '.esphome_voice_chimes == true' "$dir/export.json" >/dev/null
before=$(cksum < "$dir/config.json")
for body in '{"home_assistant_chimes":"true"}' '{"home_assistant_chimes":1}' '{"home_assistant_chimes":null}' '{"home_assistant_chimes":true,"home_assistant_chimes":false}' '{"mode":"home-assistant","home_assistant_chimes":[]}'; do
 expect 400 -X PUT "$URL/api/v1/voice-pipeline" -H "$CSRF" -H 'Content-Type: application/json' --data "$body"
 [ "$(cksum < "$dir/config.json")" = "$before" ]
done
# Config import validates this persisted flag before changing anything too.
jq '.esphome_voice_chimes = "true"' "$dir/export.json" > "$dir/bad-import.json"
expect 400 -X POST "$URL/api/v1/config/import" -H "$CSRF" -H 'Content-Type: application/json' --data-binary "@$dir/bad-import.json"
[ "$(cksum < "$dir/config.json")" = "$before" ]
put '{"home_assistant_chimes":false}' >/dev/null
expect 200 -X POST "$URL/api/v1/config/import" -H "$CSRF" -H 'Content-Type: application/json' --data-binary "@$dir/export.json"
curl -fsS "$URL/api/v1/voice-pipeline" | jq -e '.data.home_assistant.chimes == true' >/dev/null
# Unrelated saves and mode toggles retain the owner preference.
curl -fsS -X PUT "$URL/api/v1/network" -H "$CSRF" -H 'Content-Type: application/json' --data '{"discoverable":true}' >/dev/null
put '{"mode":"home-assistant"}' | jq -e '.data.home_assistant.chimes == true' >/dev/null
put '{"mode":"local"}' | jq -e '.data.home_assistant.chimes == true' >/dev/null
kill "$pid"; wait "$pid" || true; pid=''; start
curl -fsS "$URL/api/v1/voice-pipeline" | jq -e '.data.home_assistant.chimes == true' >/dev/null
put '{"home_assistant_chimes":false}' | jq -e '.data.home_assistant.chimes == false' >/dev/null
jq -e '.esphome_voice_chimes == false' "$dir/config.json" >/dev/null
echo 'HA listening chimes HTTP: default off, CSRF, strict validation, persistence and restart PASS'
