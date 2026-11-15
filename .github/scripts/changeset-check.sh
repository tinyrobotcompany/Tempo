#!/usr/bin/env bash
set -euo pipefail

if [[ "${GITHUB_EVENT_NAME:-}" != "pull_request" ]]; then
  echo "Changeset check runs only for pull_request events."
  exit 0
fi

BASE_REF="${GITHUB_BASE_REF:-main}"
DIFF_BASE="origin/${BASE_REF}"
if git remote get-url origin >/dev/null 2>&1; then
  git fetch origin "${BASE_REF}" --depth=1
elif git show-ref --verify --quiet "refs/heads/${BASE_REF}"; then
  DIFF_BASE="${BASE_REF}"
else
  DIFF_BASE="$(git rev-list --max-parents=0 HEAD | tail -n1)"
fi

CHANGED_FILES="$(git diff --name-only "${DIFF_BASE}"...HEAD)"
echo "Changed files:"
echo "${CHANGED_FILES}"

RELEASABLE_CHANGED="$(
  echo "${CHANGED_FILES}" | grep -E '^(firmware/|companion/|tests/|scripts/|\.github/|\.githooks/|package(-lock)?\.json$)' || true
)"

if [[ -z "${RELEASABLE_CHANGED}" ]]; then
  echo "No releasable code paths changed."
  exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if python3 "$SCRIPT_DIR/action-version-only.py" "$DIFF_BASE" HEAD; then
  echo "Only pinned GitHub Action versions changed; no firmware release note required."
  exit 0
fi

CHANGESET_FILES="$(
  git diff --diff-filter=AM --name-only "${DIFF_BASE}"...HEAD | grep -E '^\.changeset/[^/]+\.md$' | grep -v '^\.changeset/README\.md$' || true
)"

if [[ -n "${CHANGESET_FILES}" ]]; then
  echo "Changeset file detected:"
  echo "${CHANGESET_FILES}"
  exit 0
fi

# A release-preparation PR consumes pending notes into versioned release notes.
# Require all three release outputs and validate that no pending notes remain.
if echo "$CHANGED_FILES" | grep -qx 'firmware/CMakeLists.txt' &&
   echo "$CHANGED_FILES" | grep -qx 'CHANGELOG.md' &&
   echo "$CHANGED_FILES" | grep -qE '^docs/releases/[0-9]+\.[0-9]+\.[0-9]+\.md$'; then
  RELEASE_TAG="$(python3 -c 'import re,pathlib; print("v"+re.search(r"project\(weather_station VERSION ([0-9.]+)\)",pathlib.Path("firmware/CMakeLists.txt").read_text()).group(1))')"
  python3 scripts/release.py check --tag "$RELEASE_TAG"
  echo "Validated release preparation; pending notes have been consumed."
  exit 0
fi

echo "::error::Releasable changes detected without a changeset file."
echo "::error::Add one file under .changeset/ (excluding README.md)."
echo "::error::See .changeset/README.md for the required format."
exit 1
