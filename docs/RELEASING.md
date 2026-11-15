# Tempo release strategy

## Version and release policy

Use stable semantic versions `MAJOR.MINOR.PATCH`; `firmware/CMakeLists.txt` is the version authority. Tags are `vX.Y.Z`, and the device reports the same firmware version. Tempo is currently in the 0.x development series. Patch = compatible fixes/tooling; minor = compatible features; major = breaking behaviour, settings format or installation requirements. Even before 1.0, document every breaking change and its migration explicitly. Prerelease channels and OTA rollout policy are deferred.

Adapted from Voxa's change-note-driven versioning, validation gates and GitHub release notes. Tempo deliberately prepares the version in a reviewed commit before tagging: computing a new tag after building firmware would leave the device version inconsistent. Releases are batched deliberately, not automatically published on every merge.

## Change notes on each PR

Add `.changeset/<unique-description>.md`:

```markdown
---
"tempo": patch
---

Describe the user-visible change and why it matters.

Compatibility/migration: explain any settings, partition or API changes.
Known issues: document relevant limitations.
Validation: state what was actually tested, including hardware when relevant.
```

Use `patch`, `minor` or `major`. Notes become release content verbatim; do not include secrets, private addresses or tokens. Code PRs require a note; Dependabot retains the existing explicit exemption. Documentation-only PRs need a note when the information matters to the release. CI validates all pending note formats.

## Prepare and review a release

On a release-preparation branch containing the intended changes, run:

```sh
python3 scripts/release.py prepare
```

The command chooses the highest requested bump, updates CMake, creates `docs/releases/X.Y.Z.md`, prepends the same content to `CHANGELOG.md`, updates the README firmware text/badge, and removes the consumed notes. It rejects missing/invalid notes and existing target release records. It does not commit, tag, publish, flash or call APIs. Inspect the diff before committing; an interrupted operation must be inspected and repaired before retrying. Git remains the recovery/history mechanism.

Review release notes for real user impact, hardware compatibility, migration steps, known issues and actual validation. Run the full suite and device acceptance tests appropriate to the changes. Submit a PR; release preparation is accepted by the changeset gate only when firmware version, changelog and versioned notes are present and no pending notes remain. Merge only after required checks/review pass. Avoid bundling unrelated implementation changes in a release-preparation PR.

## Tag and draft

After the preparation commit is merged into main, an authorised maintainer creates an annotated `vX.Y.Z` tag on that exact commit and pushes that tag. Never move a published tag. This is an intentional release action, not performed by the preparation command.

The Draft Release workflow:

1. Runs full host tests, the ESP32-P4 build/memory check, and security scans on the tagged commit.
2. Requires the commit to belong to main, the tag to equal the firmware version, no pending notes, and matching versioned notes/changelog.
3. Creates a source-only GitHub **draft** using the checked-in notes, with concurrency protection and an existing-release check for reruns.

A maintainer reviews and publishes the draft explicitly. There are no firmware binaries with embedded transport credentials, no automatic installation, and no OTA claims. Failed checks produce no release draft. Do not bypass failed gates; correct the preparation before publication, or use a new version if already released.

## OTA work to follow

Future work must define signed firmware, credential provisioning, artifact hashes, device compatibility, staged rollout, interruption recovery, rollback and update channels. Release tags, matching firmware versions, notes and validation gates established here will supply the identity/history for that system; they do not implement it.

## Activation and current status

These workflow files must be published/merged to GitHub before they can run. Required checks need repository ruleset activation. `OPENAI_API_KEY` enables PR review; `REPO_ADMIN_TOKEN` is needed only by the ruleset configuration workflow. The tag release uses GitHub's built-in token and does not need the OpenAI or PRIM key. No release was published as part of implementing this strategy.
