---
"tempo": patch
---

Add a separate manually dispatched, approval-gated migration for the existing Tempo resource groups. Preserve automatic infrastructure deployment and its change filters from the prerequisite PR; never migrate resources on a push event.
