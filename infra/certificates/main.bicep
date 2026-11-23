targetScope = 'subscription'

@allowed(['staging', 'production'])
param environment string
param location string = 'westeurope'
param owner string
param costCenter string = 'tempo'
@description('Enable only after building the immutable image in the provisioned private ACR.')
param deployCompute bool = false
@description('Private ACR image reference, including @sha256 digest.')
param image string = ''
param dnsDomain string = 'tinyrobotcompany.io'
param deviceSuffix string = 'tempo.tinyrobotcompany.io'

var prefix = 'tempo-${environment}'
var unique = uniqueString(subscription().id, environment, location)
var concerns = ['network', 'data', 'security', 'ops', 'compute']
resource groups 'Microsoft.Resources/resourceGroups@2024-03-01' = [for concern in concerns: {
  name: 'rg-${prefix}-${location}-${concern}'
  location: location
  tags: { concern: concern, environment: environment, owner: owner, 'cost-center': costCenter }
}]
module network 'network.bicep' = {
  name: '${prefix}-network'
  scope: groups[0]
  params: { location: location, prefix: prefix }
}
module security 'security.bicep' = {
  name: '${prefix}-security'
  scope: groups[2]
  params: { location: location, unique: unique, prefix: prefix }
}
module data 'data.bicep' = {
  name: '${prefix}-data'
  scope: groups[1]
  params: {
    location: location
    unique: unique
    apiPrincipal: security.outputs.apiPrincipal
    workerPrincipal: security.outputs.workerPrincipal
    factoryPrincipal: security.outputs.factoryPrincipal
    bootstrapPrincipal: security.outputs.bootstrapPrincipal
  }
}
module endpoints 'endpoints.bicep' = {
  name: '${prefix}-private-endpoints'
  scope: groups[2]
  params: {
    location: location
    subnet: network.outputs.privateSubnet
    tableZone: network.outputs.tableZone
    vaultZone: network.outputs.vaultZone
    storageId: data.outputs.storageId
    vaultId: security.outputs.vaultId
  }
}
module ops 'ops.bicep' = {
  name: '${prefix}-ops'
  scope: groups[3]
  params: { location: location, prefix: prefix }
}
module compute 'compute.bicep' = if (deployCompute) {
  name: '${prefix}-compute'
  scope: groups[4]
  params: {
    location: location
    prefix: prefix
    image: image
    registry: data.outputs.registry
    subnet: network.outputs.computeSubnet
    logWorkspaceId: ops.outputs.workspaceId
    logWorkspaceKey: ops.outputs.sharedKey
    apiIdentity: security.outputs.apiIdentity
    apiClient: security.outputs.apiClient
    workerIdentity: security.outputs.workerIdentity
    workerClient: security.outputs.workerClient
    factoryIdentity: security.outputs.factoryIdentity
    factoryClient: security.outputs.factoryClient
    bootstrapIdentity: security.outputs.bootstrapIdentity
    bootstrapClient: security.outputs.bootstrapClient
    tableEndpoint: data.outputs.endpoint
    vaultUrl: security.outputs.vaultUrl
    dnsDomain: dnsDomain
    deviceSuffix: deviceSuffix
    environment: environment
  }
  dependsOn: [endpoints]
}
output enrollmentUrl string = deployCompute ? 'https://${compute!.outputs.hostname}/v1/device' : ''
output keyVaultName string = security.outputs.vaultName
output factoryJob string = deployCompute ? compute!.outputs.factoryJob : ''
output computeResourceGroup string = groups[4].name
output containerRegistry string = data.outputs.registry
