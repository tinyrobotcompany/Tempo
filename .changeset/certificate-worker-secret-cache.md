---
"tempo": patch
---

Replace continuous healthy-device cloud polling with daily reconciliation and event-driven renewal/address handling, while retaining fast pending-job checks and bounded failure backoff. Cache certificate-worker credentials in memory for at most five minutes and invalidate them after authentication failures without replaying mutations.
