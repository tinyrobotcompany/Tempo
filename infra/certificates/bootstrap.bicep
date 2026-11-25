targetScope = 'subscription'

@description('One-time operator-reviewed setup. This does not deploy the application or move existing resources.')
@allowed(['westeurope'])
param location string = 'westeurope'
param owner string = 'tinyrobotcompany'
@description('True only while the five old source groups still exist. Reapply with false after migration.')
param includeMigrationSources bool = false
@allowed(['rg-tempo-staging-westeurope-security', 'rg-tempo-westeurope-security'])
@description('Group currently holding the EXISTING runtime identities; do not create replacements.')
param runtimeIdentityResourceGroup string = 'rg-tempo-westeurope-security'
resource runtime 'Microsoft.ManagedIdentity/userAssignedIdentities@2023-01-31' existing = [for role in ['api', 'worker', 'factory', 'bootstrap']: {
  name: 'tempo-staging-${role}'
  scope: resourceGroup(runtimeIdentityResourceGroup)
}]
var bindings = {
  data: [
    { role: '76199698-9eea-4c19-bc75-cec21354c6b6', principals: [runtime[0].properties.principalId, runtime[1].properties.principalId] }
    { role: '0a9a7e1f-b9d0-4cc4-a60d-0319b160aaa3', principals: [runtime[0].properties.principalId, runtime[1].properties.principalId, runtime[2].properties.principalId] }
    { role: '7f951dda-4ed3-4680-a7ca-43fe172d538d', principals: [runtime[0].properties.principalId, runtime[1].properties.principalId, runtime[2].properties.principalId, runtime[3].properties.principalId] }
  ]
  security: [
    { role: '4633458b-17de-408a-b874-0445c86b69e6', principals: [runtime[1].properties.principalId] }
    { role: 'b86a8fe4-44ce-4948-aee5-eccb2c155cd7', principals: [runtime[3].properties.principalId] }
  ]
  network: []
  ops: []
  compute: []
}

var concerns = ['network', 'data', 'security', 'ops', 'compute']
resource groups 'Microsoft.Resources/resourceGroups@2024-03-01' = [for concern in concerns: {
  name: 'rg-tempo-${location}-${concern}'
  location: location
  tags: { concern: concern, environment: 'single', owner: owner, 'cost-center': 'tempo' }
}]
module identity 'delivery-identity.bicep' = {
  name: 'tempo-delivery-identity'
  // Explicit scope avoids a loop index leaking into identity output references.
  scope: resourceGroup('rg-tempo-${location}-security')
  dependsOn: [groups[2]]
  params: { location: location }
}
// Deployment records at subscription scope; no subscription-wide resource writer/Owner grant.
resource deploymentRole 'Microsoft.Authorization/roleDefinitions@2022-04-01' = {
  name: guid(subscription().id, 'tempo-deployment-records')
  properties: {
    roleName: 'Tempo deployment records'
    description: 'ARM deployment records only. Resource writes are separately scoped to Tempo groups.'
    type: 'CustomRole'
    assignableScopes: [subscription().id]
    permissions: [{ actions: [
      'Microsoft.Resources/deployments/*'
      'Microsoft.Resources/subscriptions/read'
      'Microsoft.Resources/subscriptions/resourceGroups/read'
    ], notActions: [], dataActions: [], notDataActions: [] }]
  }
}
resource deploymentAccess 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(subscription().id, 'tempo-infrastructure-delivery', 'deployment-records')
  properties: { principalId: identity.outputs.principalId, principalType: 'ServicePrincipal', roleDefinitionId: deploymentRole.id }
}
module permissions 'delivery-permissions.bicep' = [for (concern, i) in concerns: {
  name: 'tempo-delivery-${concern}'
  scope: groups[i]
  params: { principalId: identity.outputs.principalId, bindings: bindings[concern] }
}]
// Temporary move authority is bounded to the five already-existing source groups.
// Deleting the empty source groups removes these inherited grants as well.
resource sources 'Microsoft.Resources/resourceGroups@2024-03-01' existing = [for concern in concerns: {
  name: 'rg-tempo-staging-${location}-${concern}'
}]
module migrationPermissions 'delivery-permissions.bicep' = [for (concern, i) in concerns: if (includeMigrationSources) {
  name: 'tempo-migration-${concern}'
  scope: sources[i]
  params: { principalId: identity.outputs.principalId, bindings: bindings[concern] }
}]
output clientId string = identity.outputs.clientId
output tenantId string = tenant().tenantId
output subscriptionId string = subscription().subscriptionId

// Locks require an operator with lock-management permission, not the delivery
// identity. Apply only AFTER the in-place migration; locks must not block moves.
var unique = uniqueString(subscription().id, 'staging', location)
module registryLock 'storage-lock.bicep' = if (!includeMigrationSources) {
  name: 'tempo-registry-protection'
  scope: groups[1]
  params: { storageName: 'sttempo${unique}' }
}
module vaultLock 'vault-lock.bicep' = if (!includeMigrationSources) {
  name: 'tempo-vault-protection'
  scope: groups[2]
  params: { vaultName: 'kv-tempo-${unique}' }
}
