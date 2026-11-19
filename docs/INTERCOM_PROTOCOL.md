# Intercom protocol v1

This is a source interface for the foreground LAN intercom candidate, not a
claim of compatibility with SIP, Wyoming, Sendspin source roles or ESPHome
live duplex audio. Audio is 16,000 Hz, mono, signed 16-bit little-endian PCM.
Every audio payload is exactly 320 samples / 640 bytes / 20 ms.

## HTTPS control plane

All routes use the normal `{ok,data,error}` envelope and require an actual
unexpired local-account bearer session over the server's real HTTPS relay.
The development unauthenticated mode and a static bearer token alone cannot
authorize intercom. Mutations additionally require the normal CSRF token and
an explicit Origin exactly equal to `https://` plus the request Host. The
generic insecure-LAN and allowed-Origin exceptions do not apply.

* `GET /api/v1/intercom` returns `enabled`, `busy`, `transport` and
  `foreground_only`. It never starts audio.
* `PUT /api/v1/intercom` takes exactly `{"enabled":true|false}`. Disabling ends
  any pending/active call and revokes device-local permission.
* `POST /api/v1/intercom/call` reserves the single call and returns `ticket`,
  opaque cancellation `id`, `expires_in_ms:30000` and fixed stream `path`.
  It never starts audio. DND, permission and HA voice mode are checked first.
* `DELETE /api/v1/intercom/call` takes exactly `{"id":"<64 hex characters>"}`.
  Only the originating login and current call id can cancel a live reservation
  or call. A delayed deletion for an earlier call cannot end a later call.

The random 256-bit ticket is bound to the current login and exact Origin.
Both session expiry/logout and current speaker gates are rechecked while the
call is active. Ticket/id values are never put in a URL or log. Clients keep
them in memory. A ticket expires after 30 seconds and is consumed once.

## WSS stream

Upgrade only the fixed `/api/v1/intercom/stream` path over HTTPS, with version
13, a valid 16-byte WebSocket key and the exact matching Origin. There are no
query parameters, cookies used as credentials, extensions or subprotocols.
The connection has five seconds to send its first masked text frame:
`{"ticket":"<ticket>"}`. No ping or audio is accepted before authentication.
An upgrade alone opens neither the companion nor a microphone subscription.

The server connects the local companion only after consuming the ticket.
After the companion acknowledges its gates/cue/Assist hold, the server sends
the text message `ready`. The caller must wait for this before sending audio.

Binary messages in either direction contain a big-endian unsigned 32-bit
sequence followed by 640 PCM bytes. Each direction starts at zero and increments
by one per message. Audio before `ready`, a wrong length or a sequence gap ends
the call. A caller sends text `ping` every two seconds; the server responds
`pong`. RFC6455 ping/pong is also accepted after authentication. Six seconds
without an authenticated caller heartbeat/audio, any close or transport error,
logout, revoked permission or the ten-minute limit ends the call.

Client frames must be masked, final and unfragmented. RSV bits, continuation,
extensions, 64-bit lengths, noncanonical short lengths, messages over 1024
bytes and invalid control lengths are rejected. Receive buffering is bounded
to two maximum frames, transmit buffering to four maximum frames, and each
event-loop pass has a fixed frame budget. No web thread or unbounded allocation
is introduced. A saturated queue closes the call rather than silently delaying
speech. The browser's capture worklet has two outstanding packet credits and
a six-packet playback ceiling.

## Native endpoint boundary

`/run/libreecho/intercom.sock` is a 0600 AF_UNIX `SOCK_SEQPACKET` listener under
the same system account as the web daemon. Only one connection can be active.
The first datagram is the five ASCII bytes `LEIC1`; it must arrive in two
seconds. The companion verifies device-local opt-in, HA voice mode, audiod
status/output/levels/mute/DND and literal unmuted physical privacy state.
An unavailable or malformed status fails closed.

The companion atomically publishes the expiring Assist lease and waits up to
1.5 seconds for an expiring esphomed acknowledgement. It then requires a
successful start cue and finite ring indication, opens only the existing
system PCM FIFO, and subscribes to waked's indexed post-AEC `LVS1` stream.
After the stream acknowledgement it sends the five bytes `READY`. Subsequent
datagrams in both directions are exactly 640 PCM bytes. No sequence header is
needed inside the ordered Unix connection. Oversized datagrams fail closed.

Phone-to-room PCM is converted by the existing streaming resampler to the
48 kHz stereo system bus. A single bounded pending output block is allowed.
Room-to-phone PCM validates indexed frames and packs them into 20 ms packets.
Missing samples, overrun, lost acknowledgement, three seconds without incoming
phone PCM, EOF, a device gate change or normal process termination removes the lease,
stops only the intercom ring owner, closes both audio paths and drops queued
call bytes. An abrupt kill leaves only its expiring lease. The finite ring expires even if the companion is killed.

Test overrides select private Unix/FIFO/config/privacy/lease paths. Production
startup uses fixed local defaults. Extending this interface to another board
requires equivalent physical privacy and output ownership guarantees; the
local socket must not be exposed on the network as a shortcut around WSS auth.

The HA-owned watchdog probes the companion PID file, because the audio-only
seqpacket endpoint has no JSON status command. Its existing host-fixture
`--service` syntax accepts `NAME:pidfile=PATH:INIT::home-assistant` for an
explicit PID probe. This permits real companion recovery tests without opening
physical devices or making a health probe compete with a call.
