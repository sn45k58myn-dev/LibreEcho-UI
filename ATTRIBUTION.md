# Attribution and project relationship

This repository is part of an independent community fork of **LibreEcho**.
The original LibreEcho project was created by **Andy Slater
([aslater3](https://github.com/aslater3))**, with work from the LibreEcho
contributors and the upstream projects on which LibreEcho depends. Credit for
the original project, hardware research, architecture, software, documentation,
and existing features belongs to those authors. This fork adds to that work;
it does not claim to have created LibreEcho.

## Original project

- [LibreEcho website and hardware roadmap](https://libreecho.org/)
- [LibreEcho product, documentation and release tooling](https://github.com/aslater3/LibreEcho)
- [LibreEcho UI and services](https://github.com/aslater3/LibreEcho-UI)
- [LibreEcho Platform and image tooling](https://github.com/aslater3/LibreEcho-Platform)
- [LibreEcho Linux kernel](https://github.com/aslater3/LibreEcho-Linux)

The original repositories and their histories remain the authoritative record
of their authors' contributions. Please support and credit the original
project when sharing or discussing this fork.

## This community fork

The community repositories are maintained under
[sn45k58myn-dev](https://github.com/sn45k58myn-dev):

| Repository | Responsibility |
| --- | --- |
| [LibreEcho](https://github.com/sn45k58myn-dev/LibreEcho) | Project home, cross-repository documentation, build and release coordination |
| [LibreEcho-UI](https://github.com/sn45k58myn-dev/LibreEcho-UI) | Web UI, API and native services |
| [LibreEcho-Platform](https://github.com/sn45k58myn-dev/LibreEcho-Platform) | Initramfs, image construction, audio and update tooling |
| [LibreEcho-Linux](https://github.com/sn45k58myn-dev/LibreEcho-Linux) | Kernel, device trees and drivers |

This fork is not an official LibreEcho release and is not endorsed, maintained
or supported by Andy Slater, the upstream LibreEcho maintainers, Amazon, or any
hardware manufacturer. Questions about changes made in this fork belong in
this fork's issue tracker, not in the upstream project's support channels.

## Licenses and third-party work

Keep the original copyright notices, license texts, authorship history and
third-party notices with the code. Attribution does not replace a license or
grant rights that the applicable license does not grant.

- The original Product repository's MIT notice credits Andy Slater.
- The UI repository's root MIT notice credits LibreEcho contributors. Files
  with separate notices and vendored components retain their own terms;
  consult its `THIRD_PARTY_NOTICES.md` and individual component notices.
- The Platform tree contains Linux-kernel-derived GPLv2 code and additional
  tooling. Its `COPYING` and each file's notices remain applicable.
- The Linux repository's `COPYING` identifies
  `GPL-2.0 WITH Linux-syscall-note`; additional per-file licenses may apply.
- Codecs, libraries, models, firmware and other dependencies may have their
  own licenses, source-offer requirements or redistribution limits. A public
  source repository is not permission to redistribute every dependency or
  owner-imported device file.

No blanket relicensing is intended. [The disclaimer](DISCLAIMER.md) and
[AI disclosure](AI_DISCLOSURE.md) supplement the project documentation; they
do not remove any license obligations or rights.

Amazon, Echo and other product names are used only to identify hardware and
compatibility research. They remain the names or marks of their respective
owners.
