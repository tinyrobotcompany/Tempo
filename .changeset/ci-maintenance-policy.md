---
"tempo": patch
---

Consolidate routine GitHub Actions version updates into one monthly PR while retaining separate security updates. Use the built-in GitHub Actions bot for Codex reviews and remove optional GitHub App credentials.

Require release notes based on changed content rather than the workflow actor. Only pure pinned-action version updates are exempt; application and substantive workflow changes still require notes. Tests and security scans continue on PRs and pushes.

Validation: policy regression tests, full local pre-commit suite, and workflow lint. No firmware migration is required. Existing CODEX_REVIEW_APP_ID and CODEX_REVIEW_APP_PRIVATE_KEY settings are no longer used.
