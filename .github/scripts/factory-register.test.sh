#!/usr/bin/env bash
# Factory registration: run guards, public-key-only input, per-run values that
# are always removed, and failure/timeout handling. Azure is mocked.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/repo/scripts" "$tmp/bin"
cp "$root/scripts/factory-register.sh" "$tmp/repo/scripts/"
cat > "$tmp/bin/git" <<'MOCK'
#!/usr/bin/env bash
printf '%s\trefs/heads/main\n' "${MOCK_HEAD:-$GITHUB_SHA}"
MOCK
cat > "$tmp/bin/az" <<'MOCK'
#!/usr/bin/env bash
echo "$*" >> "$CALLS"
case "$*" in
  'account show --query id -o tsv') echo subscription ;;
  'account show --query tenantId -o tsv') echo tenant ;;
  'deployment sub show'*) echo "${MOCK_RG-rg-tempo-westeurope-compute}" ;;
  'containerapp job list'*) echo "${MOCK_JOB-tempo-westeurope-factory}" ;;
  'containerapp job execution list'*) echo "${MOCK_RUNNING:-0}" ;;
  'containerapp job update'*'--set-env-vars'*)
    # The job receives exactly the base64 PEM the manufacturing module decodes.
    all="$*"; key=${all##*MANUFACTURING_PUBLIC_KEY=}; key=${key%% *}
    [[ "$(base64 -d <<<"$key")" == "$(cat "$EXPECTED_PEM")" ]] || exit 91
    [[ "$*" == *"MANUFACTURING_SERIAL=$MANUFACTURING_SERIAL"* ]] || exit 92 ;;
  'containerapp job update'*) ;;
  'containerapp job start'*) echo exec-1 ;;
  'containerapp job execution show'*) echo "${MOCK_STATE:-Succeeded}" ;;
  *) exit 99 ;;
esac
MOCK
chmod +x "$tmp/bin/az" "$tmp/bin/git"
# Test-only key generated here.
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$tmp/private.pem" 2>/dev/null
openssl pkey -in "$tmp/private.pem" -pubout -out "$tmp/public.pem"
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 2>/dev/null | openssl pkey -pubout > "$tmp/rsa.pem"
id=$(openssl pkey -pubin -in "$tmp/public.pem" -outform DER | shasum -a 256 | cut -c1-32)
export PATH="$tmp/bin:$PATH" CALLS="$tmp/calls" FACTORY_POLLS=2 FACTORY_POLL_SECONDS=0 EXPECTED_PEM="$tmp/public.pem"
export GITHUB_ACTIONS=true GITHUB_EVENT_NAME=workflow_dispatch GITHUB_REF=refs/heads/main GITHUB_REPOSITORY=tinyrobotcompany/Tempo
export GITHUB_SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa GITHUB_STEP_SUMMARY="$tmp/summary" AZURE_SUBSCRIPTION_ID=subscription AZURE_TENANT_ID=tenant
export MANUFACTURING_PUBLIC_KEY_B64=$(base64 < "$tmp/public.pem" | tr -d '\n') MANUFACTURING_SERIAL=P4-PROTO-001 EXPECTED_DEVICE_ID=$id
run="$tmp/repo/scripts/factory-register.sh"
cleaned() { grep -q 'job update.*--remove-env-vars MANUFACTURING_PUBLIC_KEY MANUFACTURING_SERIAL' "$CALLS"; }

: > "$CALLS"; out=$(bash "$run")
cleaned || { echo 'Values not removed after success' >&2; exit 1; }
grep -q "Registered device $id" <<<"$out"
grep -q "$id" "$tmp/summary"
[[ $(grep -n 'set-env-vars' "$CALLS" | cut -d: -f1) -lt $(grep -n 'job start' "$CALLS" | cut -d: -f1) ]]
[[ $(grep -n 'job start' "$CALLS" | cut -d: -f1) -lt $(grep -n 'remove-env-vars' "$CALLS" | cut -d: -f1) ]]
# Job failure and timeout still clean up and fail the run.
for state in Failed Running; do
  : > "$CALLS"
  if MOCK_STATE=$state bash "$run" >/dev/null 2>&1; then echo "Unexpected success: $state" >&2; exit 1; fi
  cleaned || { echo "Values not removed after $state" >&2; exit 1; }
done
# Local operator mode reads a key file and needs the explicit flag.
local_run() { env -u GITHUB_ACTIONS -u MANUFACTURING_PUBLIC_KEY_B64 bash "$run" "$@"; }
: > "$CALLS"
local_run --i-am-the-operator --public-key "$tmp/public.pem" --serial P4-PROTO-001 --device-id "$id" >/dev/null
cleaned || { echo 'Local run did not clean up' >&2; exit 1; }
# Guards fail before anything reaches the job.
for scenario in event branch stale local-no-flag flag-in-actions private-key rsa-key two-keys bad-base64 \
    serial-empty serial-long serial-chars wrong-id no-id no-compute no-job busy; do
  : > "$CALLS"
  (
    extra=()
    case "$scenario" in
      event) export GITHUB_EVENT_NAME=push ;;
      branch) export GITHUB_REF=refs/heads/feature/x ;;
      stale) export MOCK_HEAD=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb ;;
      local-no-flag) unset GITHUB_ACTIONS ;;
      flag-in-actions) extra=(--i-am-the-operator) ;;
      private-key) export MANUFACTURING_PUBLIC_KEY_B64=$(base64 < "$tmp/private.pem" | tr -d '\n') ;;
      rsa-key) export MANUFACTURING_PUBLIC_KEY_B64=$(base64 < "$tmp/rsa.pem" | tr -d '\n') ;;
      two-keys) export MANUFACTURING_PUBLIC_KEY_B64=$(cat "$tmp/public.pem" "$tmp/public.pem" | base64 | tr -d '\n') ;;
      bad-base64) export MANUFACTURING_PUBLIC_KEY_B64='not base64!' ;;
      serial-empty) export MANUFACTURING_SERIAL= ;;
      serial-long) export MANUFACTURING_SERIAL=$(printf 'a%.0s' $(seq 65)) ;;
      serial-chars) export MANUFACTURING_SERIAL='P4 001;rm' ;;
      wrong-id) export EXPECTED_DEVICE_ID=00000000000000000000000000000000 ;;
      no-id) export EXPECTED_DEVICE_ID= ;;
      no-compute) export MOCK_RG= ;;
      no-job) export MOCK_JOB= ;;
      busy) export MOCK_RUNNING=1 ;;
    esac
    if bash "$run" ${extra[@]+"${extra[@]}"} >/dev/null 2>&1; then echo "Unexpected success: $scenario" >&2; exit 1; fi
  )
  if grep -q -E 'job update|job start' "$CALLS"; then echo "Job changed despite $scenario" >&2; exit 1; fi
done
echo 'PASS: factory registration guards, public-key-only input and per-run cleanup on failure/timeout'
