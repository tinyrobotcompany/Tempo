targetScope = 'subscription'

// One Azure environment. ACME test/live selection does not create another stack.
@allowed(['westeurope'])
param location string = 'westeurope'
param deployCompute bool = false
param image string = ''
@allowed(['staging', 'production'])
param acmeEnvironment string = 'staging'
param dnsDomain string = 'tinyrobotcompany.io'
param deviceSuffix string = 'tempo.tinyrobotcompany.io'
// Preserve existing resource names/data during the resource-group-only migration.
var prefix = 'tempo-staging'
var unique = uniqueString(subscription().id, 'staging', location)
var concerns = ['network', 'data', 'security', 'ops', 'compute']
// Groups and GitHub federation are established once by bootstrap.bicep.
resource groups 'Microsoft.Resources/resourceGroups@2024-03-01' existing = [for concern in concerns: {
  name: 'rg-tempo-${location}-${concern}'
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
    environment: acmeEnvironment
    bootstrapContactEmail: ''
    bootstrapTermsUrl: ''
    bootstrapDnsCredentials: ''
  }
  dependsOn: [endpoints]
}
output enrollmentUrl string = deployCompute ? 'https://${compute!.outputs.hostname}/v1/device' : ''
output keyVaultName string = security.outputs.vaultName
output factoryJob string = deployCompute ? compute!.outputs.factoryJob : ''
output computeResourceGroup string = groups[4].name
output containerRegistry string = data.outputs.registry
