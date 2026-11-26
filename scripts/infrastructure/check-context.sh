#!/usr/bin/env bash
set -euo pipefail
# Reject a stale approved run or a login against another subscription.
[[ "${GITHUB_ACTIONS:-}" == true && "${GITHUB_EVENT_NAME:-}" == push && "${GITHUB_REF:-}" == refs/heads/main ]] || { echo 'Expected main push workflow' >&2; exit 1; }
[[ "${GITHUB_REPOSITORY:-}" == tinyrobotcompany/Tempo ]] || exit 1
: "${AZURE_SUBSCRIPTION_ID:?}" "${AZURE_TENANT_ID:?}" "${GITHUB_SHA:?}"
[[ $(az account show --query id -o tsv) == "$AZURE_SUBSCRIPTION_ID" ]] || exit 1
[[ $(az account show --query tenantId -o tsv) == "$AZURE_TENANT_ID" ]] || exit 1
[[ $(git rev-parse HEAD) == "$GITHUB_SHA" ]] || exit 1
[[ $(git ls-remote origin refs/heads/main | cut -f1) == "$GITHUB_SHA" ]] || { echo 'Main changed since this run; use a new reviewed Bicep merge.' >&2; exit 1; }
