---
"tempo": patch
---

Add reviewed infrastructure validation and approval-gated Azure deployment and resource-group migration workflows, using OIDC and one Tempo environment.

No firmware flashing or automatic cloud activation. Resource-group migration preserves existing data and requires reviewed bootstrap plus GitHub approval. Validation: Bicep compilation, workflow lint and deployment/migration guard tests.
