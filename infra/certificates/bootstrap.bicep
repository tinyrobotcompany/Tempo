targetScope = 'subscription'

@description('One-time operator-reviewed setup. This does not deploy the application or move existing resources.')
@allowed(['westeurope'])
param location string = 'westeurope'
param owner string = 'tinyrobotcompany'
var concerns = ['network', 'data', 'security', 'ops', 'compute']
resource groups 'Microsoft.Resources/resourceGroups@2024-03-01' = [for concern in concerns: {
  name: 'rg-tempo-${location}-${concern}'
  location: location
  tags: { concern: concern, environment: 'single', owner: owner, 'cost-center': 'tempo' }
}]
module identity 'delivery-identity.bicep' = {
  name: 'tempo-delivery-identity'
  scope: groups[2]
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
  params: { principalId: identity.outputs.principalId }
}]
// Temporary move authority is bounded to the five already-existing source groups.
// Deleting the empty source groups removes these inherited grants as well.
resource sources 'Microsoft.Resources/resourceGroups@2024-03-01' existing = [for concern in concerns: {
  name: 'rg-tempo-staging-${location}-${concern}'
}]
module migrationPermissions 'delivery-permissions.bicep' = [for (concern, i) in concerns: {
  name: 'tempo-migration-${concern}'
  scope: sources[i]
  params: { principalId: identity.outputs.principalId }
}]
output clientId string = identity.outputs.clientId
output tenantId string = tenant().tenantId
output subscriptionId string = subscription().subscriptionId
