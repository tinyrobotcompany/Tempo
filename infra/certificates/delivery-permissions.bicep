param principalId string
@description('Built-in role definition GUIDs the workload template assigns in this group. Empty denies all role administration.')
param delegableRoles array = []
var contributor = 'b24988ac-6180-42a0-ab88-20f7382dd24c'
var administrator = 'f58310d9-a9f6-439a-9e8d-f62e7b41a168'
var writeAction = '!(ActionMatches{\'Microsoft.Authorization/roleAssignments/write\'})'
var deleteAction = '!(ActionMatches{\'Microsoft.Authorization/roleAssignments/delete\'})'
var roleSet = join(delegableRoles, ', ')
// Writes: only the listed runtime roles, only to service principals, never to the
// delivery identity itself. Deletes: only assignments of the listed roles.
var limitedDelegation = '((${writeAction}) OR (@Request[Microsoft.Authorization/roleAssignments:RoleDefinitionId] ForAnyOfAnyValues:GuidEquals {${roleSet}} AND @Request[Microsoft.Authorization/roleAssignments:PrincipalType] ForAnyOfAnyValues:StringEqualsIgnoreCase {\'ServicePrincipal\'} AND @Request[Microsoft.Authorization/roleAssignments:PrincipalId] ForAnyOfAllValues:GuidNotEquals {${principalId}})) AND ((${deleteAction}) OR (@Resource[Microsoft.Authorization/roleAssignments:RoleDefinitionId] ForAnyOfAnyValues:GuidEquals {${roleSet}}))'
var noDelegation = '(${writeAction}) AND (${deleteAction})'
// Contributor cannot assign roles. Names match the earlier unconditioned grants,
// so re-running bootstrap replaces them in place rather than adding new ones.
resource writer 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(resourceGroup().id, principalId, contributor)
  properties: { principalId: principalId, principalType: 'ServicePrincipal', roleDefinitionId: subscriptionResourceId('Microsoft.Authorization/roleDefinitions', contributor) }
}
resource delegate 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(resourceGroup().id, principalId, administrator)
  properties: {
    principalId: principalId
    principalType: 'ServicePrincipal'
    roleDefinitionId: subscriptionResourceId('Microsoft.Authorization/roleDefinitions', administrator)
    description: 'Tempo delivery: assign only the workload runtime roles in this group'
    conditionVersion: '2.0'
    condition: empty(delegableRoles) ? noDelegation : limitedDelegation
  }
}
