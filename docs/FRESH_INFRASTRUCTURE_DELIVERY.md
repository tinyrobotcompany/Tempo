# Tempo infrastructure delivery

Tempo uses the same delivery pattern as Voxa and DejaGroove: Bicep defines Azure
resources; GitHub Actions authenticates with OIDC and runs Azure CLI. There is no
Python infrastructure tooling and no resource migration workflow.

## One environment and predictable names

The sole environment is in West Europe. `project`, `location` and `generation`
in `infra/certificates/main.bicep` derive resource names. `generation` is stable
between deployments; changing it deliberately creates a new set of globally
unique resources, so it must never be randomized on each run. The fresh generation
uses a different naming seed from the deleted legacy foundation, avoiding the
soft-deleted legacy vault name without purging or weakening purge protection.

The five groups are `rg-${project}-${location}-{network,data,security,ops,compute}`.
The default project is `tempo`. The existing `rg-tempo-westeurope-security` group
and its `tempo-infrastructure-delivery` identity are retained. The bootstrap and
workload templates do not manage `rg-tinyrobotcompany` or `NetworkWatcherRG`.

Storage, registry and vault names derive from the same project/region/generation
seed. Consumers use Bicep module outputs, not copied resource IDs. Private DNS zone
names and built-in role IDs are Azure-defined constants, not resource naming inputs.

## One-time bootstrap after the user-deleted groups

Deleting the old groups also deleted their scoped pipeline permissions. The
preserved GitHub identity cannot create its own missing permissions. An authorised
Azure operator applies the reviewed bootstrap **after this code is merged**:

```sh
az deployment sub what-if \
  --subscription 923dd002-44bc-431c-b986-464d2ca524a2 \
  --location westeurope --template-file infra/certificates/bootstrap.bicep
az deployment sub create \
  --subscription 923dd002-44bc-431c-b986-464d2ca524a2 \
  --location westeurope --name tempo-bootstrap \
  --template-file infra/certificates/bootstrap.bicep \
  --query properties.outputs
```

This restores the five groups and their deployment permissions. It preserves the
existing identity and its corrected immutable GitHub federation subject. It does
not deploy the workload. Never grant subscription Owner or create a client secret.
The grants allow resource management in the five Tempo groups, plus subscription
deployment records. Role administration is ABAC-conditioned per group: the
identity may assign only the runtime roles `main.bicep` uses in that group, only
to service principals and never to itself, and may delete only those roles'
assignments. Groups whose workload assigns no roles (network, ops, compute) allow
no role administration. Owner, User Access Administrator and RBAC Administrator
are never delegable. CI fails if `delegableRoles` in `bootstrap.bicep` differs
from the roles the workload modules assign, so adding a runtime role needs both
changes in one reviewed PR and an operator re-run of bootstrap.

Re-running bootstrap updates the existing administrator assignments in place
(their names are unchanged) by adding the condition; nothing else changes.
Recipients are restricted by role and principal type, not by identity: runtime
identities are created by the workload after bootstrap, so they cannot be named
in the condition without a second bootstrap pass.

The `tempo-infrastructure` GitHub environment contains `AZURE_CLIENT_ID`,
`AZURE_TENANT_ID`, and `AZURE_SUBSCRIPTION_ID`. Use bootstrap outputs to verify
these values. If the identity survives, its client ID stays unchanged. The subject
is `repo:tinyrobotcompany@326183923/Tempo@1458582269:environment:tempo-infrastructure`.
Keep the main-only environment policy, required reviewer and disabled admin bypass.

## Normal deployment

1. Open a PR changing Bicep. PR checks compile templates and test the delivery
   guards, without Azure credentials.
2. The user merges the PR. Only changes matching `infra/certificates/**/*.bicep`
   on `main` start deployment. Main requires PRs and the existing required checks,
   without bypass. Script/workflow/docs/backend-only merges do not deploy Azure.
3. Approve the preview job. It checks subscription/tenant and current main, compiles
   Bicep and runs Azure what-if. The summary lists resource IDs and change types;
   it does not expose resolved secrets. A proposed resource deletion fails.
4. Inspect the preview, then approve apply. It verifies the same compiled artifact
   and current main, deploys it using `az deployment sub create`, and checks that
   storage and Key Vault remain private. This verifies control-plane configuration,
   not application-level private-network connectivity.
5. Review the output artifact. It contains generated names and endpoints, not keys.

All Azure CLI errors remain visible in the workflow logs. Concurrent deployments
queue rather than cancelling active writes. No local workload deployment script,
ordinary manual deployment trigger, resource move or deletion exists. Retry a
failed eligible run while its commit remains current main; a stale run requires
a new reviewed Bicep merge. Native GitHub path filters have a 300-file limit, so
keep infrastructure changes focused.

## Foundation, application image and activation

An empty `imageDigest` in main.bicep deploys the foundation only. The initial fresh
registry must exist before the image can be built. The image delivery workflow
builds the reviewed backend into that registry; its generated registry name must
come from the `containerRegistry` deployment output, never a legacy literal.
A subsequent reviewed Bicep change sets the exact 64-character image digest,
which deploys the API, worker and jobs with that immutable image. Both stages use
the same Azure environment. No placeholder or public sample image is deployed.

## CA account bootstrap

After compute is deployed, an operator adds three repository secrets once:

- `SPACESHIP_CREDENTIALS`: `{"key":"…","secret":"…"}`, the generated Spaceship API
  key and secret (not the key's display name), limited to DNS record read/write;
- `ACME_CONTACT_EMAIL`: the CA account contact address;
- `ACCEPTED_ACME_TERMS_URL`: the exact Let's Encrypt subscriber agreement
  URL the operator has read and accepts (the bootstrap fails if it differs from the
  CA's current agreement).

Then start **Certificate CA account bootstrap** from `main` and approve it. It
gives the credential to the private bootstrap job for one execution, which stores
it and the ACME account in Key Vault. The credential is removed from the job
afterwards, also on failure. Re-run it after switching `acmeEnvironment` or
rotating the DNS credential; the account key is reused.

### Switching to production Let's Encrypt

`acmeEnvironment` is `production`. Firmware trusts only ISRG Root X1/X2, so
devices reject staging certificates. Production is a separate CA with its own
account. The bootstrap job reuses the existing account key from Key Vault,
registers it with the production CA and **replaces `acme-account-uri`**. The
worker refuses to issue with an account URI from the other CA, so certificate
jobs fail (and alert as `device_job_failed`) until the bootstrap re-run below
has succeeded. Do the steps back to back:

1. Confirm `ACCEPTED_ACME_TERMS_URL` still equals the production directory's
   `meta.termsOfService` (`https://acme-v02.api.letsencrypt.org/directory`).
   On 28 Nov 2026 staging and production both gave
   `https://letsencrypt.org/documents/LE-SA-v1.8-July-06-2026.pdf`. If it
   differs, read the new agreement and update the secret first.
   `SPACESHIP_CREDENTIALS` and `ACME_CONTACT_EMAIL` stay as they are.
2. Merge the reviewed change, then approve preview and apply. For this change alone, preview shows
   updates to the worker and bootstrap job (`ACME_ENVIRONMENT`), nothing else.
3. Immediately start **Certificate CA account bootstrap** from `main` and
   approve it. It must succeed once.
4. Verify read-only: the bootstrap job execution `Succeeded`; the worker logs
   no `worker_cycle_failed`/`device_job_failed`; a test device's status shows a
   certificate whose issuer chains to ISRG Root X1 or X2.
5. Jobs that failed in the window back off (5 minutes and up) and retry by
   themselves. A device that used all four attempts sends a fresh signed
   request after the one-day error cooldown.

**No working rollback yet.** A bootstrap re-run against a CA where the key is
already registered fails: acme's `new_account` raises `ConflictError`, which
`bootstrap.py` does not handle. That covers going back to staging, and also a
second production run (for example, rotating the DNS credential: the new
credential is stored, then the job fails). Key Vault is private, so an operator
cannot restore the earlier `acme-account-uri` version by hand. Make the
bootstrap idempotent (use the existing account's URI on conflict) before
relying on a rollback or a re-run.

ACME `staging` means test certificates, not an additional Azure environment. Live
certificate issuance requires DNS credentials, CA account/terms setup and the
production CA mode above. These secrets are not workflow inputs or
Bicep outputs. Application startup, private access, issuance, renewal and actual
phone/device acceptance must be verified before declaring HTTPS operational.

The old migration pipeline is retired: the user deleted the old foundation. Do
not rerun historic migration workflows or restore the old resource-name allowlist.
