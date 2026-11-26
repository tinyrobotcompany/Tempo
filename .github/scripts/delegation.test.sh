#!/usr/bin/env bash
# Delivery role delegation must equal exactly the roles the workload assigns per group.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
bootstrap=${1:-}
if [[ -z "$bootstrap" ]]; then
  # Standalone (test-all): compile the reviewed template; Infrastructure Required
  # always passes its own compiled build, so a host without Azure CLI may skip here.
  command -v az >/dev/null || { echo 'SKIP: delegation check needs Azure CLI (enforced by Infrastructure Required)'; exit 0; }
  build="$root/build-host/infrastructure"
  mkdir -p "$build"
  bootstrap="$build/bootstrap-delegation.json"
  az bicep build --file "$root/infra/certificates/bootstrap.bicep" --outfile "$bootstrap" 2>/dev/null
fi
dir="$root/infra/certificates"
concerns=(network data security ops compute)
fail() { echo "FAIL: $*" >&2; exit 1; }
# module file -> group index from main.bicep, e.g. "data.bicep 1".
modules=$(awk "/^module /{gsub(/'/,\"\",\$3); file=\$3} /scope: groups\\[/{match(\$0,/[0-9]+/); print file, substr(\$0,RSTART,RLENGTH)}" "$dir/main.bicep")
[[ $(wc -l <<<"$modules") -ge 5 ]] || fail "could not map main.bicep modules to groups"
for i in "${!concerns[@]}"; do
  concern=${concerns[$i]}
  used=$(awk -v i="$i" '$2 == i {print $1}' <<<"$modules" | while read -r file; do
    grep -ho "roleDefinitions', '[0-9a-f-]\{36\}'" "$dir/$file" | grep -o '[0-9a-f-]\{36\}' || true
  done | sort -u)
  allowed=$(jq -r --arg c "$concern" '.variables.delegableRoles[$c][]' "$bootstrap" | sort -u)
  [[ "$used" == "$allowed" ]] || fail "$concern delegable roles [$allowed] differ from workload roles [$used]"
done
# Owner, User Access Administrator and RBAC Administrator can never be delegated.
for privileged in 8e3af657-a8ff-443c-a75c-2fe8c4bcb635 18d7d88d-d35e-4fb5-a5c3-7773c4a72d9d f58310d9-a9f6-439a-9e8d-f62e7b41a168; do
  jq -e --arg r "$privileged" '[.variables.delegableRoles[][]] | index($r) == null' "$bootstrap" >/dev/null || fail "privileged role $privileged is delegable"
done
# Every administrator grant carries an ABAC condition; Contributor stays unconditioned.
permissions=$(jq -c '[.. | objects | select(.properties.template.parameters.delegableRoles?) | .properties.template]' "$bootstrap")
[[ $(jq length <<<"$permissions") == 1 ]] || fail "delivery-permissions template not found once"
jq -e '.[0] | .variables.administrator == "f58310d9-a9f6-439a-9e8d-f62e7b41a168" and .variables.contributor == "b24988ac-6180-42a0-ab88-20f7382dd24c" and
  (.resources | (if type == "object" then [.[]] else . end) | map(select(.type == "Microsoft.Authorization/roleAssignments")) | length == 2 and
  (map(select(.properties.roleDefinitionId | contains("variables(\u0027administrator\u0027)"))) | length == 1 and (.[0].properties | .conditionVersion == "2.0" and (.condition | test("noDelegation") and test("limitedDelegation")))) and
  (map(select(.properties.roleDefinitionId | contains("variables(\u0027contributor\u0027)"))) | length == 1 and (.[0].properties.condition == null)))' <<<"$permissions" >/dev/null \
  || fail "administrator grant is not conditioned"
jq -e '.[0].variables.limitedDelegation | test("RoleDefinitionId\\] ForAnyOfAnyValues:GuidEquals") and test("PrincipalType\\] ForAnyOfAnyValues:StringEqualsIgnoreCase") and test("PrincipalId\\] ForAnyOfAllValues:GuidNotEquals") and test("@Resource\\[Microsoft.Authorization/roleAssignments:RoleDefinitionId\\]")' <<<"$permissions" >/dev/null \
  || fail "limited delegation condition lost a restriction"
echo 'PASS: delivery delegation matches workload roles, excludes privileged roles and is ABAC-conditioned'
