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

## Source and hardware status

The frozen personal `0.15.0` candidate for the full-size Echo 2nd Generation
(`radar_puffin`) passed local source tests, cross-compilation, image/signature
verification and complete installer preparation. It remains
**`PREPARED_NOT_FLASHED`: no physical boot, audio, microphone or update acceptance.**
The candidate's original build input for this component is `7119e28fc7464c321375f5b5df9d95c0051d3929`.
Publication documentation is added above those frozen inputs; an existing binary
is not relabelled as a build of the publication commit.

Echo Dot 2 support is inherited from upstream and has not been qualified in this
fork. **Echo 3, Echo Studio and additional boards are research targets only:
unsupported, with no compatible image or enabled port provided here.**

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

This publication contains source and documentation. No stable firmware release or
working Echo 3/Studio port is announced. The locally prepared personal image uses
a personal OTA public key and does not accept official LibreEcho OTA signatures;
its private signing seed is not published.

Inherited build/release workflows and download wrappers still contain upstream
repository defaults. They must not be presented as building or installing this
fork. Fork GitHub Actions are to remain disabled until checkout identities,
permissions, signing and release destinations have been adapted and reviewed.
No remote CI success is claimed by this source publication.

The original component documentation is preserved verbatim in
[README.upstream.md](README.upstream.md). It describes an upstream snapshot;
upstream badges, support claims, releases and install commands in that document
are **not evidence or instructions for this fork's modified candidate**.
Never bypass target, partition, boot-chain or signature checks to try a new board.

## Known limitation

The established-playback radio regression passes. A separate pre-existing race
can stop a just-starting radio child before its parent-death guard is armed when
pause happens before the first PCM. This fork does not claim that race is fixed.
See the project build-status page for the evidence limits and next gates.

## Exact tested source history

The [source-history bundle](docs/community/history/tested-source.bundle) preserves
the tested source files and their history on top of the upstream baseline. The published commit IDs reflect an authorship metadata cleanup.
It contains Git source objects, not device firmware or signing secrets. This also
keeps the frozen build ledger usable when GitHub's API creates a publication
commit with a new commit ID but the same checked source files.

On a Linux filesystem, after cloning this fork and making the upstream prerequisite
available, verify and fetch the bundle without flashing or changing device state:

```sh
git bundle verify docs/community/history/tested-source.bundle
git fetch docs/community/history/tested-source.bundle HEAD
git rev-parse FETCH_HEAD
```

The last command must print `7119e28fc7464c321375f5b5df9d95c0051d3929`.
The publication tree includes documentation above that frozen source; it does
not claim an already-built binary was rebuilt from a new publication commit.
