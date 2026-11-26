#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/repo/scripts" "$tmp/bin"
cp "$root/scripts/certificate-account.sh" "$tmp/repo/scripts/"
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
  'containerapp job list'*) echo "${MOCK_JOB-tempo-westeurope-bootstrap}" ;;
  'containerapp job secret set'*) [[ "$*" == *'dns-bootstrap={ "key": "k", "secret": "s" }'* ]] || exit 91 ;;
  'containerapp job secret remove'*) ;;
  'containerapp job update'*) ;;
  'containerapp job start'*) echo exec-1 ;;
  'containerapp job execution show'*) echo "${MOCK_STATE:-Succeeded}" ;;
  *) exit 99 ;;
esac
MOCK
chmod +x "$tmp/bin/az" "$tmp/bin/git"
export PATH="$tmp/bin:$PATH" CALLS="$tmp/calls" BOOTSTRAP_POLLS=2 BOOTSTRAP_POLL_SECONDS=0
export GITHUB_ACTIONS=true GITHUB_EVENT_NAME=workflow_dispatch GITHUB_REF=refs/heads/main GITHUB_REPOSITORY=tinyrobotcompany/Tempo
export GITHUB_SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa GITHUB_STEP_SUMMARY="$tmp/summary" AZURE_SUBSCRIPTION_ID=subscription AZURE_TENANT_ID=tenant
export ACME_CONTACT_EMAIL=ops@example.com ACCEPTED_ACME_TERMS_URL=https://letsencrypt.org/documents/LE-SA-v1.8-July-06-2026.pdf
export SPACESHIP_CREDENTIALS='{ "key": "k", "secret": "s" }'
run="$tmp/repo/scripts/certificate-account.sh"
cleaned() { grep -q 'job update.*--remove-env-vars SPACESHIP_CREDENTIALS' "$CALLS" && grep -q 'job secret remove.*--secret-names dns-bootstrap' "$CALLS"; }
: > "$CALLS"; bash "$run" >/dev/null
cleaned || { echo 'Credential not removed after success' >&2; exit 1; }
# Reference is removed before the secret it points to.
[[ $(grep -n 'remove-env-vars' "$CALLS" | cut -d: -f1) -lt $(grep -n 'secret remove' "$CALLS" | cut -d: -f1) ]]
grep -q 'Succeeded\|succeeded' "$tmp/summary"
# Job failure and timeout still clean up and fail the run.
for state in Failed Running; do
  : > "$CALLS"
  if MOCK_STATE=$state bash "$run" >/dev/null 2>&1; then echo "Unexpected success: $state" >&2; exit 1; fi
  cleaned || { echo "Credential not removed after $state" >&2; exit 1; }
done
# Guards fail before any credential reaches Azure.
for scenario in event branch stale email terms creds-shape creds-extra no-compute no-job; do
  : > "$CALLS"
  (
    case "$scenario" in
      event) export GITHUB_EVENT_NAME=push ;;
      branch) export GITHUB_REF=refs/heads/feature/x ;;
      stale) export MOCK_HEAD=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb ;;
      email) export ACME_CONTACT_EMAIL='not an email' ;;
      terms) export ACCEPTED_ACME_TERMS_URL=https://example.com/terms.pdf ;;
      creds-shape) export SPACESHIP_CREDENTIALS='["k","s"]' ;;
      creds-extra) export SPACESHIP_CREDENTIALS='{"key":"k","secret":"s","x":1}' ;;
      no-compute) export MOCK_RG= ;;
      no-job) export MOCK_JOB= ;;
    esac
    if bash "$run" >/dev/null 2>&1; then echo "Unexpected success: $scenario" >&2; exit 1; fi
  )
  if grep -q 'secret set' "$CALLS"; then echo "Credential sent despite $scenario" >&2; exit 1; fi
done
# The secret value never appears in output.
out=$(bash "$run" 2>&1)
if grep -q '"secret"' <<<"$out"; then echo 'Credential printed' >&2; exit 1; fi
echo 'PASS: CA bootstrap guards, one-shot credential lifetime and cleanup on failure/timeout'
