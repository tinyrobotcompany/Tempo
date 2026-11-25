---
"tempo": patch
---

Add a separate manually dispatched, approval-gated migration for the existing Tempo resource groups. Preserve automatic infrastructure deployment and its change filters from the prerequisite PR; never migrate resources on a push event. If a migration fails after private endpoints are removed, recreate any missing endpoint and DNS binding against current resource locations, retain every source group, record the outcome in the recovery evidence and report the original failure. Preview and apply both require every destination group to exist in westeurope before any endpoint is removed.
