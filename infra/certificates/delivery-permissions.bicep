param principalId string
// Exact built-in role / managed-identity combinations, supplied by bootstrap.
param bindings array
var contributor = 'b24988ac-6180-42a0-ab88-20f7382dd24c'
var administrator = 'f58310d9-a9f6-439a-9e8d-f62e7b41a168'
var writeRules = map(bindings, binding => '(@Request[Microsoft.Authorization/roleAssignments:RoleDefinitionId] GuidEquals ${binding.role} AND @Request[Microsoft.Authorization/roleAssignments:PrincipalId] ForAnyOfAnyValues:GuidEquals {${join(binding.principals, ', ')}})')
var deleteRules = map(bindings, binding => '(@Resource[Microsoft.Authorization/roleAssignments:RoleDefinitionId] GuidEquals ${binding.role} AND @Resource[Microsoft.Authorization/roleAssignments:PrincipalId] ForAnyOfAnyValues:GuidEquals {${join(binding.principals, ', ')}})')
var noDelegation = '(!(ActionMatches{\'Microsoft.Authorization/roleAssignments/write\'})) AND (!(ActionMatches{\'Microsoft.Authorization/roleAssignments/delete\'}))'
var limitedDelegation = '((!(ActionMatches{\'Microsoft.Authorization/roleAssignments/write\'})) OR (${join(writeRules, ' OR ')})) AND ((!(ActionMatches{\'Microsoft.Authorization/roleAssignments/delete\'})) OR (${join(deleteRules, ' OR ')}))'
resource writer 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(resourceGroup().id, principalId, contributor)
  properties: { principalId: principalId, principalType: 'ServicePrincipal', roleDefinitionId: subscriptionResourceId('Microsoft.Authorization/roleDefinitions', contributor) }
}
// Keep the original assignment name so incremental deployment RESTRICTS the
// existing grant, rather than leaving an unrestricted assignment alongside it.
resource delegate 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(resourceGroup().id, principalId, administrator)
  properties: {
    principalId: principalId
    principalType: 'ServicePrincipal'
    roleDefinitionId: subscriptionResourceId('Microsoft.Authorization/roleDefinitions', administrator)
    conditionVersion: '2.0'
    condition: empty(bindings) ? noDelegation : limitedDelegation
  }
}
