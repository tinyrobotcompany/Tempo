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
param backupPrincipal string
param restorePrincipal string
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
resource readRegistry 'Microsoft.Authorization/roleAssignments@2022-04-01' = [for principal in [apiPrincipal, workerPrincipal, backupPrincipal]: {
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
resource pull 'Microsoft.Authorization/roleAssignments@2022-04-01' = [for principal in [apiPrincipal, workerPrincipal, factoryPrincipal, bootstrapPrincipal, backupPrincipal, restorePrincipal]: {
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

// DeviceRegistry backups: a separate geo-redundant private account, Entra-only,
// write-once for 90 days, deleted by lifecycle after a year.
resource backupStorage 'Microsoft.Storage/storageAccounts@2023-05-01' = {
  name: take('stbk${project}${unique}', 24)
  location: location
  kind: 'StorageV2'
  sku: { name: 'Standard_GRS' }
  properties: {
    minimumTlsVersion: 'TLS1_2'
    supportsHttpsTrafficOnly: true
    allowBlobPublicAccess: false
    allowSharedKeyAccess: false
    allowCrossTenantReplication: false
    publicNetworkAccess: 'Disabled'
    networkAcls: { defaultAction: 'Deny', bypass: 'None' }
  }
}
resource blobs 'Microsoft.Storage/storageAccounts/blobServices@2023-05-01' = {
  parent: backupStorage
  name: 'default'
  properties: {
    deleteRetentionPolicy: { enabled: true, days: 30 }
    containerDeleteRetentionPolicy: { enabled: true, days: 30 }
  }
}
resource backups 'Microsoft.Storage/storageAccounts/blobServices/containers@2023-05-01' = {
  parent: blobs
  name: 'device-registry'
  properties: { publicAccess: 'None' }
}
resource worm 'Microsoft.Storage/storageAccounts/blobServices/containers/immutabilityPolicies@2023-05-01' = {
  parent: backups
  name: 'default'
  properties: { immutabilityPeriodSinceCreationInDays: 90, allowProtectedAppendWrites: false }
}
resource lifecycle 'Microsoft.Storage/storageAccounts/managementPolicies@2023-05-01' = {
  parent: backupStorage
  name: 'default'
  properties: { policy: { rules: [{ name: 'expire-registry-backups', enabled: true, type: 'Lifecycle', definition: {
    filters: { blobTypes: ['blockBlob'], prefixMatch: ['device-registry/device-registry/'] }
    actions: { baseBlob: { delete: { daysAfterCreationGreaterThan: 365 } } }
  } }] } }
}
var blobWriter = subscriptionResourceId('Microsoft.Authorization/roleDefinitions', 'ba92f5b4-2d11-453d-a403-e96b0029c9fe')
var blobReader = subscriptionResourceId('Microsoft.Authorization/roleDefinitions', '2a2b9908-6ea1-4ae2-8e65-a410df84e7d1')
// Export: add blobs to this container only. The condition removes delete, so even
// after the 90-day lock expires the job identity cannot destroy older backups.
resource writeBackups 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(backups.id, backupPrincipal, 'writer')
  scope: backups
  properties: {
    principalId: backupPrincipal
    principalType: 'ServicePrincipal'
    roleDefinitionId: blobWriter
    conditionVersion: '2.0'
    condition: '(!(ActionMatches{\'Microsoft.Storage/storageAccounts/blobServices/containers/blobs/delete\'}))'
  }
}
// Restore (manual job): read backups, add missing registry rows.
resource readBackups 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(backups.id, restorePrincipal, 'reader')
  scope: backups
  properties: { principalId: restorePrincipal, principalType: 'ServicePrincipal', roleDefinitionId: blobReader }
}
resource restoreRegistry 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(registry.id, restorePrincipal, 'writer')
  scope: registry
  properties: { principalId: restorePrincipal, principalType: 'ServicePrincipal', roleDefinitionId: tableWriter }
}
output backupStorageId string = backupStorage.id
output backupEndpoint string = backupStorage.properties.primaryEndpoints.blob
output backupContainer string = backups.name
