# Azure delivery role boundaries

Scope: the single Tempo foundation in subscription `923dd002-44bc-431c-b986-464d2ca524a2`, with separate network, data, security, operations and compute groups. No new environment, runtime identity, network or backend resource is introduced.

The existing deployment principal remains Contributor within those groups. Its existing RBAC Administrator assignment IDs are updated in place with Azure ABAC condition version 2.0. Creating a second conditional assignment would leave the old unrestricted grant effective and is prohibited.

| Group | Permitted role | Exact recipients |
|---|---|---|
| Data | Storage Table Data Reader | Existing API and worker |
| Data | Storage Table Data Contributor | Existing API, worker and factory |
| Data | AcrPull | Existing API, worker, factory and bootstrap |
| Security | Key Vault Secrets User | Existing worker only |
| Security | Key Vault Secrets Officer | Existing bootstrap only |
| Network, ops, compute | None | No role delegation |

Each condition pairs the role with its recipients. It checks request attributes on assignment and resource attributes on deletion. GUIDs are resolved from the existing four managed identities, not arbitrary service principals or the deployment principal. Contributor is not expanded to Owner or subscription-wide resource access.

## Apply and verify

An authorized Azure operator must review the bootstrap from merged main, preview it, then apply it. Before migration, explicitly enable source-group grants and select the old security group. After migration, use the defaults to resolve the same identities in their new group, avoid the deleted source groups and install storage/vault CanNotDelete locks. Preserve identity GUIDs. Normal pipeline deployment cannot remove those locks; intentional resource retirement requires a separate operator action.

After application, inspect **all effective role assignments**, including inherited and additional assignments, for the delivery principal. Every assignment with role-assignment write/delete permissions must be conditional. Confirm the expected condition and recipient GUIDs at every Tempo scope. Then run an approved deployment that reconciles the existing runtime permissions. Verify that attempts to grant Owner, grant a runtime role to the delivery principal or another principal, grant Secrets Officer to the API, or delete a disallowed role assignment are denied. Perform authorization probes only against disposable test assignments with cleanup; never delete a live runtime grant to test denial.

Read-only Azure what-if on 2026-11-25 returned Succeeded, 2 Modify, 7 NoChange and 20 Unsupported entries. It proposed no resource create/delete. The modifications include the already-merged OIDC subject correction. Unsupported role assignments mean **effective condition enforcement remains unverified**; this preview is not a successful deployment. No restrictions or locks were applied during implementation.

## Evidence and residual risk

Verified from compiled templates: both assignment/removal are constrained; original grant IDs are retained; migration grants are explicit; locks only target the existing migrated storage and vault. CI checks the compiled conditions, not just source strings.

Verified platform guidance: [delegated role conditions](https://learn.microsoft.com/en-us/azure/role-based-access-control/delegate-role-assignments-examples) distinguish request/resource attributes; [resource locks](https://learn.microsoft.com/en-us/azure/azure-resource-manager/management/lock-resources) protect control-plane operations, not data-plane entity deletion. [Identity guidance](https://learn.microsoft.com/en-us/azure/well-architected/security/identity-access) supports narrowly scoped workload permissions.

Derived limitation: the pipeline can still replace worker code or attach permitted managed identities, so environment approval and reviewed image provenance remain essential. A lock is not a registry backup, nor a defense against deleting Table entities through an authorized identity. Isolated registry export/restore verification and audit/alerts remain separate required work. Bootstrap Secrets Officer remains narrowly assigned but must be retired after secret initialization under the bootstrap lifecycle follow-up.

Source coverage was intentionally limited to identity/delegation and resource locks. Network, CA issuance, DNS delegation, physical manufacturing and application authentication were not reassessed in this change. Live effective permissions and negative authorization tests are the outstanding evidence, owned by the authorized deployment operator.
