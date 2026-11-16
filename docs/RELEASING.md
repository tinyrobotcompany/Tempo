# Tempo release strategy

## Automatic signed application releases

A firmware-related merge into main starts **Publish OTA Release** (`ota-release.yml`). Required host tests, 80% per-file coverage, firmware build/memory checks and security jobs must pass before signing/publication. PR builds do not access signing secrets or publish firmware.

There is one production channel. Devices normally check every **30 days**; hourly checking is an explicit temporary testing setting. Checking, downloading and scheduled installation are separate operations. Read [OTA operation, credential migration and physical acceptance](OTA.md) before the first bootstrap.

## Versions and release notes

Each application PR adds `.changeset/<description>.md`, with `"tempo": patch`, `minor` or `major`, and user-visible notes, compatibility and known limitations. The release job computes the next semantic version using the latest published/tagged version and the new notes since that tag. Dependency-only changes default to patch. It builds the reviewed source commit with `TEMPO_RELEASE_VERSION` and refuses an artifact whose ESP application descriptor does not match.

Tags point to reviewed source commits; generated release metadata does not bypass main's PR rules. `firmware/CMakeLists.txt` is the local-development baseline, while the GitHub release tag, signed manifest and installed image are the authority for published versions. The pipeline records the source SHA in `release.json` and release notes. GitHub Releases are the published changelog; `.changeset` retains the source notes. Never invent historical releases or move a published tag.

## Signing and publishing

The private signing key is an encrypted GitHub environment secret named `OTA_SIGNING_PRIVATE_KEY`, in the **production-ota** environment restricted to the `main` branch. Its matching public key is committed in `firmware/main/ota_public.pem`. No production PRIM or GitHub credential is supplied to the firmware build. The private key must be backed up securely by the maintainer; losing it can require a USB trust bootstrap. See OTA.md for deliberate key-rotation sequencing.

The serialised job creates a draft, attaches firmware, a signed manifest, signature, checksums, dependency lock and build/source metadata, then publishes the complete release with notes. Published packages must be immutable; reruns only complete existing drafts or exit if the release is already published. An older source commit cannot allocate a release after a newer tagged commit. Failed validation/signing leaves no installable release.

GitHub release assets provide downloads without separate website hosting or a device-side PAT. The public repository is part of this distribution assumption. A fleet service/telemetry is not implemented.

## Initial activation

1. Configure the main-only signing environment and matching signing key.
2. Merge the OTA implementation after review/checks and acceptance planning; its firmware changes trigger the first release automatically.
3. Verify the GitHub release contains matching notes and all six update/metadata assets.
4. USB-install an OTA-capable bootstrap with the same public key and rollback-enabled bootloader. Provision PRIM once into device NVS.
5. Validate one complete update and rollback/power-cut matrix on hardware before sale or claiming production qualification. Restore monthly checks after testing.

No release, merge or device flash is implied by local source changes. Do not distribute older binaries containing a compiled transport key.

## Legacy source-only tooling

`scripts/release.py prepare` and `release.yml` remain available for deliberately prepared `source-vX.Y.Z` source-only tags (for example `source-v0.8.16`). Automatic OTA uses `vX.Y.Z`; those tags never trigger the legacy source workflow. They are not the automatic OTA publication path. They require matching checked-in versioned notes and create a draft that a maintainer must explicitly publish. Do not mark a source-only release as the latest production OTA release: it has no signed package for devices. New firmware releases should use the automatic workflow above.

If release automation lands before the runtime prerequisite, CI/security still validate the commit and the readiness job reports publication inactive. No signing or publication runs. A later firmware merge reevaluates readiness automatically; the build retains the same prerequisite checks as defense in depth.
