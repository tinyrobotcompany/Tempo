#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/bin"
cat > "$tmp/bin/git" <<'MOCK'
#!/usr/bin/env bash
case "$*" in
  'rev-parse HEAD') echo "$TEST_CHECKOUT" ;;
  'ls-remote --exit-code origin refs/heads/main')
    [[ "${TEST_NETWORK_FAIL:-0}" == 0 ]] || exit 128
    printf '%s\n' "$TEST_REMOTE" ;;
  *) echo "Unexpected git command: $*" >&2; exit 99 ;;
esac
MOCK
chmod +x "$tmp/bin/git"
export PATH="$tmp/bin:$PATH"
export GITHUB_REPOSITORY=tinyrobotcompany/Tempo GITHUB_REF=refs/heads/main
export GITHUB_SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
export TEST_CHECKOUT="$GITHUB_SHA"
export TEST_REMOTE="$GITHUB_SHA"$'\trefs/heads/main'
check() { bash "$root/.github/scripts/ota-release-head.sh" > "$tmp/result" 2>&1; }
reject() { if check; then echo 'Expected release rejection' >&2; exit 1; fi; }
check
export TEST_REMOTE=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb$'\trefs/heads/main'
reject
grep -q 'Start a NEW Publish OTA Release run' "$tmp/result"
export TEST_REMOTE="$GITHUB_SHA"$'\trefs/heads/main'
export TEST_NETWORK_FAIL=1; reject; export TEST_NETWORK_FAIL=0
export TEST_CHECKOUT=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb; reject
export TEST_CHECKOUT="$GITHUB_SHA"
for ref in refs/heads/feature/test refs/tags/v1.0.0; do export GITHUB_REF="$ref"; reject; done
export GITHUB_REF=refs/heads/main
export GITHUB_REPOSITORY=other/Tempo; reject; export GITHUB_REPOSITORY=tinyrobotcompany/Tempo
for response in '' invalid "$GITHUB_SHA refs/heads/other" "$TEST_REMOTE"$'\nextra'; do
  export TEST_REMOTE="$response"; reject
done
export TEST_REMOTE="$GITHUB_SHA"$'\trefs/heads/main'
export GITHUB_SHA=invalid; reject
# Assert placement: no expensive build or release mutation precedes its guard.
node --input-type=module - "$root" <<'JS'
import assert from 'node:assert/strict';
import fs from 'node:fs';
const workflow=fs.readFileSync(`${process.argv[2]}/.github/workflows/ota-release.yml`,'utf8');
assert(workflow.includes('  workflow_dispatch:'));
const publish=workflow.slice(workflow.indexOf('  publish:'));
assert(publish.indexOf('bash .github/scripts/ota-release-head.sh') < publish.indexOf('Checkout pinned ESP-IDF'));
const mutation=publish.slice(publish.indexOf('      - name: Publish complete release'));
assert(mutation.indexOf('bash .github/scripts/ota-release-head.sh') < mutation.indexOf('gh api '));
assert(mutation.lastIndexOf('bash .github/scripts/ota-release-head.sh') < mutation.indexOf('gh release edit'));
assert(mutation.lastIndexOf('bash .github/scripts/ota-release-head.sh') > mutation.indexOf('verify-assets'));
JS
echo 'PASS: current/stale release commits, approval delay, wrong context, lookup errors and publication guard placement'
