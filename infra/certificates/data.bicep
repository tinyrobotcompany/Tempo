param location string
@minLength(13)
@maxLength(13)
param unique string
@minLength(2)
@maxLength(8)
param project string
param apiPrincipal string
param workerPrincipal string
param factoryPrincipal string
param bootstrapPrincipal string
resource storage 'Microsoft.Storage/storageAccounts@2023-05-01' = {
  name: 'st${project}${unique}'
  location: location
  kind: 'StorageV2'
  sku: { name: 'Standard_ZRS' }
  properties: {
    minimumTlsVersion: 'TLS1_2'
    supportsHttpsTrafficOnly: true
    allowBlobPublicAccess: false
    allowSharedKeyAccess: false
    publicNetworkAccess: 'Disabled'
    networkAcls: { defaultAction: 'Deny', bypass: 'None' }
  }
}
resource service 'Microsoft.Storage/storageAccounts/tableServices@2023-05-01' = {
  parent: storage
  name: 'default'
}
resource registry 'Microsoft.Storage/storageAccounts/tableServices/tables@2023-05-01' = {
  parent: service
  name: 'DeviceRegistry'
}
resource state 'Microsoft.Storage/storageAccounts/tableServices/tables@2023-05-01' = {
  parent: service
  name: 'DeviceState'
}
var tableReader = subscriptionResourceId('Microsoft.Authorization/roleDefinitions', '76199698-9eea-4c19-bc75-cec21354c6b6')
var tableWriter = subscriptionResourceId('Microsoft.Authorization/roleDefinitions', '0a9a7e1f-b9d0-4cc4-a60d-0319b160aaa3')
resource readRegistry 'Microsoft.Authorization/roleAssignments@2022-04-01' = [for principal in [apiPrincipal, workerPrincipal]: {
  name: guid(registry.id, principal, 'reader')
  scope: registry
  properties: { principalId: principal, principalType: 'ServicePrincipal', roleDefinitionId: tableReader }
}]
resource writeState 'Microsoft.Authorization/roleAssignments@2022-04-01' = [for principal in [apiPrincipal, workerPrincipal]: {
  name: guid(state.id, principal, 'writer')
  scope: state
  properties: { principalId: principal, principalType: 'ServicePrincipal', roleDefinitionId: tableWriter }
}]
resource manufacture 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(registry.id, factoryPrincipal, 'writer')
  scope: registry
  properties: { principalId: factoryPrincipal, principalType: 'ServicePrincipal', roleDefinitionId: tableWriter }
}
output storageId string = storage.id
output endpoint string = storage.properties.primaryEndpoints.table
resource containerRegistry 'Microsoft.ContainerRegistry/registries@2025-04-01' = {
  name: 'acr${project}${unique}'
  location: location
  sku: { name: 'Basic' }
  properties: { adminUserEnabled: false, anonymousPullEnabled: false, publicNetworkAccess: 'Enabled' }
}
resource pull 'Microsoft.Authorization/roleAssignments@2022-04-01' = [for principal in [apiPrincipal, workerPrincipal, factoryPrincipal, bootstrapPrincipal]: {
  name: guid(containerRegistry.id, principal, 'pull')
  scope: containerRegistry
  properties: {
    principalId: principal
    principalType: 'ServicePrincipal'
    roleDefinitionId: subscriptionResourceId('Microsoft.Authorization/roleDefinitions', '7f951dda-4ed3-4680-a7ca-43fe172d538d')
  }
}]
output registry string = containerRegistry.properties.loginServer

output storageName string = storage.name
