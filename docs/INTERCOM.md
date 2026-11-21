# Phone and room intercom

This source candidate adds an iPhone-initiated, two-way call to one native
LibreEcho speaker. It is intended for Safari in the foreground on the same
trusted LAN. It does not implement incoming iPhone calls, background audio,
push notifications, Internet traversal, automatic answering on other devices,
or intercom support in an existing ESPHome voice board.

Open the speaker's control centre over **trusted HTTPS**, sign in with a local
device account, open **Intercom**, and explicitly enable **Allow calls to this
speaker**. Calling is off by default. Every signed-in local account has this
permission; the current account model has no separate administrator/caller
roles. Disable the permission to revoke calling for all accounts. Permission
is device-local, stored beside the resolved persistent config as `web-config.json.intercom`, and is not exported with
portable settings or copied to paired speakers.

The phone needs a certificate it actually trusts for the speaker's exact host
name or address. Bypassing a self-signed certificate warning is insufficient
to promise microphone or AudioWorklet access in Safari. The existing System
certificate upload can install an owner-provided certificate and key. A
private CA must be explicitly trusted on the phone, and the leaf certificate
must have the appropriate SAN. This feature does not install a CA, distribute
keys, or change generic certificate generation. The page refuses to call when
its secure-origin/media APIs are unavailable.

Select Home Assistant as the speaker's voice pipeline. **Call room** is the
only action that requests the phone microphone. The speaker cancels the
current Assist turn, sounds a start cue and flashes its ring before opening
the room audio stream. Home Assistant voice capture and announcements stay
paused during the call. On hangup, they resume using their existing settings.
Other voice pipeline modes fail closed in this first implementation.

**Mute my microphone** affects only the phone's outgoing audio. The room's
physical privacy switch, microphone mute, Do Not Disturb, disabled calling,
missing audio output, zero speaker/notification level, or a failed start cue
prevent a call. Privacy, mute, DND and permission changes during a call end it;
the companion checks device gates at least every 250 ms, with bounded local
adapter timeouts. The feature never unmutes a microphone or changes volume.
Ending the call, leaving the page, putting Safari in the background or losing
the connection closes the phone tracks and speaker subscription. Calls have
a ten-minute maximum. A slow audio consumer ends the call instead of retaining
an expanding audio queue. No call audio is recorded by this feature.

## Runtime and packaging

The web daemon owns authentication and the WSS connection. The small C99
`libreecho-intercomd` companion owns the call's local audio subscriptions. It
uses the existing waked post-AEC stream and audiod system PCM bus; micd and
audiod remain the sole hardware PCM owners. Idle intercomd never subscribes to
the microphone. The HA-owned watchdog restores a crashed companion using its
PID file; it never sends status probes to the single-call audio socket. Voice
mode transitions stop the companion before changing owners and start it only
after HA readiness. Calling permission stays off unless explicitly enabled. It needs the existing audio, LED, wake and HA/esphomed runtime.

On the 2017 Echo 2 (`radar_puffin`), waked recognises the installed intercom
companion over its Unix socket and opens a separate echo canceller for that
call. The canceller delays its internal speaker reference by 30 ms and scales
quiet signals inside the filter to improve their precision. A loud frame
immediately reduces the internal gain. The ordinary wake and Assist streams
continue to use their existing filter. The private waked `status` response
reports `intercom_aec_active` while the call owns its filter. A failed filter
allocation falls back to ordinary processed audio, as do other board profiles.

The source Makefile installs `libreecho-intercomd` and
`libreecho-intercomd.init`. Platform image assembly must also include both in
its explicit binary/init whitelists, retain `intercom-ui.js` and
`intercom-worklet.js`, and activate the service with the HA runtime. Installing
source files alone does not prove an image contains the feature. No device is
configured, flashed or published by these changes.

The two `/run/libreecho` hold/ack markers contain a monotonic deadline, are
0600, and are atomically replaced. A marker more than 2.1 seconds in the future,
expired, malformed, insecurely accessible, owned by another user or a symlink
does not hold Assist. intercomd refreshes a two-second hold only while its call
is alive. esphomed acknowledges only after cancelling capture/playback. A
companion or esphomed crash cannot leave a permanent hold, and a call stops
when its acknowledgement expires. The normal init path runs these companions
under the same system account; a different account requires a separately
designed local permission boundary.

## Qualification

`sh tests/test_intercom.sh` exercises strict authentication, framing, scoped
late cancellation, two-way bridge traffic, marker publication and expiry,
synthetic native audio, device gates, bounded browser conversion and phone
cleanup. The existing ESPHome runner includes the Assist hold/resume test.
Tests use generated PCM and private Unix sockets, never a live microphone.

Host checks do not qualify real iPhone microphone permission, certificate
trust, full-duplex acoustic echo cancellation, audible cues, RF jitter, room
latency or ARM hardware resource use. Those require an explicit device test
after source/security review. The current ESPHome device does not gain this
capability until an implementation of its endpoint protocol is installed.

## Additional voice board clients

Keep authentication, endpoint permission and one-call ownership at the control
plane. A new board needs an adapter that can stop its Assist capture/TTS,
provide post-AEC mono PCM, play caller PCM through its existing output owner,
honour physical mute/privacy/DND and maintain expiring hold/ack state. It must
not open a second hardware PCM path. Do not advertise the board as callable
until all these gates are available and tested.

The current endpoint adapter protocol is documented in
[INTERCOM_PROTOCOL.md](INTERCOM_PROTOCOL.md). Future adapters can implement that
bounded local boundary while retaining the same authenticated browser flow.
Cross-speaker calling, HA dashboard embedding and arbitrary remote endpoints
need an explicit endpoint registry and permissions; this candidate does not
silently trust a URL or expose an unauthenticated audio endpoint.
