---
"tempo": patch
---

Update CodeQL init and analyze together to v4.39.3, fixing security scans that fail when only one step is upgraded. Group future CodeQL Dependabot version updates in one PR and check that workflow steps remain pinned to the same commit.

No firmware migration or device testing is required. Validation: full host suite, workflow lint, and CodeQL version regression test. Live GitHub security scans must pass before merge.
