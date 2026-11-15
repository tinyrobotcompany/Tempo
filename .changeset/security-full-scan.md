---
"tempo": patch
---

Align PR and main CodeQL security gates by disabling diff-filtered results, so unchanged high-severity findings block before merge. Keep the transport preflight test's dummy credential in memory while retaining key parsing and capture redaction coverage. Security gate failures now identify the rule and location without printing source snippets.

Validation: preflight tests, security gate tests, full local pre-commit suite, and full-repository GitHub CodeQL analysis. No firmware behaviour or credential configuration changes. Full PR scans may take longer than diff-filtered scans.
