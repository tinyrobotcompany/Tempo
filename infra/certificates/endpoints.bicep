param location string
param prefix string
param subnet string
param storageId string
param vaultId string
param tableZone string
param vaultZone string
var targets = [
  { name: 'table', id: storageId, zone: tableZone }
  { name: 'vault', id: vaultId, zone: vaultZone }
]
resource endpoints 'Microsoft.Network/privateEndpoints@2024-05-01' = [for target in targets: {
  name: '${prefix}-${target.name}-endpoint'
  location: location
  properties: {
    subnet: { id: subnet }
    privateLinkServiceConnections: [
      { name: target.name, properties: { privateLinkServiceId: target.id, groupIds: [target.name] } }
    ]
  }
}]
resource zoneGroups 'Microsoft.Network/privateEndpoints/privateDnsZoneGroups@2024-05-01' = [for (target, i) in targets: {
  name: '${endpoints[i].name}/default'
  properties: { privateDnsZoneConfigs: [{ name: target.name, properties: { privateDnsZoneId: target.zone } }] }
}]
