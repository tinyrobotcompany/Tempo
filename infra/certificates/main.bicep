targetScope = 'subscription'

// CI previews this template using what-if --no-pretty-print for machine-readable JSON.

// One Azure environment. ACME test/live selection does not create another stack.
@minLength(2)
@maxLength(8)
@description('Lowercase alphanumeric project name, used to derive resource names.')
param project string = 'tempo'
@allowed(['westeurope'])
param location string = 'westeurope'
@description('Stable name generation. Change only for an intentional fresh rebuild, never on each deployment.')
param generation string = '1'
@description('Reviewed image digest from the image-build workflow; empty deploys the foundation only.')
param imageDigest string = ''
var deployCompute = !empty(imageDigest)
@allowed(['staging', 'production'])
param acmeEnvironment string = 'staging'
param dnsDomain string = 'tinyrobotcompany.io'
param deviceSuffix string = 'tempo.tinyrobotcompany.io'
var prefix = '${project}-${location}'
var unique = uniqueString(subscription().id, project, location, generation)
var concerns = ['network', 'data', 'security', 'ops', 'compute']
// Groups and GitHub federation are established once by bootstrap.bicep.
resource groups 'Microsoft.Resources/resourceGroups@2024-03-01' existing = [for concern in concerns: {
  name: 'rg-${project}-${location}-${concern}'
}]
module network 'network.bicep' = {
  name: '${prefix}-network'
  scope: groups[0]
  params: { location: location, prefix: prefix }
}
module security 'security.bicep' = {
  name: '${prefix}-security'
  scope: groups[2]
  params: { location: location, unique: unique, prefix: prefix, project: project }
}
module data 'data.bicep' = {
  name: '${prefix}-data'
  scope: groups[1]
  params: {
    location: location
    unique: unique
    project: project
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
    prefix: prefix
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
    image: '${data.outputs.registry}/tempo-certificates@sha256:${imageDigest}'
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

output resourceGroups array = [for concern in concerns: 'rg-${project}-${location}-${concern}']
output storageAccountName string = data.outputs.storageName
output apiName string = deployCompute ? '${prefix}-api' : ''
output workerName string = deployCompute ? '${prefix}-worker' : ''
