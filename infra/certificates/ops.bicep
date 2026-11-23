param location string
param prefix string
resource workspace 'Microsoft.OperationalInsights/workspaces@2023-09-01' = {
  name: '${prefix}-logs'
  location: location
  properties: { sku: { name: 'PerGB2018' }, retentionInDays: 30, workspaceCapping: { dailyQuotaGb: 1 } }
}
output workspaceId string = workspace.properties.customerId
@secure()
output sharedKey string = workspace.listKeys().primarySharedKey
