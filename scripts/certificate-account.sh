#!/usr/bin/env bash
# One-shot CA account bootstrap: hand DNS credentials and the operator's accepted
# ACME terms to the private bootstrap job, which stores them in Key Vault.
# The credential exists in the job only for this run and is always removed after.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ "${GITHUB_ACTIONS:-}" == true && "${GITHUB_EVENT_NAME:-}" == workflow_dispatch && "${GITHUB_REF:-}" == refs/heads/main ]] || { echo 'Expected manual run on main' >&2; exit 1; }
[[ "${GITHUB_REPOSITORY:-}" == tinyrobotcompany/Tempo ]] || exit 1
: "${AZURE_SUBSCRIPTION_ID:?}" "${AZURE_TENANT_ID:?}" "${GITHUB_SHA:?}"
[[ $(az account show --query id -o tsv) == "$AZURE_SUBSCRIPTION_ID" && $(az account show --query tenantId -o tsv) == "$AZURE_TENANT_ID" ]] || { echo 'Wrong Azure context' >&2; exit 1; }
[[ $(git ls-remote origin refs/heads/main | cut -f1) == "$GITHUB_SHA" ]] || { echo 'Main changed since this run started' >&2; exit 1; }
[[ "${ACME_CONTACT_EMAIL:-}" =~ ^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$ ]] || { echo 'Set environment variable ACME_CONTACT_EMAIL' >&2; exit 1; }
[[ "${ACCEPTED_ACME_TERMS_URL:-}" =~ ^https://letsencrypt\.org/documents/[A-Za-z0-9._-]+\.pdf$ ]] || { echo 'Set environment variable ACCEPTED_ACME_TERMS_URL to the agreement you accepted' >&2; exit 1; }
# Validate without printing; pass the unmodified secret so GitHub's log masking applies.
jq -e 'type == "object" and (keys == ["key", "secret"]) and (.key | type == "string" and length > 0) and (.secret | type == "string" and length > 0)' \
  <<<"${SPACESHIP_CREDENTIALS:-}" >/dev/null 2>&1 || { echo 'Set environment secret SPACESHIP_CREDENTIALS to {"key":"...","secret":"..."}' >&2; exit 1; }
rg=$(az deployment sub show --subscription "$AZURE_SUBSCRIPTION_ID" --name tempo-infrastructure --query properties.outputs.computeResourceGroup.value -o tsv)
[[ "$rg" =~ ^rg-[a-z0-9]{2,8}-[a-z0-9]+-compute$ ]] || { echo 'Compute is not deployed' >&2; exit 1; }
job=$(az containerapp job list --subscription "$AZURE_SUBSCRIPTION_ID" -g "$rg" --query "[?ends_with(name, '-bootstrap')].name" -o tsv)
[[ "$job" =~ ^[a-z0-9-]+-bootstrap$ ]] || { echo 'Expected exactly one bootstrap job' >&2; exit 1; }
cleanup() {
  status=$?
  # Remove the reference before the secret, even after failure. The non-secret
  # contact/terms values are reset to Bicep's empty values on the next deployment.
  az containerapp job update --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" \
    --remove-env-vars SPACESHIP_CREDENTIALS -o none || status=1
  az containerapp job secret remove --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" --secret-names dns-bootstrap --yes -o none || status=1
  [[ $status == 0 ]] && echo 'Temporary bootstrap credential removed from the job.' || echo 'CLEANUP OR BOOTSTRAP FAILED: check the job has no dns-bootstrap secret.' >&2
  exit $status
}
trap cleanup EXIT
az containerapp job secret set --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" --secrets "dns-bootstrap=$SPACESHIP_CREDENTIALS" -o none
az containerapp job update --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" -o none \
  --set-env-vars "ACME_CONTACT_EMAIL=$ACME_CONTACT_EMAIL" "ACCEPTED_ACME_TERMS_URL=$ACCEPTED_ACME_TERMS_URL" SPACESHIP_CREDENTIALS=secretref:dns-bootstrap
execution=$(az containerapp job start --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" --query name -o tsv)
[[ "$execution" =~ ^[a-z0-9-]+$ ]] || { echo 'Job did not start' >&2; exit 1; }
for _ in $(seq 1 "${BOOTSTRAP_POLLS:-60}"); do
  state=$(az containerapp job execution show --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" --job-execution-name "$execution" --query properties.status -o tsv)
  case "$state" in
    Succeeded) printf '## CA account bootstrap\n\nJob `%s` execution `%s` succeeded. DNS credentials and ACME account are in Key Vault.\n' "$job" "$execution" >> "${GITHUB_STEP_SUMMARY:-/dev/null}"; exit 0 ;;
    Failed|Stopped|Degraded) echo "Bootstrap execution $execution $state; see the job's Log Analytics console logs." >&2; exit 1 ;;
  esac
  sleep "${BOOTSTRAP_POLL_SECONDS:-10}"
done
echo "Bootstrap execution $execution did not finish in time" >&2
exit 1
