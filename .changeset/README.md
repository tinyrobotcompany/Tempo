# Change notes

Add a uniquely named Markdown file for firmware, companion, tests, scripts or CI changes. Use frontmatter `"tempo": patch` (or minor/major), enclosed by `---` lines, followed by user impact, compatibility/migrations, known issues and validation.

`python3 scripts/release.py prepare` consumes these notes into versioned release notes and CHANGELOG.md and updates firmware/CMakeLists.txt. It does not commit, publish or flash. See [release strategy](../docs/RELEASING.md).
