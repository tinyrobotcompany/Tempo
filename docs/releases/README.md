# Versioned release notes

`python3 scripts/release.py prepare` creates `X.Y.Z.md` here from pending change notes. The tag workflow uses that exact file as the GitHub draft release body. Files record prepared release content; publication status is established by GitHub Releases. No past releases are inferred from firmware version numbers.
