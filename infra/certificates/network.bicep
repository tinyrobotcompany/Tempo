param location string
param prefix string
resource vnet 'Microsoft.Network/virtualNetworks@2024-05-01' = {
  name: '${prefix}-vnet'
  location: location
  properties: {
    addressSpace: { addressPrefixes: ['10.62.0.0/16'] }
    subnets: [
      { name: 'compute', properties: { addressPrefix: '10.62.0.0/23', delegations: [
        { name: 'container-apps', properties: { serviceName: 'Microsoft.App/environments' } }
      ] } }
      { name: 'private', properties: { addressPrefix: '10.62.2.0/24', privateEndpointNetworkPolicies: 'Disabled' } }
    ]
  }
}
resource zones 'Microsoft.Network/privateDnsZones@2024-06-01' = [for name in ['privatelink.table.${environment().suffixes.storage}', 'privatelink.vaultcore.azure.net', 'privatelink.blob.${environment().suffixes.storage}']: {
  name: name
  location: 'global'
}]
resource links 'Microsoft.Network/privateDnsZones/virtualNetworkLinks@2024-06-01' = [for (name, i) in ['table', 'vault', 'blob']: {
  name: '${zones[i].name}/${prefix}-${name}'
  location: 'global'
  properties: { virtualNetwork: { id: vnet.id }, registrationEnabled: false }
}]
output computeSubnet string = '${vnet.id}/subnets/compute'
output privateSubnet string = '${vnet.id}/subnets/private'
output tableZone string = zones[0].id
output vaultZone string = zones[1].id
output blobZone string = zones[2].id
