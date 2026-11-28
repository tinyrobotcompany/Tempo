#!/usr/bin/env bash
# Alert rules match real service log events; the recipient is only a deployment input.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
template=${1:-}
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$root/build-host"
work=$(mktemp -d "$root/build-host/alerts-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
if [[ -z "$template" ]]; then
  # Standalone (test-all): Infrastructure Required always passes its compiled build.
  command -v az >/dev/null || { echo 'SKIP: alert template check needs Azure CLI (enforced by Infrastructure Required)'; exit 0; }
  template="$work/main.json"
  az bicep build --file "$root/infra/certificates/main.bicep" --outfile "$template" 2>/dev/null
fi
# Secure, required, no default: a missing secret cannot silently disable email.
jq -e '.parameters.alertEmail | .type == "securestring" and (has("defaultValue") | not) and .minLength >= 6' "$template" >/dev/null \
  || fail 'alertEmail must be a required secure parameter'
ops=$(jq -c '[.. | objects | select(.properties.template.variables.alerts?) | .properties] | select(length == 1) | .[0]' "$template")
[[ -n "$ops" ]] || fail 'ops module with alert definitions not found once'
input="[parameters('alertEmail')]"
jq -e --arg input "$input" '.parameters.alertEmail.value == $input' <<<"$ops" >/dev/null || fail 'ops does not receive the deployment input'
jq -e --arg input "$input" --arg count "[length(variables('alerts'))]" '.template.resources | (if type == "object" then [.[]] else . end) |
  (map(select(.type == "Microsoft.Insights/actionGroups")) | length == 1 and
     (.[0].properties.emailReceivers | length == 1 and .[0].emailAddress == $input)) and
  (map(select(.type == "Microsoft.Insights/scheduledQueryRules")) | length == 1 and
     (.[0] | .copy.count == $count and .properties.enabled == true and .properties.autoMitigate == true and
      (.properties.actions.actionGroups | length == 1) and (.properties.scopes | length == 1)))' <<<"$ops" >/dev/null \
  || fail 'expected one email action group and one enabled, actioned rule loop'
# Exact alert contract from the brief: events, thresholds and windows.
expected='[
  {"events":["worker_cycle_failed"],"minimum":1,"window":"PT30M"},
  {"events":["device_job_failed"],"minimum":3,"window":"PT1H"},
  {"events":["device_job_invalid"],"minimum":1,"window":"PT30M"},
  {"events":["nonce_cleanup_failed"],"minimum":1,"window":"PT30M"},
  {"events":["nonce_cleanup_budget_reached"],"minimum":1,"window":"PT30M"},
  {"events":["certificate_expiring_soon","certificate_expiry_check_failed"],"minimum":1,"window":"PT2H"},
  {"events":["ca_rate_limited"],"minimum":1,"window":"PT30M"}]'
jq -e --argjson want "$expected" '[.template.variables.alerts[] | {events, minimum, window}] == $want' <<<"$ops" >/dev/null \
  || fail 'alert events/thresholds differ from the reviewed contract'
jq -e --arg minimum "[variables('alerts')[copyIndex()].minimum]" '.template.resources | (if type == "object" then [.[]] else . end) | map(select(.type == "Microsoft.Insights/scheduledQueryRules"))[0].properties.criteria.allOf[0] |
  .operator == "GreaterThanOrEqual" and .threshold == $minimum and (.query | test("ContainerAppConsoleLogs_CL") and test("has_any"))' <<<"$ops" >/dev/null \
  || fail 'rule criteria do not count matching console log events'
# Every alerted event is actually logged by the service, so names cannot drift.
while IFS= read -r event; do
  grep -rqE "LOG\.warning\(\"$event( |\")" "$root/services/certificates/tempo_certificates" || fail "service never logs $event"
done < <(jq -r '.template.variables.alerts[].events[]' <<<"$ops")
# No recipient address is committed anywhere in infrastructure delivery.
if grep -EIrn '[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}' "$root/infra/certificates" "$root"/.github/workflows/infrastructure-*.yaml; then
  fail 'literal email address in infrastructure'
fi
# Both preview and apply take the secret through the environment into RUNNER_TEMP.
workflow="$root/.github/workflows/infrastructure-deploy.yaml"
[[ $(grep -c 'ALERT_EMAIL: \${{ secrets.ALERT_EMAIL }}' "$workflow") == 2 ]] || fail 'ALERT_EMAIL secret not given to preview and apply'
[[ $(grep -c -- '--parameters @"$RUNNER_TEMP/alert.parameters.json"' "$workflow") == 2 ]] || fail 'deployment commands do not use the alert parameters'
awk '/ALERT_EMAIL:\?Create/ { emit=1 } emit { print } /alert\.parameters\.json"\)$/ && emit { exit }' "$workflow" > "$work/parameters.sh"
[[ $(grep -c . "$work/parameters.sh") == 2 ]] || fail 'could not extract parameter-file commands'
if RUNNER_TEMP="$work" ALERT_EMAIL='' bash -eu "$work/parameters.sh" 2>/dev/null; then fail 'empty ALERT_EMAIL accepted'; fi
RUNNER_TEMP="$work" ALERT_EMAIL='ops@example.invalid' bash -eu "$work/parameters.sh"
jq -e '.parameters.alertEmail.value == "ops@example.invalid"' "$work/alert.parameters.json" >/dev/null || fail 'parameter file content'
[[ $(stat -c %a "$work/alert.parameters.json" 2>/dev/null || stat -f %Lp "$work/alert.parameters.json") == 600 ]] || fail 'parameter file is not private'
echo 'PASS: alert rules cover service events with reviewed thresholds; recipient is a required secret input'
