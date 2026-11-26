#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/infrastructure/check-context.sh
# Do not publish the superseded shared-product implementation.
if grep -REq 'WAVEFORM_DEVICE_SUFFIX|WAVEFORM-DEVICE-V1' services/certificates/tempo_certificates; then
  echo 'Merge the Tempo-only backend correction before publishing.' >&2
  exit 1
fi
[[ "$GITHUB_SHA" =~ ^[0-9a-f]{40}$ && "$GITHUB_RUN_ID" =~ ^[0-9]+$ && "$GITHUB_RUN_ATTEMPT" =~ ^[0-9]+$ ]]
mkdir -p build-host
registry_server=$(az deployment sub show --subscription "$AZURE_SUBSCRIPTION_ID" --name tempo-infrastructure --query properties.outputs.containerRegistry.value -o tsv)
[[ "$registry_server" =~ ^[a-z0-9]{5,50}\.azurecr\.io$ ]] || { echo 'Missing/invalid Bicep registry output' >&2; exit 1; }
registry=${registry_server%.azurecr.io}
repository=tempo-certificates
tag="$repository:$GITHUB_SHA-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT"
# --no-logs returns the completed ARM run object; no registry administrator credentials.
# Azure CLI resolves --file from the working directory (repository root), not the context.
az acr build --subscription "$AZURE_SUBSCRIPTION_ID" --registry "$registry" \
  --image "$tag" --platform linux/amd64 --file services/certificates/Dockerfile --timeout 1200 \
  --no-logs services/certificates -o json > build-host/certificate-build.json
jq -e '.status == "Succeeded"' build-host/certificate-build.json >/dev/null
digest=$(jq -er --arg registry "$registry_server" --arg repository "$repository" \
  --arg tag "${tag#*:}" '[.outputImages[] | select(.registry == $registry and .repository == $repository and .tag == $tag)] | select(length == 1) | .[0].digest' build-host/certificate-build.json)
[[ "$digest" =~ ^sha256:[0-9a-f]{64}$ ]] || { echo 'Build returned no valid image digest' >&2; exit 1; }
bash scripts/infrastructure/check-context.sh
image="$registry_server/$repository@$digest"
jq -n --arg sha "$GITHUB_SHA" --arg image "$image" --arg digest "${digest#sha256:}" \
  --arg run "$GITHUB_RUN_ID" --arg attempt "$GITHUB_RUN_ATTEMPT" \
  '{sha:$sha,image:$image,imageDigest:$digest,run:$run,attempt:$attempt}' > build-host/certificate-image.json
printf '## Certificate service image\n\nSource: `%s`\n\nImage: `%s`\n\nSelect imageDigest in main.bicep through a reviewed PR. This build does not deploy.\n' "$GITHUB_SHA" "$image" >> "$GITHUB_STEP_SUMMARY"
