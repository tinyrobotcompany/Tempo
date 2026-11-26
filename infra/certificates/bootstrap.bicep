targetScope = 'subscription'

// One-time operator bootstrap: groups, delivery identity and scoped permissions only.
@minLength(2)
@maxLength(8)
@description('Lowercase alphanumeric project name, used to derive resource names.')
param project string = 'tempo'
@allowed(['westeurope'])
param location string = 'westeurope'
param owner string = 'tinyrobotcompany'
var concerns = ['network', 'data', 'security', 'ops', 'compute']
// Roles main.bicep assigns to runtime identities in each group (CI checks this list
// against the workload modules). Everything else, including Owner, is undelegable.
var delegableRoles = {
  network: []
  data: [
    '76199698-9eea-4c19-bc75-cec21354c6b6' // Storage Table Data Reader
    '0a9a7e1f-b9d0-4cc4-a60d-0319b160aaa3' // Storage Table Data Contributor
    '7f951dda-4ed3-4680-a7ca-43fe172d538d' // AcrPull
  ]
  security: [
    '4633458b-17de-408a-b874-0445c86b69e6' // Key Vault Secrets User
    'b86a8fe4-44ce-4948-aee5-eccb2c155cd7' // Key Vault Secrets Officer
  ]
  ops: []
  compute: []
}
resource groups 'Microsoft.Resources/resourceGroups@2024-03-01' = [for concern in concerns: {
  name: 'rg-${project}-${location}-${concern}'
  location: location
  tags: { concern: concern, environment: 'single', owner: owner, 'cost-center': project }
}]
module identity 'delivery-identity.bicep' = {
  name: '${project}-delivery-identity'
  // Explicit scope avoids a loop index leaking into identity output references.
  scope: resourceGroup('rg-${project}-${location}-security')
  dependsOn: [groups[2]]
  params: { location: location, project: project }
}
// Deployment records at subscription scope; no subscription-wide resource writer/Owner grant.
resource deploymentRole 'Microsoft.Authorization/roleDefinitions@2022-04-01' = {
  name: guid(subscription().id, '${project}-deployment-records')
  properties: {
    roleName: '${project} deployment records'
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
  name: guid(subscription().id, '${project}-infrastructure-delivery', 'deployment-records')
  properties: { principalId: identity.outputs.principalId, principalType: 'ServicePrincipal', roleDefinitionId: deploymentRole.id }
}
module permissions 'delivery-permissions.bicep' = [for (concern, i) in concerns: {
  name: '${project}-delivery-${concern}'
  scope: groups[i]
  params: { principalId: identity.outputs.principalId, delegableRoles: delegableRoles[concern] }
}]
output clientId string = identity.outputs.clientId
output tenantId string = tenant().tenantId
output subscriptionId string = subscription().subscriptionId
output resourceGroups array = [for concern in concerns: 'rg-${project}-${location}-${concern}']
output location string = location
