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

The personal `0.15.0` Echo 2 candidate was built from recorded source commits.
Its evidence records passing source tests, supplemental checks, independent
image verification, signature checks and local-file installer validation.
The candidate's status is **`PREPARED_NOT_FLASHED`**.

Those checks establish the outcomes named in their reports. They do not
establish a successful boot on a physical Echo, audible playback, microphone
quality, wake-word accuracy, electrical safety, recovery success, or reliable
operation in a home. AI review is not independent human hardware validation.
Do not describe the candidate as hardware-tested, production-ready or safe
for every device based on these reports.

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
