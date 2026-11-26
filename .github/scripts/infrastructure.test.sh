#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# Exercise the real detector against additions, deletions, renames and unrelated changes.
git init -q "$tmp/repo"
git -C "$tmp/repo" config user.email tests@example.invalid
git -C "$tmp/repo" config user.name Tests
cd "$tmp/repo"
mkdir -p infra/certificates
printf test > README.md
git add . && git commit -qm initial
base=$(git rev-parse HEAD)
printf template > infra/certificates/main.bicep
git add . && git commit -qm bicep
head=$(git rev-parse HEAD)
check() {
  jq -n --arg base "$1" '{before:$base}' > "$tmp/event.json"
  : > "$tmp/output"
  GITHUB_EVENT_PATH="$tmp/event.json" GITHUB_SHA="$2" GITHUB_OUTPUT="$tmp/output" bash "$root/.github/scripts/infrastructure-changes.sh"
  grep -qx "infrastructure=$3" "$tmp/output"
}
check "$base" "$head" true
base=$head
mv infra/certificates/main.bicep renamed.txt
git add -A && git commit -qm rename
head=$(git rev-parse HEAD)
check "$base" "$head" true
base=$head
printf more >> README.md
git add . && git commit -qm docs
head=$(git rev-parse HEAD)
check "$base" "$head" false
# PR detection uses merge-base diff rather than just the final commit.
jq -n --arg base "$base" --arg head "$head" '{pull_request:{base:{sha:$base},head:{sha:$head}}}' > "$tmp/event.json"
: > "$tmp/output"
GITHUB_EVENT_PATH="$tmp/event.json" GITHUB_SHA="$head" GITHUB_OUTPUT="$tmp/output" bash "$root/.github/scripts/infrastructure-changes.sh"
grep -qx 'infrastructure=false' "$tmp/output"
# Test actual context guard without cloud writes, including wrong account/stale checkout.
mkdir "$tmp/bin"
cat > "$tmp/bin/az" <<'MOCK'
#!/usr/bin/env bash
case "$*" in
  'account show --query id -o tsv') echo "${MOCK_SUBSCRIPTION:-subscription}" ;;
  'account show --query tenantId -o tsv') echo tenant ;;
  *) exit 99 ;;
esac
MOCK
cat > "$tmp/bin/git" <<'MOCK'
#!/usr/bin/env bash
case "$*" in
  'rev-parse HEAD') echo abc ;;
  'ls-remote origin refs/heads/main') printf '%s\trefs/heads/main\n' "${MOCK_HEAD:-abc}" ;;
  *) exit 99 ;;
esac
MOCK
chmod +x "$tmp/bin/az" "$tmp/bin/git"
export PATH="$tmp/bin:$PATH" GITHUB_ACTIONS=true GITHUB_EVENT_NAME=push GITHUB_REF=refs/heads/main
export GITHUB_REPOSITORY=tinyrobotcompany/Tempo GITHUB_SHA=abc AZURE_SUBSCRIPTION_ID=subscription AZURE_TENANT_ID=tenant
bash "$root/scripts/infrastructure/check-context.sh"
if MOCK_HEAD=stale bash "$root/scripts/infrastructure/check-context.sh"; then exit 1; fi
if MOCK_SUBSCRIPTION=wrong bash "$root/scripts/infrastructure/check-context.sh"; then exit 1; fi
if GITHUB_EVENT_NAME=pull_request bash "$root/scripts/infrastructure/check-context.sh"; then exit 1; fi
if GITHUB_REF=refs/heads/feature/test bash "$root/scripts/infrastructure/check-context.sh"; then exit 1; fi
# No Python or old migration implementation can re-enter the infrastructure path.
if grep -En 'python|\.py\b' "$root"/.github/workflows/infrastructure-*.yaml "$root"/scripts/infrastructure/*.sh; then exit 1; fi
[[ ! -e "$root/.github/workflows/infrastructure-migrate.yaml" ]]
[[ ! -e "$root/infra/certificates/deployment.json" ]]
if grep -q 'workflow_dispatch:' "$root/.github/workflows/infrastructure-deploy.yaml"; then exit 1; fi
grep -Fq "'infra/certificates/**/*.bicep'" "$root/.github/workflows/infrastructure-deploy.yaml"
echo 'PASS: infrastructure change detection, deployment context and no-Python/no-migration guards'
# Execute the workflow's actual what-if command against the Azure CLI output contract.
# Without --no-pretty-print, Azure emits human-readable text even with -o json.
cat > "$tmp/bin/az" <<'MOCK'
#!/usr/bin/env bash
[[ "$1 $2 $3" == 'deployment sub what-if' ]] || exit 99
case " $* " in
  *' --no-pretty-print '*) printf '{"status":"Succeeded","changes":[]}\n' ;;
  *) printf 'Note: The result may contain false positive predictions (noise).\n' ;;
esac
MOCK
mkdir -p build-host/infrastructure
export AZURE_LOCATION=westeurope
awk '/^[[:space:]]*az deployment sub what-if / { emit=1 } emit { print; if ($0 !~ /\\$/) exit }' \
  "$root/.github/workflows/infrastructure-deploy.yaml" > "$tmp/preview-command.sh"
[[ -s "$tmp/preview-command.sh" ]]
bash "$tmp/preview-command.sh"
jq -e '.status == "Succeeded" and all(.changes[]; .changeType != "Delete")' build-host/infrastructure/preview.json >/dev/null
# Prove this fixture catches the original failure instead of accepting prose.
sed 's/ --no-pretty-print//' "$tmp/preview-command.sh" > "$tmp/original-command.sh"
bash "$tmp/original-command.sh"
if jq -e . build-host/infrastructure/preview.json 2>/dev/null; then exit 1; fi
echo 'PASS: actual workflow preview command emits parseable JSON; original command fails'
