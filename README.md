# LibreEcho-UI — independent experimental community fork

**Built on [LibreEcho](https://github.com/aslater3/LibreEcho), created by
Andy Slater ([aslater3](https://github.com/aslater3)) and the LibreEcho contributors.**
Full credit for the original project, architecture, hardware research and existing
features belongs to the original authors. Visit [libreecho.org](https://libreecho.org/)
and read [ATTRIBUTION.md](ATTRIBUTION.md). Original licences and copyright notices
remain unchanged.

This is an independent fork with **AI-assisted additions**. It is not an official
LibreEcho release and is not endorsed by upstream or Amazon. Read
[AI_DISCLOSURE.md](AI_DISCLOSURE.md) and the full [DISCLAIMER.md](DISCLAIMER.md).
**There is no promised repair, recovery, unbricking, replacement or support service.**

## Echo 2 intercom in 0.16.8

Version `0.16.8` reduces the delayed sound of the phone caller's own voice
returning through the Echo microphone during a two-way call. One full-size
2017 Echo 2 (`radar_puffin`) ran the signed update. A subsequent iPhone call
sounded better to the tester. Native Sendspin and the existing intercom are
included in this UI source. [Intercom operation](docs/INTERCOM.md) ·
[Release and installation notes](https://github.com/sn45k58myn-dev/LibreEcho/blob/docs/community-publication/docs/community/0.16.8.md) · [Product release](https://github.com/sn45k58myn-dev/LibreEcho/releases/tag/echo2-source-v0.16.8).

The intercom uses a separate echo canceller during the call. Its 30 ms local
speaker-reference delay aligns playback with microphone capture inside the
Echo. End-to-end call delay includes phone, network and audio buffering and
varies between calls. Home Assistant voice and wake-word detection continue
through their own audio path. See the [intercom protocol](docs/INTERCOM_PROTOCOL.md)
and [microphone architecture](docs/MICROPHONE_ARCHITECTURE.md).

Echo Dot 2 retains its upstream support status. This release targets the
full-size Echo 2 only. Echo Studio and Echo 3 remain research targets.

## Four-repository layout

| Repository | Responsibility |
| --- | --- |
| [LibreEcho](https://github.com/sn45k58myn-dev/LibreEcho) | Project home, coordination, build/release tooling and documentation. |
| [LibreEcho-UI](https://github.com/sn45k58myn-dev/LibreEcho-UI) | Web interface, APIs and device/voice services. |
| [LibreEcho-Platform](https://github.com/sn45k58myn-dev/LibreEcho-Platform) | Initramfs, image and update tooling, audio and feature packaging. |
| [LibreEcho-Linux](https://github.com/sn45k58myn-dev/LibreEcho-Linux) | Kernel, device trees and drivers. |

## Hardware roadmap

Start at the [project home](https://github.com/sn45k58myn-dev/LibreEcho),
[hardware roadmap](https://github.com/sn45k58myn-dev/LibreEcho/blob/docs/community-publication/docs/community/ROADMAP.md),
[board proposal template](https://github.com/sn45k58myn-dev/LibreEcho/blob/docs/community-publication/docs/community/BOARD_PROPOSAL.md), and
[build status](https://github.com/sn45k58myn-dev/LibreEcho/blob/docs/community-publication/docs/community/BUILD_STATUS.md).
Use [CONTRIBUTING.community.md](CONTRIBUTING.community.md) for fork-specific routing
and preserve the existing source, test, licence and boot-chain rules.

## Build, installation and automation boundaries

Use the [product release notes](https://github.com/sn45k58myn-dev/LibreEcho/blob/docs/community-publication/docs/community/0.16.8.md) for the coordinated source pins,
checksums, and separate first-install and OTA instructions. The UI component
alone does not install firmware. The tested system uses a community OTA
signing identity. An existing device must trust the signer used for its update.

The fork's AI disclosure, warranty disclaimer and upstream attribution apply.
Inherited automation and download wrappers can point to upstream services.
Use the product release instructions for the fork. The original UI
documentation is preserved in [README.upstream.md](README.upstream.md).

## Known limitation

The established-playback radio regression passes. A separate pre-existing race
can stop a just-starting radio child before its parent-death guard is armed when
pause happens before the first PCM. This fork does not claim that race is fixed.
See the project build-status page for the evidence limits and next gates.

## Exact tested source history

The [public UI source commit](https://github.com/sn45k58myn-dev/LibreEcho-UI/commit/984ebc22cf68f1900e931d1129357db91951ea06)
has the same Git tree as the tested 0.16.8 UI source. Its parent is the
previously published 0.16.3 UI source. The publication branch adds fork
documentation and the [source-history bundle](docs/community/history/tested-source.bundle).

To verify the bundle, run the following in a clone containing upstream base
`b14f9eb4dc8343769eca86240bc258c9130fcc6b`.

```sh
git bundle verify docs/community/history/tested-source.bundle
git fetch docs/community/history/tested-source.bundle HEAD
git rev-parse FETCH_HEAD
git rev-parse 'FETCH_HEAD^{tree}'
```

Expected commit `984ebc22cf68f1900e931d1129357db91951ea06`. Expected source
tree `41b650f94bd108acb2c57e6c073712cf70ce8dd6`.
