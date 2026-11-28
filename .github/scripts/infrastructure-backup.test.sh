#!/usr/bin/env bash
# DeviceRegistry backups: private, write-once >= 90 days, least-privilege jobs, alerted.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
template=${1:-}
fail() { echo "FAIL: $*" >&2; exit 1; }
if [[ -z "$template" ]]; then
  command -v az >/dev/null || { echo 'SKIP: backup template check needs Azure CLI (enforced by Infrastructure Required)'; exit 0; }
  mkdir -p "$root/build-host"
  work=$(mktemp -d "$root/build-host/backup-test.XXXXXX")
  trap 'rm -rf "$work"' EXIT
  template="$work/main.json"
  az bicep build --file "$root/infra/certificates/main.bicep" --outfile "$template" 2>/dev/null
fi
# Nested module templates by a resource they uniquely contain.
module() { jq -c --arg t "$1" '[.. | objects | select(.properties.template.resources? and (.properties.template.resources | (if type == "object" then [.[]] else . end) | any(.type == $t))) | .properties.template] | select(length == 1) | .[0] | .resources | (if type == "object" then [.[]] else . end)' "$template"; }
data=$(module Microsoft.Storage/storageAccounts/blobServices/containers/immutabilityPolicies)
[[ -n "$data" ]] || fail 'backup storage module not found once'
jq -e 'map(select(.type == "Microsoft.Storage/storageAccounts")) | length == 2 and all(.properties |
  .publicNetworkAccess == "Disabled" and .allowSharedKeyAccess == false and .allowBlobPublicAccess == false and
  .networkAcls.defaultAction == "Deny" and .networkAcls.bypass == "None" and .minimumTlsVersion == "TLS1_2")' <<<"$data" >/dev/null \
  || fail 'registry and backup accounts must both be private, Entra-only and TLS 1.2'
jq -e '(map(select(.type == "Microsoft.Storage/storageAccounts/blobServices/containers")) | length == 1 and .[0].properties.publicAccess == "None") and
  (map(select(.type == "Microsoft.Storage/storageAccounts/blobServices/containers/immutabilityPolicies"))[0].properties |
     .immutabilityPeriodSinceCreationInDays >= 90 and .allowProtectedAppendWrites == false) and
  (map(select(.type == "Microsoft.Storage/storageAccounts/managementPolicies"))[0].properties.policy.rules[0].definition.actions.baseBlob.delete.daysAfterCreationGreaterThan >= 90)' <<<"$data" >/dev/null \
  || fail 'backups must be private, write-once for at least 90 days and kept at least 90 days'
# Export writer: blob role on the container only, with delete removed by condition.
writer=$(jq -c 'map(select(.type == "Microsoft.Authorization/roleAssignments" and (.properties.roleDefinitionId | contains("blobWriter"))))' <<<"$data")
jq -e 'length == 1 and (.[0] | (.scope | contains("blobServices/containers")) and (.properties.principalId | contains("backupPrincipal")) and
  .properties.conditionVersion == "2.0" and (.properties.condition | test("^\\(!\\(ActionMatches\\{.Microsoft.Storage/storageAccounts/blobServices/containers/blobs/delete.\\}\\)\\)$")))' <<<"$writer" >/dev/null \
  || fail 'backup writer must be container-scoped with delete denied'
jq -e 'map(select(.type == "Microsoft.Authorization/roleAssignments" and (.properties.roleDefinitionId | contains("blobReader")))) |
  length == 1 and (.[0] | (.scope | contains("blobServices/containers")) and (.properties.principalId | contains("restorePrincipal")))' <<<"$data" >/dev/null \
  || fail 'only the restore identity reads backups'
# The export identity never writes the registry; only factory and restore do.
jq -e '[.[] | select(.type == "Microsoft.Authorization/roleAssignments" and (.properties.roleDefinitionId | contains("tableWriter")) and (.scope | contains("DeviceRegistry")))
  | .properties.principalId] | length == 2 and any(test("factoryPrincipal")) and any(test("restorePrincipal")) and (any(test("backupPrincipal")) | not)' <<<"$data" >/dev/null \
  || fail 'registry writers must be exactly factory and restore'
# Private endpoint and DNS zone for blob.
grep -q "{ name: 'blob', id: backupStorageId, zone: blobZone }" "$root/infra/certificates/endpoints.bicep" || fail 'backup account has no private blob endpoint'
grep -q "privatelink.blob" "$root/infra/certificates/network.bicep" || fail 'no private blob DNS zone'
# Jobs: daily scheduled export, manual restore that defaults to a dry run.
compute=$(module Microsoft.App/jobs)
jq -e 'map(select(.type == "Microsoft.App/jobs" and .properties.configuration.triggerType == "Schedule")) | length == 1 and (.[0] |
  (.properties.configuration.scheduleTriggerConfig.cronExpression | test("^[0-9]+ [0-9]+ \\* \\* \\*$")) and
  .properties.template.containers[0].command == ["python", "-m", "tempo_certificates.backup", "export"])' <<<"$compute" >/dev/null \
  || fail 'expected exactly one daily scheduled export job'
jq -e 'map(select(.type == "Microsoft.App/jobs" and (.properties.template.containers[0].command | index("restore")))) | length == 1 and (.[0] |
  .properties.configuration.triggerType == "Manual" and
  (.properties.template.containers[0].env | tostring | test("RESTORE_APPLY., .value., .no.\\)") and test("RESTORE_BLOB., .value., ..\\)")))' <<<"$compute" >/dev/null \
  || fail 'restore job must be manual and dry-run by default'
# Absence alert on the success event the job really logs.
ops=$(module Microsoft.Insights/actionGroups)
jq -e 'map(select(.type == "Microsoft.Insights/scheduledQueryRules" and .copy == null)) | length == 1 and (.[0].properties |
  .windowSize == "P1D" and .criteria.allOf[0].operator == "LessThan" and .criteria.allOf[0].threshold == 1 and
  (.criteria.allOf[0].query | test("registry_backup_completed")) and (.actions.actionGroups | length == 1))' <<<"$ops" >/dev/null \
  || fail 'missing-backup alert must fire when no export completed in a day'
grep -q 'LOG.info("registry_backup_completed ' "$root/services/certificates/tempo_certificates/backup.py" || fail 'service never logs registry_backup_completed'
echo 'PASS: registry backups are private, write-once >= 90 days, least-privilege, scheduled and alerted'
