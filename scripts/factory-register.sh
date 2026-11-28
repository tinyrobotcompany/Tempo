#!/usr/bin/env bash
# Admit one device's PUBLIC identity key to DeviceRegistry through the private
# factory job (tempo_certificates.manufacturing). Accepts only a public key and a
# serial. Runs from the manual GitHub workflow on main, or locally only with
# --i-am-the-operator. The values are set on the job for this run only and are
# always removed afterwards, including after failure.
#
#   GitHub:  MANUFACTURING_PUBLIC_KEY_B64, MANUFACTURING_SERIAL, EXPECTED_DEVICE_ID
#   Local:   bash scripts/factory-register.sh --i-am-the-operator \
#              --public-key identity-public.pem --serial SERIAL --device-id ID
set -euo pipefail
cd "$(dirname "$0")/.."
operator=0 key_file='' serial="${MANUFACTURING_SERIAL:-}" expected="${EXPECTED_DEVICE_ID:-}"
while (($#)); do
  case "$1" in
    --i-am-the-operator) operator=1 ;;
    --public-key) key_file=${2:?}; shift ;;
    --serial) serial=${2:?}; shift ;;
    --device-id) expected=${2:?}; shift ;;
    *) echo "Unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
if [[ "${GITHUB_ACTIONS:-}" == true ]]; then
  [[ "${GITHUB_EVENT_NAME:-}" == workflow_dispatch && "${GITHUB_REF:-}" == refs/heads/main ]] || { echo 'Expected manual run on main' >&2; exit 1; }
  [[ "${GITHUB_REPOSITORY:-}" == tinyrobotcompany/Tempo ]] || exit 1
  : "${GITHUB_SHA:?}"
  [[ $(git ls-remote origin refs/heads/main | cut -f1) == "$GITHUB_SHA" ]] || { echo 'Main changed since this run started' >&2; exit 1; }
  ((operator == 0)) || { echo '--i-am-the-operator is for local runs only' >&2; exit 1; }
elif ((operator == 0)); then
  echo 'Refusing: run the "Factory device registration" workflow, or locally add --i-am-the-operator.' >&2; exit 1
fi
: "${AZURE_SUBSCRIPTION_ID:?}" "${AZURE_TENANT_ID:?}"

if [[ -n "$key_file" ]]; then
  [[ -f "$key_file" && $(wc -c <"$key_file") -le 1024 ]] || { echo 'Public key file missing or too large' >&2; exit 1; }
  pem=$(cat "$key_file")
else
  b64=${MANUFACTURING_PUBLIC_KEY_B64:-}
  [[ "$b64" =~ ^[A-Za-z0-9+/=]+$ && ${#b64} -ge 100 && ${#b64} -le 1400 ]] || { echo 'Set MANUFACTURING_PUBLIC_KEY_B64 to the base64 PEM printed by factory-lockdown.mjs' >&2; exit 1; }
  pem=$(base64 -d <<<"$b64" 2>/dev/null) || { echo 'Public key is not valid base64' >&2; exit 1; }
fi
# Only a single P-256 PUBLIC key is accepted; anything private is refused unread.
if grep -qi private <<<"$pem"; then echo 'Refusing: that is not a public key. Do not share it.' >&2; exit 1; fi
[[ $(grep -c -- '-----BEGIN PUBLIC KEY-----' <<<"$pem") == 1 ]] || { echo 'Expected exactly one PEM public key' >&2; exit 1; }
openssl pkey -pubin -noout -text <<<"$pem" 2>/dev/null | grep -q 'prime256v1\|P-256' || { echo 'Not an EC P-256 public key' >&2; exit 1; }
[[ "$serial" =~ ^[A-Za-z0-9._-]{1,64}$ ]] || { echo 'Serial must be 1-64 letters, digits, dot, underscore or dash' >&2; exit 1; }
id=$(openssl pkey -pubin -outform DER <<<"$pem" | shasum -a 256 | cut -c1-32)
[[ "$expected" == "$id" ]] || { echo "Device ID $id does not match the expected ID; check you copied the right unit's key" >&2; exit 1; }

[[ $(az account show --query id -o tsv) == "$AZURE_SUBSCRIPTION_ID" && $(az account show --query tenantId -o tsv) == "$AZURE_TENANT_ID" ]] || { echo 'Wrong Azure context' >&2; exit 1; }
rg=$(az deployment sub show --subscription "$AZURE_SUBSCRIPTION_ID" --name tempo-infrastructure --query properties.outputs.computeResourceGroup.value -o tsv)
[[ "$rg" =~ ^rg-[a-z0-9]{2,8}-[a-z0-9]+-compute$ ]] || { echo 'Compute is not deployed' >&2; exit 1; }
job=$(az containerapp job list --subscription "$AZURE_SUBSCRIPTION_ID" -g "$rg" --query "[?ends_with(name, '-factory')].name" -o tsv)
[[ "$job" =~ ^[a-z0-9-]+-factory$ ]] || { echo 'Expected exactly one factory job' >&2; exit 1; }
running=$(az containerapp job execution list --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" --query "length([?properties.status=='Running'])" -o tsv)
[[ "$running" == 0 ]] || { echo 'Another factory execution is running; wait for it to finish' >&2; exit 1; }
cleanup() {
  status=$?
  az containerapp job update --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" -o none \
    --remove-env-vars MANUFACTURING_PUBLIC_KEY MANUFACTURING_SERIAL || status=1
  [[ $status == 0 ]] && echo 'Per-run registration values removed from the factory job.' || echo 'REGISTRATION OR CLEANUP FAILED: check the factory job has no MANUFACTURING_* variables.' >&2
  exit $status
}
trap cleanup EXIT
az containerapp job update --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" -o none \
  --set-env-vars "MANUFACTURING_PUBLIC_KEY=$(base64 <<<"$pem" | tr -d '\n')" "MANUFACTURING_SERIAL=$serial"
execution=$(az containerapp job start --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" --query name -o tsv)
[[ "$execution" =~ ^[a-z0-9-]+$ ]] || { echo 'Job did not start' >&2; exit 1; }
for _ in $(seq 1 "${FACTORY_POLLS:-60}"); do
  state=$(az containerapp job execution show --subscription "$AZURE_SUBSCRIPTION_ID" -n "$job" -g "$rg" --job-execution-name "$execution" --query properties.status -o tsv)
  case "$state" in
    Succeeded)
      echo "Registered device $id (serial $serial) as d-$id. Execution $execution."
      printf '## Factory registration\n\nDevice `%s` (serial `%s`) admitted by job `%s` execution `%s`.\n' "$id" "$serial" "$job" "$execution" >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
      exit 0 ;;
    Failed|Stopped|Degraded) echo "Factory execution $execution $state. A unit that is already registered fails with device_already_registered; see the job's console logs." >&2; exit 1 ;;
  esac
  sleep "${FACTORY_POLL_SECONDS:-10}"
done
echo "Factory execution $execution did not finish in time" >&2
exit 1
