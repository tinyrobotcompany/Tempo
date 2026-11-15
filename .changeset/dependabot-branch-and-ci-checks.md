---
"tempo": patch
---

Allow Dependabot pull-request branch names in the PR branch-name check while keeping the existing conventional branch format for regular development branches.

User impact: dependency-update PRs from Dependabot no longer fail immediately on branch-name policy when they otherwise meet repository conventions.

Compatibility/migrations: no runtime or API migration required.

Known issues: none identified.

Validation: branch-name pattern test for valid conventional and Dependabot branch formats; confirmed invalid branch formats remain rejected.
