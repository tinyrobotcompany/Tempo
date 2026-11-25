# Tempo infrastructure delivery

## Authority and current state

All infrastructure changes are reviewed as code. GitHub Actions is the normal
preview/deployment/migration path. Local scripts refuse cloud operations outside
a main-branch push or explicitly dispatched workflow on `tinyrobotcompany/Tempo` main. This guard
prevents accidents; Azure RBAC and GitHub environment protection enforce access.

There is ONE Azure environment in Tiny-Robot-Company subscription
`923dd002-44bc-431c-b986-464d2ca524a2`, tenant
`17ca2540-dd3e-4204-b2f7-a3e3ad209719`, West Europe. The five groups are
`rg-tempo-westeurope-{network,data,security,ops,compute}`. No integration group is
needed. The `acmeEnvironment` setting chooses test/live certificates and does not
create a separate Azure environment. Production activation is a separate decision
from establishing the delivery pipeline.

Existing resources were originally deployed directly from the feature branch.
They remain in `rg-tempo-staging-westeurope-*` until the migration workflow is
approved and succeeds. Individual resource names retain `staging` deliberately:
the user requested resource-group names to change, not resource replacement.
The existing unique-name salt is retained so storage and vault are not replaced.

## Pull request validation

`ci.yaml` detects infrastructure changes using the complete Git diff (including
renames/deletions), then calls `infrastructure-ci.yaml` for Bicep compilation and
guard tests only when relevant files changed. Unrelated firmware/docs changes
skip that job. The aggregate accepts a skip only when detection succeeded and
reported no infrastructure changes; failures and unexpected skips remain blocking.
The existing `CI Required` aggregate requires infrastructure success as well as
all existing tests, changeset and conditional firmware-build gates. Fork PR code
receives no Azure credentials, OIDC permission or infrastructure environment.
Every external action is pinned by commit SHA. Normal host tests include the
infrastructure guard tests. An infrastructure changeset is included.

## One-time authentication bootstrap

GitHub cannot create its first Azure identity before it has Azure access. This
one-time setup is explicit, separately reviewed and captured in
`infra/certificates/bootstrap.bicep`; it is not an undocumented application
provisioning script. An authorised operator applies the reviewed template after
preview, through an authenticated Azure administration session. Do not apply it
from an unreviewed branch. It creates only the five destination groups, delivery
identity, federation and delivery permissions. It neither moves existing resources
nor starts the application. Never grant subscription Owner or put an Azure client
secret in GitHub.

Read-only bootstrap what-if was checked on 2026-11-24: Azure reported success,
9 Create entries and 20 Unsupported permission entries. Unsupported entries are
not verified grants: review those Bicep assignments explicitly, then verify the
resulting scopes after bootstrap. Compilation alone is not Azure validation.

The bootstrap grants subscription deployment-record operations plus Contributor
and RBAC Administrator scoped to the ten exact source/destination Tempo groups.
RBAC administration is needed to restore the narrowly scoped runtime assignments.
This is a privileged deployment identity: it can modify Tempo resources/access,
but is not a runtime identity and has no subscription-wide resource writer grant.
The source-group grants disappear when those groups are deleted. Bootstrap is
not a recurring deployment and cannot be reapplied unchanged after source removal.

GitHub environment `tempo-infrastructure` must have:

- Required reviewer `simonholmes001`; admin bypass disabled.
- Custom deployment-branch policy allowing only the branch `main` (no tag rule).
- Self-review allowed so the sole operator can approve their explicitly dispatched
  run; this is an explicit review action, not automatic approval.
- Variables `AZURE_CLIENT_ID`, `AZURE_TENANT_ID`, `AZURE_SUBSCRIPTION_ID` from the
  reviewed bootstrap outputs. These identifiers are public metadata, not secrets.

Federation accepts ONLY issuer `https://token.actions.githubusercontent.com`,
audience `api://AzureADTokenExchange`, and subject
`repo:tinyrobotcompany@326183923/Tempo@1458582269:environment:tempo-infrastructure`.
This matches this repository's `use_immutable_subject: true` configuration. Before
bootstrap, compare `gh api repos/tinyrobotcompany/Tempo/actions/oidc/customization/sub`
with the template: its `sub_claim_prefix` plus `:environment:tempo-infrastructure`
must match exactly. Do not disable immutable subjects to make a legacy template work.
The login attempt in run `45629123208` confirmed this subject on 2026-11-25;
the original name-only federation was rejected before any migration operations.
Never add a wildcard
branch/PR subject. If environment protections are unavailable, stop; do not replace
OIDC with a long-lived password or remove approval.

## Preview and apply

On pushes to main, **Infrastructure deployment** starts only when a file matching
`infra/certificates/**/*.bicep` changes. The main ruleset must require pull requests,
without bypass, so deployment follows a reviewed merge. A second full-diff check
selects only Bicep changes, including deleted/renamed files. Application, docs,
workflow, script and JSON-only changes do not trigger deployment. PR validation
still covers the wider infrastructure inputs. GitHub native path filtering has a
300-file diff limit; keep infrastructure PRs focused.

There is no ordinary manual deployment trigger. Retry a failed eligible run using
GitHub's Re-run jobs, while its commit remains current main. Migration keeps its
separate one-time manual workflow. A stale run needs a new reviewed Bicep change.

Deployment still reads `infra/certificates/deployment.json`: an empty image means
foundation-only, and ACME `staging` means test certificates, not a second Azure
environment. A JSON-only configuration change cannot deploy. Before application
activation, move the selected image/CA configuration into reviewed Bicep so that
an activation change follows the same Bicep merge rule. No automatic trigger
silently activates the backend.

The workflow validates before Azure login. Existing GitHub environment approval
is preserved for preview access and again for apply after the artifact exists.
Thus automatic triggering is not automatic permission to change Azure.

Merge this automation PR first, then PR #26 (the one-time migration). After those
PRs are merged by the user and bootstrap is complete:

1. For this existing foundation, first run **Infrastructure migration** from
   `main` (provided by PR #26). Its preview inventories the resources; its separately
   approved apply moves and reconciles them before cleanup. Do not run a normal
   foundation deployment first: even with compute disabled it would try to create
   destination copies of existing resources. Normal `preview` and `deploy` refuse
   while any old group remains. After migration, normal Bicep merges generate a deployment preview.
2. Approve access to the infrastructure environment. The job validates checkout,
   current main SHA, subscription and tenant, compiles Bicep and runs Azure what-if.
3. Review the immutable plan artifact, compiled template, parameters and diagnostics.
   ARM can short-circuit role assignments derived from identity outputs; the preview
   must not be described as proof of complete runtime connectivity or access.
4. For automatic deployment or migration, a SECOND environment approval protects the apply job
   after its preview exists. The same GitHub environment is used for both gates;
   there are not two Azure environments.
5. Apply uses the exact commit and artifact. Normal deployment verifies file hashes,
   repeats what-if and refuses changed plans. An advanced main branch requires a
   fresh run. Concurrent infrastructure runs queue instead of canceling active writes.
6. Review retained results and private-access policy checks. Actual device/browser,
   CA issuance, cold-start and private-network runtime checks remain release gates.

An empty image means foundation only. Application deployment requires an explicit
immutable `acrtempo22bw55qnmg6pm.azurecr.io/tempo-certificates@sha256:...` image.
This PR does not certify the earlier image for production or implement the pending
service security changes. The API template scales from zero to three; the worker
remains one replica. Cold-start acceptance is required before customer activation.

No secrets are accepted as workflow inputs. DNS credentials and CA account setup
remain a separate controlled operation; deployment approval is not acceptance of
CA terms. Do not log private key material, credential-bearing requests or resolved
workspace keys. Preview artifacts redact sensitive values and retain hashes for
change detection. Artifact retention is 14 days.

## Resource-group migration

Run **Infrastructure migration** manually after PR #26 is merged. This is restricted to the inspected, inactive foundation. It
refuses unknown resources, missing/duplicated expected resources, a running compute
stack, changed source layout or unexpected resource-scoped permissions. Existing
application groups elsewhere in the subscription are out of scope.

The preview lists the exact source/destination groups. Only the two private
endpoints and their automatically managed NIC/DNS bindings are recreated, because
private-link moves have provider restrictions. Public access remains disabled.
Storage, vault, registry, logs and four runtime identities move in place. Azure
validates each move after endpoint detachment. Unsupported moves stop the run;
no source group is deleted on failure. The reviewed foundation template then
recreates private connections and the eleven original runtime role assignments.

Verification checks preserved identity principal IDs, role scopes, endpoint
approval/provisioning, DNS-group bindings and private storage/vault policies.
Only then are the five old groups checked empty and deleted. There is no wildcard
resource-group deletion command. A successful run checks that no source group
remains. Permissions and network configuration are checked in the control plane;
this is not a claim that live application connectivity was tested.

On failure, download the plan/progress artifacts. Do not delete groups or keys.
Inspect resource locations, resolve the provider error and generate a fresh plan.
The migration supports the expected resources split across source/destination
names, so it can resume without duplicating data. If a resource was lost or its
identity changed, stop for recovery. Restoring endpoints at their currently
correct resource IDs may be necessary before service activation. This migration
is designed before application startup, not as a zero-downtime fleet migration.

## Dependencies and outstanding acceptance

This delivery PR is independent of the firmware and can target main directly.
PR #26 depends on this PR and adds only the explicitly dispatched migration path.
Application acceptance and security hardening remain separate work.
Do not activate the service merely because infrastructure validation passes.

No infrastructure workflow is automatically dispatched by pushing a feature
branch. No PR is merged by this workflow. Budgets/alert recipients, restricted
DNS authority, CA terms and device acceptance still require their explicit
configuration. Keep the user-approved one-environment constraint.
