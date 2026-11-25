#!/usr/bin/env bash
# Read-only guard: the approved run must still describe current main.
set -euo pipefail
fail() { echo "::error::$*" >&2; exit 1; }
[[ "${GITHUB_REPOSITORY:-}" == tinyrobotcompany/Tempo && "${GITHUB_REF:-}" == refs/heads/main ]] ||
  fail 'OTA publication is allowed only from tinyrobotcompany/Tempo main.'
[[ "${GITHUB_SHA:-}" =~ ^[0-9a-f]{40}$ ]] || fail 'Missing or invalid release commit.'
checkout=$(git rev-parse HEAD) || fail 'Cannot verify the checked-out release commit.'
[[ "$checkout" == "$GITHUB_SHA" ]] || fail 'Release checkout differs from the approved run.'
remote=$(git ls-remote --exit-code origin refs/heads/main) || fail 'Cannot verify current main; no release will be published.'
read -r current ref extra <<< "$remote"
[[ "$current" =~ ^[0-9a-f]{40}$ && "$ref" == refs/heads/main && -z "${extra:-}" && "$remote" != *$'\n'* ]] ||
  fail 'Unexpected main reference response; no release will be published.'
[[ "$current" == "$GITHUB_SHA" ]] ||
  fail 'Outdated OTA run: main has advanced. Start a NEW Publish OTA Release run on main; do not rerun this old job.'
echo "Release commit is current main: $GITHUB_SHA"
