# Tempo repository guardrails

Adapted from project-template `ded9fcfb24cbe0bb741c2aed71d9a42bb1ffd0af` and Voxa `26d57c26905d55dcfd33fe89179edff024f76c92`.

## Local setup — required for every clone

Use macOS with Xcode Command Line Tools, Node 22+, Python 3.10+, CMake and actionlint (`brew install cmake actionlint node`). Apple `aea` and `aa` are required for signed Shortcut verification. No API key, live transport endpoint, USB device or firmware flash is needed for unit tests.

```sh
bash scripts/setup-hooks.sh
bash scripts/test-all.sh
```

Git does not automatically activate tracked hooks on clone. Setup records `core.hooksPath=.githooks` in local repository configuration, shared by its worktrees. The hook exports the index into an isolated temporary directory and runs **every** host test on **every** commit, including documentation-only and empty commits. It fails if a tool/dependency is missing, a test fails, or the index changes during execution. Unstaged edits remain untouched and cannot hide staged failures. Do not use `--no-verify`.

Tests include core/settings/calendar, networking, weather, transport/parser/filter, phone server/protocol/reconnection, native Shortcut integrity, all clock styles/layouts, map decoding/cache, ASan/UBSan, adapter race/startup guards, watcher/preflight, hook behaviour, Codex review/security scripts and workflow lint. Host dependencies are pinned by SHA256 and restored from the existing Espressif cache or official HTTPS sources. Subsequent runs need no downloads. CMake outputs remain private in build-host.

## Pull requests and CI

- `All Unit Tests`: full macOS host suite (unit, host integration and regression), exactly the same runner as the commit hook; enforced 80% coverage gates and retained CI reports. See [testing scope](TESTING.md).
- `ESP32-P4 Build`: runs only when firmware build inputs change (source, configuration, embedded assets, credential generator or embedded Shortcut). A non-secret CI sentinel keeps transport paths linked for the PSRAM placement check; the generator refuses this mode if `.env` exists. No production token or flashing. The aggregate gate accepts a skip only when the detector explicitly reports no firmware change.
- `Changeset Check`: required change note for firmware, companion, tests, scripts, hooks and substantive workflow changes, regardless of PR author or triggering actor. A diff containing only SHA/version-annotation updates to existing remote action references in existing workflow files is exempt. Commands, inputs, permissions, action identity changes, file additions/deletions, and mixed application changes still require notes.
- `Branch Name` / `PR Title (Conventional Commits)`: template naming conventions.
- `Secret Scan`: pinned Gitleaks CLI, full-history scan with redacted output.
- `CodeQL Scripts`: JavaScript, Python and GitHub Actions, with a high-severity SARIF gate. PRs and main both evaluate full-repository results (`CODEQL_ACTION_DIFF_INFORMED_QUERIES: 'false'`), so an unchanged finding cannot pass a PR and then block its merge run. Gate logs identify the rule and location without source snippets or diagnostic messages containing sensitive evidence.
- `CI Required` and `Security Audit Required`: fail unless every dependency succeeds; cancellations/skips do not count as success.
- Dependabot proposes routine GitHub Actions version updates monthly in one grouped PR, with at most one routine update PR open. This schedule does not change CI: tests, security scans, builds and reviews still run on their existing PR/push triggers. Security updates remain separate from the monthly version-update group and limit; keep Dependabot security updates enabled in repository settings. CODEOWNERS and the PR template provide review ownership and evidence prompts.
- CodeQL `init` and `analyze` must use the same commit because they share versioned configuration. Routine updates are included together in the monthly group; CodeQL security updates have their own group. The local suite checks this invariant. After a replacement PR is merged, close superseded partial updates rather than merging them or weakening checks.

C firmware validation is build + host tests + sanitizers, not a claim of complete C static analysis. Hardware, timing, display DMA and provider behaviour still require device testing.

## Codex review and secrets

Use `OPENAI_API_KEY` in local `.env`; never commit it. Actions cannot read your local file. Configure the repository secret with the same name. From an authenticated, network-enabled Terminal:

```sh
python3 scripts/sync-github-secrets.py --env /absolute/path/to/.env
```

The helper sends only `OPENAI_API_KEY` and, if present, `REPO_ADMIN_TOKEN` to tinyrobotcompany/Tempo. It never sources the file as shell code, prints values, or sends the PRIM key. The OpenAI key is tooling-only and never embedded in firmware.

`Codex Review` runs on pull_request_target using **base-commit scripts and rubrics only**. PR metadata/diffs are untrusted data sent to OpenAI; no PR-head code is checked out or executed with secrets. The model defaults to the reference workflow's `gpt-5.5`; override with repository variable `CODEX_REVIEW_MODEL` if needed for your account. A review is tied to the exact head commit and both head/base are checked before posting. Missing key, API failure, incomplete/malformed output, changed head/base, or oversized diff fails the check. Split a PR exceeding 120,000 diff characters. Critical/High findings are posted and block the check. No Dependabot/release-name bypass or passing fallback is used.

The workflow must first be merged into main before pull_request_target can execute it from the trusted base. This initial infrastructure PR therefore needs normal human review; subsequent PRs get automated review. Do not claim the first PR was automatically reviewed before bootstrap.

### Review author identity

Every review includes a visible **Codex Code Review** heading. GitHub derives the author from the authentication token; changing the heading or workflow name cannot rename the author. The built-in workflow token posts as `github-actions[bot]`, not the person who triggered the workflow. Reviews posted separately using a personal access token appear under that person's account.

The workflow always uses the built-in `github.token`. No separate GitHub App, app-token action, or private key is needed. Legacy `CODEX_REVIEW_APP_ID` and `CODEX_REVIEW_APP_PRIVATE_KEY` settings are unused and can be removed after this change is merged. Existing reviews keep their original authors. Workflow changes take effect after merging into main because review code comes from the trusted base.

## Required checks — activation is separate from workflow files

`.github/required-checks.json` lists the stable contexts for main. `Ensure Required Checks` maintains an active, strict ruleset and prohibits deletion/force-push. It creates no bypass actors and preserves existing restrictions. It needs a repository-scoped fine-grained `REPO_ADMIN_TOKEN` with Administration: write, stored as a GitHub Actions secret. This is separate from the OpenAI API key. Without it the configuration job fails visibly; YAML files alone do not enforce merging rules.

Alternatively a repository administrator can configure the listed required checks in GitHub Settings > Rules > Rulesets. Check names must match exactly. Initial merge and actual ruleset activation are pending until remote write/admin access is available.

## Releases and intentionally excluded template features

A pushed `vX.Y.Z` tag runs full CI and security validation, verifies the commit belongs to main and matches firmware/CMakeLists.txt, then creates a **draft source release**. No binary with embedded credentials is uploaded, no device is flashed, and this is not OTA. Release preparation uses `python3 scripts/release.py prepare` to calculate the highest pending change-note bump and update the embedded firmware version and release history together. Tags must match that version and checked-in notes. See [Release strategy](RELEASING.md).

Azure/IaC deployments, .NET, Swift app builds, TestFlight, project-board automation and quant/agent services are not applicable to this standalone firmware/browser project and are not copied. The native Shortcut test is retained. Device OTA remains a separate outstanding product requirement.

### Verified check contexts

GitHub check-run names (queried from the commits/check-runs API on PR #1) are `CI Required`, `Security Audit Required`, `Branch Name`, and `PR Title (Conventional Commits)`. The workflow prefix shown in the PR interface is not part of these API names. Required contexts must match the API names, not the combined UI labels. `Codex Review` is the configured review job name and the initial bootstrap status context.

The release checkout explicitly fetches main with full history and tags. Validation peels both the event SHA and tag ref to commits, requires equality, and detaches at that commit before checking ancestry and release metadata.

Branch naming accepts both `feature/description` and its `feat/description` alias. Other accepted human prefixes and the existing Dependabot pattern remain unchanged; whitespace, malformed paths and unsupported prefixes are rejected.
