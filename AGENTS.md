# Pull request review limits

Before opening or updating a PR, measure its complete diff against the actual target branch and keep it below the configured `CODEX_REVIEW_DIFF_MAX` with margin. If Codex review fails because the diff exceeds the limit, automatically split the change into smaller coherent PRs, retaining tests and documenting dependencies. Do not ask again for permission to split, raise the limit, truncate review input, or bypass checks. Never merge without the user’s explicit permission.
