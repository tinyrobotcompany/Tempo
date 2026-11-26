---
"tempo": patch
---

Restrict the infrastructure delivery identity's role administration with ABAC conditions. In each Tempo resource group it may assign only the runtime roles the workload template uses there, only to service principals and never to itself; network, ops and compute groups allow no role assignment at all. Owner and other privileged roles can no longer be granted by a compromised pipeline. CI checks the allowlist against the workload modules. Takes effect when an Azure operator re-runs the bootstrap; no firmware or device behaviour changes.
