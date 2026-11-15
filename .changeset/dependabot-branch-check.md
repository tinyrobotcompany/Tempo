---
"tempo": patch
---

Fix the PR branch-name workflow check to accept Dependabot branch names while remaining compatible with `grep -E` on GitHub Actions runners.

Keep branch naming enforcement for contributor branches and preserve the documented accepted formats in workflow output.

Validation: regex matching tested locally for expected passing and failing branch-name examples.
