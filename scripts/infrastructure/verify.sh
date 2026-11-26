#!/usr/bin/env bash
set -euo pipefail
: "${AZURE_SUBSCRIPTION_ID:?}"
while IFS= read -r group; do
  [[ $(az group exists --subscription "$AZURE_SUBSCRIPTION_ID" --name "$group") == true ]]
  resources=$(az resource list --subscription "$AZURE_SUBSCRIPTION_ID" --resource-group "$group" -o json)
  while IFS= read -r id; do
    detail=$(az resource show --ids "$id" -o json)
    jq -e '.properties.provisioningState == "Succeeded" and .properties.publicNetworkAccess == "Disabled"' <<< "$detail" >/dev/null
  done < <(jq -r '.[] | select(.type == "Microsoft.Storage/storageAccounts" or .type == "Microsoft.KeyVault/vaults") | .id' <<< "$resources")
  while IFS= read -r id; do
    detail=$(az network private-endpoint show --ids "$id" -o json)
    jq -e '.provisioningState == "Succeeded" and (.privateLinkServiceConnections | length > 0) and all(.privateLinkServiceConnections[]; .privateLinkServiceConnectionState.status == "Approved")' <<< "$detail" >/dev/null
  done < <(jq -r '.[] | select(.type == "Microsoft.Network/privateEndpoints") | .id' <<< "$resources")
done < <(jq -r '.resourceGroups.value[]' "$1")
echo 'Verified resource groups and private storage/Key Vault configuration.'
