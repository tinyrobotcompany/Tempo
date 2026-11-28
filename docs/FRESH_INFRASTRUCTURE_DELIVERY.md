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

## Alert recipient

Before merging the first Bicep change after alerts were added, create the
repository secret `ALERT_EMAIL` containing one email address (Settings → Secrets
and variables → Actions → New repository secret). Preview and apply both fail
with "Create the ALERT_EMAIL repository secret" while it is missing, so no
deployment can silently drop alert delivery. The address is passed to Bicep as
the secure `alertEmail` parameter through a parameters file in the runner's
temporary directory. It is never in the repository, a command line, the
uploaded plan or the deployment outputs. Azure sends a "you have been added to
an action group" email to it after the first apply. To change the recipient,
update the secret, then merge any reviewed Bicep change (or wait for the next).

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

## DeviceRegistry backup and restore

`DeviceRegistry` holds the manufacturing admissions (public identity keys,
hostnames, serials, enabled flags). It cannot be rebuilt from devices, so it is
exported daily to a separate account:

- `stbk<project><unique>` in the data group: geo-redundant (GRS), public network
  access disabled, shared keys disabled, reachable only through the
  `tempo-westeurope-blob-endpoint` private endpoint and the `privatelink.blob`
  DNS zone;
- container `device-registry`: a time-based immutability policy keeps every
  backup write-once (no overwrite or delete) for 90 days; lifecycle management
  deletes backups after 365 days; blob and container soft delete keep 30 days;
- `tempo-westeurope-registry-backup` job runs `03:17 UTC` daily as the `backup`
  identity: Table Data Reader on `DeviceRegistry` and Blob Data Contributor on
  that one container, with an ABAC condition removing blob delete. It cannot
  write the registry or remove older backups;
- `tempo-westeurope-registry-restore` is a manual job under a separate `restore`
  identity: Blob Data Reader on the container and Table Data Contributor on
  `DeviceRegistry`. It is a dry run unless told otherwise and only ever adds
  missing rows.

Alerts: `registry_backup_failed` (any failed export) and "no
`registry_backup_completed` in 24 hours". The second fires until the first
export succeeds, so expect it after the first deployment.

**Activation order.** These need two new runtime roles in the data group
(Storage Blob Data Contributor and Reader), added to `delegableRoles` in
`bootstrap.bicep`. After merging, and before approving the infrastructure
preview, an authorised operator re-runs bootstrap as described above; the
deployment's role assignments otherwise fail. The jobs run the backup module
from the image, so they work only after an image built from this code is
selected through a reviewed `imageDigest` change.

The immutability policy is left **unlocked** so a mistaken setting can still be
corrected. Locking it is irreversible (the account cannot then be deleted until
the newest backup is 90 days old); do it manually only after the first backups
are verified.

### Restore procedure

Restore is an operator action on the live service. Run each command with
`--subscription 923dd002-44bc-431c-b986-464d2ca524a2 -g rg-tempo-westeurope-compute
-n tempo-westeurope-registry-restore`, and read the job output in Log Analytics:
`ContainerAppConsoleLogs_CL | where ContainerJobName_s == 'tempo-westeurope-registry-restore'`.

1. If the storage account or table itself was deleted, first let the normal
   reviewed deployment recreate it (an empty `DeviceRegistry`).
2. List backups: `az containerapp job start …`. With no backup selected the job
   prints the ten newest backup names and changes nothing.
3. Dry run: `az containerapp job update … --set-env-vars RESTORE_BLOB=<name>`,
   then `az containerapp job start …`. The output gives `missing`, `differing`
   and `identical` counts. Before using any row, the job checks that each ID and
   hostname derives from its public key, and rejects the whole backup if any
   does not.
4. Apply: `az containerapp job update … --set-env-vars RESTORE_APPLY=yes`, then
   start it again. Only missing rows are created. Existing rows always win, so a
   device disabled after the backup stays disabled. `differing` rows are
   reported for a manual decision and never changed.
5. Reset: `az containerapp job update … --set-env-vars RESTORE_BLOB= RESTORE_APPLY=no`.
   The next infrastructure deployment also resets both values.

Restore does not touch `DeviceState`. Devices request their certificate and
address state again on their next check-in.

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

ACME `staging` means test certificates, not an additional Azure environment. Live
certificate issuance requires DNS credentials, CA account/terms setup and a
reviewed change to production CA mode. These secrets are not workflow inputs or
Bicep outputs. Application startup, private access, issuance, renewal and actual
phone/device acceptance must be verified before declaring HTTPS operational.

The old migration pipeline is retired: the user deleted the old foundation. Do
not rerun historic migration workflows or restore the old resource-name allowlist.
