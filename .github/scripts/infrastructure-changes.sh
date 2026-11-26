#!/usr/bin/env bash
set -euo pipefail
# PR validation covers all delivery inputs. Deployment itself has a Bicep-only trigger.
base=$(jq -r '.pull_request.base.sha // .before // empty' "$GITHUB_EVENT_PATH")
head=$(jq -r '.pull_request.head.sha // empty' "$GITHUB_EVENT_PATH")
head=${head:-$GITHUB_SHA}
paths=$(mktemp)
trap 'rm -f "$paths"' EXIT
if jq -e '.pull_request != null' "$GITHUB_EVENT_PATH" >/dev/null; then
  git diff --no-renames --name-only -z "$base...$head" > "$paths"
elif [[ -n "$base" && ! "$base" =~ ^0+$ ]]; then
  git diff --no-renames --name-only -z "$base" "$head" > "$paths"
else
  git ls-tree -r --name-only -z "$head" > "$paths"
fi
selected=false
while IFS= read -r -d '' path; do
  case "$path" in
    infra/certificates/*|scripts/infrastructure/*|.github/workflows/infrastructure-*.yaml|.github/scripts/infrastructure-*|.github/workflows/ci.yaml|.github/scripts/firmware-changes.py)
      selected=true ;;
  esac
done < "$paths"
echo "infrastructure=$selected" >> "$GITHUB_OUTPUT"
echo "Infrastructure validation required: $selected"
