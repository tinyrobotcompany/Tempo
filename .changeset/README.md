# Change notes

Add a uniquely named Markdown file for firmware, companion, tests, scripts or CI changes. Use frontmatter `"tempo": patch` (or minor/major), enclosed by `---` lines, followed by user impact, compatibility/migrations, known issues and validation.

Pure SHA/version-annotation updates to existing remote GitHub Actions in existing workflow files do not require a firmware release note. All other releasable changes, including substantive workflow edits, require notes regardless of author, branch name or workflow actor. Tests and security checks still run for dependency updates.

`python3 scripts/release.py prepare` consumes these notes into versioned release notes and CHANGELOG.md and updates firmware/CMakeLists.txt. It does not commit, publish or flash. See [release strategy](../docs/RELEASING.md).
