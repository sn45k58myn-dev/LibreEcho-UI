# Experimental hardware and software disclaimer

**This is an independent, experimental community fork. It includes
AI-assisted additions. Use it at your own risk. The personal `0.15.0` Echo 2
candidate is `PREPARED_NOT_FLASHED`; physical hardware acceptance has not been
established.**

Read this document together with [AI_DISCLOSURE.md](AI_DISCLOSURE.md),
[ATTRIBUTION.md](ATTRIBUTION.md), the applicable licenses, and the instructions
for the exact hardware and build you intend to use.

## No guarantee of compatibility or successful operation

Source availability, a successful compilation, passing automated tests, an
installer's recognition of a target, or a valid signature does not guarantee
that a device will boot, work correctly, remain secure, or be recoverable.
Hardware revisions, component differences, boot-chain versions and stock
software can change the outcome.

The current personal candidate is for the full-size Echo 2 target family
identified in its manifest. It is not evidence of this fork's hardware
acceptance for an Echo Dot, Echo 3, Echo Studio or any other board. Research
notes, proposed ports and roadmap entries are not compatibility approvals.
Do not cross-flash an image because devices look similar or share a chip.

## Risk of damage, loss and interruption

Flashing, unlocking, partition changes, experimental firmware, soldering and
other modifications can permanently damage or brick a device. They can also
destroy data or settings, make recovery impossible, compromise security or
privacy, and cause unreliable audio, microphones, lights, buttons, networking
or connected-home behavior. Physical work can cause short circuits,
overheating or electrical hazards. A failure may appear only after a later
restart or update.

Only work on hardware you own or are authorized to modify. You are
responsible for verifying the exact model, board revision, boot chain,
partition layout, wiring, power requirements, backups and recovery method
before making changes. Stop if the evidence is missing or contradictory.
Instructions for another model or boot-chain generation may be destructive.

Do not rely on this project for security, access control, emergency services,
medical needs, safety-critical functions, or any task where failure could
cause harm. Test connected-home actions in a controlled setting before
allowing ordinary use.

## No repair, unbricking or support obligation

The maintainers, contributors and original upstream authors have **no
obligation to diagnose, repair, unbrick, recover, replace or reimburse a
damaged or non-working device**. They do not undertake to recover lost data,
restore vendor software, provide individual installation help, supply
replacement hardware, or pay associated costs.

Opening an issue, following documentation, downloading a build, receiving a
voluntary reply, or receiving an AI-assisted suggestion does not create a
promise that support, recovery or a working result will be provided. Any help
is voluntary and may be limited or unavailable. Do not proceed on the
assumption that someone else will fix a failed modification.

This fork does not supply an Amazon or other manufacturer's warranty, repair
service or support agreement. It cannot promise that a manufacturer or the
original LibreEcho project will support a modified device.

## Software and documentation are provided as is

To the fullest extent permitted by applicable law, this project's additions,
software, build artifacts, documentation, examples and advice are provided
**AS IS** and **AS AVAILABLE**, without any express or implied warranty or
guarantee, including guarantees of correctness, compatibility, fitness for a
particular purpose, availability, security, non-infringement or freedom from
defects. Features may change, be removed or remain unfinished.

AI-assisted code and explanations may be wrong even when they appear
plausible or pass automated tests. A signature establishes an artifact's
relationship to a signing key; it is not a safety certification. A checksum
establishes byte identity; it is not proof that the bytes are suitable for a
device.

## Limitation of liability

To the fullest extent permitted by applicable law, the fork maintainers,
contributors, authors and copyright holders accept no liability arising from
use of, inability to use, or reliance on this project. This includes device
damage, bricking, lost data or settings, loss of access or service, privacy or
security incidents, property damage, expenses, lost time or income, and
direct, indirect, incidental, special or consequential losses.

This wording does not promise legal immunity. It does not exclude or limit
liability that cannot lawfully be excluded or limited, and it does not remove
any statutory or other rights that applicable law makes non-excludable.
Different jurisdictions may give this wording different effects.

## Independence, licenses and updates

This fork is not endorsed by Amazon, any hardware manufacturer, Andy Slater,
or the original LibreEcho maintainers. Do not attribute this fork's failures,
support obligations or AI-assisted changes to them.

Original copyright notices, licenses, source-offer obligations and per-file
terms remain in effect. This document does not relicense third-party work,
add restrictions contrary to an applicable open-source license, or grant
permission to redistribute restricted firmware, models, credentials or
recordings.

The personal candidate uses a personal update trust key. It does not accept
official LibreEcho OTA signatures through that trust root. Returning to the
official trust root requires the appropriate official reinstall path; do not
assume update packages from different projects or signing keys are
interchangeable. Never bypass signature, model, partition or boot-chain
checks to make an installer continue.
