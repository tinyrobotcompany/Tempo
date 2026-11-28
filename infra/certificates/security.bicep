param location string
@minLength(13)
@maxLength(13)
param unique string
@minLength(2)
@maxLength(8)
param project string
param prefix string
resource identities 'Microsoft.ManagedIdentity/userAssignedIdentities@2023-01-31' = [for role in ['api', 'worker', 'factory', 'bootstrap', 'backup', 'restore']: {
  name: '${prefix}-${role}'
  location: location
}]
resource vault 'Microsoft.KeyVault/vaults@2023-07-01' = {
  name: 'kv-${project}-${take(unique, 10)}'
  location: location
  properties: {
    tenantId: tenant().tenantId
    sku: { family: 'A', name: 'standard' }
    enableRbacAuthorization: true
    enablePurgeProtection: true
    softDeleteRetentionInDays: 90
    publicNetworkAccess: 'Disabled'
    networkAcls: { defaultAction: 'Deny', bypass: 'None' }
  }
}
resource workerSecrets 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(vault.id, identities[1].id, 'secrets-reader')
  scope: vault
  properties: {
    principalId: identities[1].properties.principalId
    principalType: 'ServicePrincipal'
    roleDefinitionId: subscriptionResourceId('Microsoft.Authorization/roleDefinitions', '4633458b-17de-408a-b874-0445c86b69e6')
  }
}
output apiIdentity string = identities[0].id
output apiPrincipal string = identities[0].properties.principalId
output apiClient string = identities[0].properties.clientId
output workerIdentity string = identities[1].id
output workerPrincipal string = identities[1].properties.principalId
output workerClient string = identities[1].properties.clientId
output factoryIdentity string = identities[2].id
output factoryPrincipal string = identities[2].properties.principalId
output factoryClient string = identities[2].properties.clientId
output vaultId string = vault.id
output vaultName string = vault.name
output vaultUrl string = vault.properties.vaultUri
resource bootstrapSecrets 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(vault.id, identities[3].id, 'secrets-officer')
  scope: vault
  properties: {
    principalId: identities[3].properties.principalId
    principalType: 'ServicePrincipal'
    roleDefinitionId: subscriptionResourceId('Microsoft.Authorization/roleDefinitions', 'b86a8fe4-44ce-4948-aee5-eccb2c155cd7')
  }
}
output bootstrapIdentity string = identities[3].id
output bootstrapClient string = identities[3].properties.clientId
output bootstrapPrincipal string = identities[3].properties.principalId
output backupIdentity string = identities[4].id
output backupClient string = identities[4].properties.clientId
output backupPrincipal string = identities[4].properties.principalId
output restoreIdentity string = identities[5].id
output restoreClient string = identities[5].properties.clientId
output restorePrincipal string = identities[5].properties.principalId
