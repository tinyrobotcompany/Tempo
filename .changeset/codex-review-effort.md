---
"tempo": patch
---

Run Codex PR review at medium reasoning effort to reduce Critical/High findings that the diff and its tests already disprove. Raise the output budget from 6000 to 20000 tokens because reasoning tokens count toward it; an exhausted budget returns an incomplete response, which the review fails closed on. The diff limit and blocking rules are unchanged.
