# Contributing to this community fork

Thank you for helping improve an independent fork of
[LibreEcho](https://github.com/aslater3/LibreEcho). The original project and
its contributors deserve full credit; read [ATTRIBUTION.md](ATTRIBUTION.md).
Also read [AI_DISCLOSURE.md](AI_DISCLOSURE.md), [DISCLAIMER.md](DISCLAIMER.md)
and the repository's existing `CONTRIBUTING.md` and `AGENTS.md`.

This guide adds fork-specific context. It does not replace the existing
source, testing, privacy, licensing or boot-chain rules.

## Choose the repository

| Change | Community repository |
| --- | --- |
| Project documentation, cross-repository planning, build/release coordination | [LibreEcho](https://github.com/sn45k58myn-dev/LibreEcho) |
| Web UI, API or native service behavior | [LibreEcho-UI](https://github.com/sn45k58myn-dev/LibreEcho-UI) |
| Initramfs, image packaging, audio or update tooling | [LibreEcho-Platform](https://github.com/sn45k58myn-dev/LibreEcho-Platform) |
| Kernel, device tree or driver | [LibreEcho-Linux](https://github.com/sn45k58myn-dev/LibreEcho-Linux) |

For a change spanning repositories, open a planning issue in the community
Product repository and link the component pull requests. Report fork-specific
problems here rather than asking the upstream maintainers to support this
fork. General upstream fixes can be proposed upstream separately, with clear
attribution and appropriate evidence.

## Make focused, reviewable changes

Use a purpose-named `feature/` or `fix/` branch and preserve unrelated work.
Describe the concrete problem, resulting behavior, affected targets and
relevant risks. Include the exact source commits and reproducible commands
for checks you actually ran. Identify AI assistance and review its output;
generated confidence or an AI explanation is not evidence.

Keep existing validation and security checks. Do not resolve a failed test by
removing its assertion, weakening a hardware identity check, accepting a
different dependency hash, or bypassing signature verification. Report known
limitations honestly. Never post signing seeds, tokens, passwords, private
recordings, personal identifiers or unredacted home-network diagnostics.

Retain copyright and license notices. Check dependency and model licenses and
redistribution boundaries before proposing binary downloads or releases.

## Separate evidence levels

State whether a result is a source review, host test, target compilation,
image/installer validation, or an observation on named physical hardware.
Include device model, board revision, build identity, test conditions and
redacted logs for hardware claims.

Release `0.16.8` was exercised on one full-size Echo 2 in a two-way iPhone
call. Report your own hardware and observed results when testing a different
board or build. The [release notes](https://github.com/sn45k58myn-dev/LibreEcho/blob/docs/community-publication/docs/community/0.16.8.md) describe the scope of the tests.

Documentation-only publication commits may sit above the source commits used
to build a candidate. Preserve and identify those frozen build inputs; do
not silently relabel an old binary as a build of a different commit.

## Adding boards, including Echo 3

Keep the current four-repository separation and use the existing Product
porting guide as the starting point. New boards remain **research targets**
until the necessary implementation and device-specific verification exist.
Echo 3 and Echo Studio are not enabled by this fork's Echo 2 image.

A proposed port should collect evidence for:

1. Exact retail model, board revision, codename and device-reported identity.
2. SoC, architecture, memory, boot chain, partition map and recovery access.
3. Kernel and device tree, clocks, power, storage, networking and USB behavior.
4. Speaker codec/amplifier routing, microphones, buttons, LEDs and hardware
   privacy/mute behavior.
5. Fail-closed Product/Platform/kernel target descriptors and installer checks.
6. Reproducible build, licensing/source offers, complete payload installation,
   signatures, update behavior and recovery evidence.
7. A documented physical acceptance matrix with passing and failing results.

The current target family is MT8163/ARM32. A different SoC requires real
kernel/platform and tools-profile work, not merely a new device name in a
target list. Unknown boards must continue to be rejected. Avoid destructive
flash experiments until the specific boot chain and a viable recovery route
are established; preserve existing boot-chain safeguards.

## Releases and support

Submitting or merging source does not itself approve a release, an automatic
update pointer, signing-key changes or a hardware flash. Those are separate
review steps with exact artifacts and evidence. Inherited upstream workflows
and download defaults must be reviewed before enabling fork publication.

Issue reports and contributions are welcome, but assistance is voluntary.
There is no promised installation, repair, recovery or unbricking service.
