# Code signing policy

Free code signing provided by [SignPath.io](https://signpath.io), certificate by [SignPath Foundation](https://signpath.org).

## Scope
Only the Windows binaries of FastCAE (`fastcae-setup-<version>.exe` installer and the executables/DLLs built from this repository) that are produced by the public GitHub Actions workflow `.github/workflows/build.yml` from the source code in this repository are submitted for signing. Third-party DLLs (Qt, MinGW runtime, etc.) are redistributed unmodified under their own licenses, as listed in `THIRD_PARTY_NOTICES.txt`.

## Roles
- **Author / Committer:** [Trantamming](https://github.com/Trantamming) (Trần Minh Tâm)
- **Reviewer:** [Trantamming](https://github.com/Trantamming) – all external pull requests are reviewed before merge
- **Approver:** [Trantamming](https://github.com/Trantamming) – every signing request is approved manually

All team members use multi-factor authentication on GitHub and SignPath.

## Build and signing process
1. A tagged release triggers the public GitHub Actions build from this repository.
2. The unsigned artifact is uploaded to SignPath by the official SignPath GitHub Action.
3. The approver manually approves the signing request.
4. The signed installer is published on [GitHub Releases](https://github.com/Trantamming/FastCAE/releases).

## Privacy policy
This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it. FastCAE runs locally and does not collect telemetry. [Adjust this paragraph if any feature contacts the network.]
