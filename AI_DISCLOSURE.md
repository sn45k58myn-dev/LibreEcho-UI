# AI-assisted development disclosure

**This community fork contains additions and modifications developed with the
assistance of AI coding tools.** The disclosure applies to this fork's work;
it makes no claim about how the original LibreEcho authors developed their
project.

AI assistance was used to inspect source code, investigate failures, propose
and implement changes, prepare build scripts and documentation, and run and
interpret automated checks. AI-generated or AI-assisted work can contain
incorrect assumptions, incomplete fixes, security problems and misleading
explanations. Passing tests does not remove those possibilities.

## What has been demonstrated

For community release `0.16.8`, host tests exercised the call filter and the
intercom's browser and native audio paths. A signed build ran on one
full-size Echo 2 (2017). A tester heard less echo on an iPhone during a
normal two-way call. Hardware and room conditions differ. The test covers
one device and call setup. See the [product's verification record](https://github.com/sn45k58myn-dev/LibreEcho/blob/docs/community-publication/docs/community/0.16.8.md).

A known radio startup/pause race remains: pausing before the first PCM output
can stop a child before its parent-death guard is armed. The established
playback path passed its tests. The published source and evidence must retain
that distinction until a separately reviewed fix is verified.

## Expectations for future work

Contributors should identify AI-assisted changes in their pull requests,
review generated code and cited sources, explain the behavior they intended
to change, and provide reproducible evidence. Do not submit generated results
as observations from hardware that was never tested. Do not invent board
pinouts, compatibility claims, test results, or recovery guarantees.

New boards, including Echo 3 and Echo Studio, remain research targets until
their device-specific access, kernel, audio, privacy controls, installation
and recovery paths have been implemented and verified. A roadmap entry is
not support and does not make an existing image suitable for another board.

Never upload credentials, private signing keys, personal voice recordings,
home-network configuration, or private diagnostic data to public issues or
repositories. AI tooling does not change the project's privacy and source
publication boundaries.

Read [ATTRIBUTION.md](ATTRIBUTION.md) for the original project's credit and
[DISCLAIMER.md](DISCLAIMER.md) before considering hardware use.
