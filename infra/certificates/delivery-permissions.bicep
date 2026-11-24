param principalId string
// Contributor cannot assign the runtime's narrow RBAC roles; the second grant
// permits role administration only inside this Tempo resource group.
var roles = ['b24988ac-6180-42a0-ab88-20f7382dd24c', 'f58310d9-a9f6-439a-9e8d-f62e7b41a168']
resource grants 'Microsoft.Authorization/roleAssignments@2022-04-01' = [for role in roles: {
  name: guid(resourceGroup().id, principalId, role)
  properties: { principalId: principalId, principalType: 'ServicePrincipal', roleDefinitionId: subscriptionResourceId('Microsoft.Authorization/roleDefinitions', role) }
}]
