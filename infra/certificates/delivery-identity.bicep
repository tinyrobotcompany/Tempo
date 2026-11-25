param location string
resource identity 'Microsoft.ManagedIdentity/userAssignedIdentities@2023-01-31' = {
  name: 'tempo-infrastructure-delivery'
  location: location
}
resource federation 'Microsoft.ManagedIdentity/userAssignedIdentities/federatedIdentityCredentials@2023-01-31' = {
  parent: identity
  name: 'github-approved-infrastructure'
  properties: {
    issuer: 'https://token.actions.githubusercontent.com'
    // GitHub's immutable subject binds both names to their numeric IDs.
    subject: 'repo:tinyrobotcompany@326183923/Tempo@1458582269:environment:tempo-infrastructure'
    audiences: ['api://AzureADTokenExchange']
  }
}
output principalId string = identity.properties.principalId
output clientId string = identity.properties.clientId
