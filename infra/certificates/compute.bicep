param location string
param prefix string
param image string
param registry string
param subnet string
param logWorkspaceId string
@secure()
param logWorkspaceKey string
param apiIdentity string
param apiClient string
param workerIdentity string
param workerClient string
param factoryIdentity string
param factoryClient string
param bootstrapIdentity string
param bootstrapClient string
param backupIdentity string
param backupClient string
param restoreIdentity string
param restoreClient string
param backupEndpoint string
param backupContainer string
param tableEndpoint string
param vaultUrl string
param dnsDomain string
param deviceSuffix string
param environment string
param bootstrapContactEmail string
param bootstrapTermsUrl string
@secure()
param bootstrapDnsCredentials string

resource managed 'Microsoft.App/managedEnvironments@2025-01-01' = {
  name: '${prefix}-environment'
  location: location
  properties: {
    vnetConfiguration: { infrastructureSubnetId: subnet }
    workloadProfiles: [{ name: 'Consumption', workloadProfileType: 'Consumption' }]
    appLogsConfiguration: { destination: 'log-analytics', logAnalyticsConfiguration: {
      customerId: logWorkspaceId, sharedKey: logWorkspaceKey
    } }
  }
}
var common = [{ name: 'TABLE_ENDPOINT', value: replace(tableEndpoint, '.net/', '.net') }, { name: 'DEVICE_SUFFIX', value: deviceSuffix }]
resource api 'Microsoft.App/containerApps@2025-01-01' = {
  name: '${prefix}-api'
  location: location
  identity: { type: 'UserAssigned', userAssignedIdentities: { '${apiIdentity}': {} } }
  properties: {
    environmentId: managed.id
    configuration: {
      activeRevisionsMode: 'Single'
      registries: [{ server: registry, identity: apiIdentity }]
      ingress: { external: true, targetPort: 8080, allowInsecure: false, transport: 'http' }
    }
    template: {
      containers: [{ name: 'api', image: image, resources: { cpu: json('0.25'), memory: '0.5Gi' }
        env: concat(common, [{ name: 'AZURE_CLIENT_ID', value: apiClient }])
        probes: [{ type: 'Liveness', httpGet: { path: '/health', port: 8080 }, initialDelaySeconds: 10, periodSeconds: 30 }]
      }]
      scale: { minReplicas: 0, maxReplicas: 3, rules: [{ name: 'http', http: { metadata: { concurrentRequests: '20' } } }] }
    }
  }
}
resource worker 'Microsoft.App/containerApps@2025-01-01' = {
  name: '${prefix}-worker'
  location: location
  identity: { type: 'UserAssigned', userAssignedIdentities: { '${workerIdentity}': {} } }
  properties: {
    environmentId: managed.id
    configuration: { activeRevisionsMode: 'Single', registries: [{ server: registry, identity: workerIdentity }] }
    template: {
      containers: [{ name: 'worker', image: image, command: ['python', '-m', 'tempo_certificates.runtime']
        resources: { cpu: json('0.25'), memory: '0.5Gi' }
        env: concat(common, [
          { name: 'AZURE_CLIENT_ID', value: workerClient }
          { name: 'KEY_VAULT_URL', value: vaultUrl }
          { name: 'DNS_DOMAIN', value: dnsDomain }
          { name: 'ACME_ENVIRONMENT', value: environment }
        ])
      }]
      scale: { minReplicas: 1, maxReplicas: 1 }
    }
  }
}
// Factory admission runs inside the private network, under a separate identity.
// Only factory operators may start/override this job; runtime identities cannot.
resource factory 'Microsoft.App/jobs@2025-01-01' = {
  name: '${prefix}-factory'
  location: location
  identity: { type: 'UserAssigned', userAssignedIdentities: { '${factoryIdentity}': {} } }
  properties: {
    environmentId: managed.id
    configuration: { triggerType: 'Manual', replicaTimeout: 120, replicaRetryLimit: 0
      registries: [{ server: registry, identity: factoryIdentity }]
      manualTriggerConfig: { replicaCompletionCount: 1, parallelism: 1 } }
    template: {
      containers: [{ name: 'factory', image: image, command: ['python', '-m', 'tempo_certificates.manufacturing']
        resources: { cpu: json('0.25'), memory: '0.5Gi' }
        env: concat(common, [{ name: 'AZURE_CLIENT_ID', value: factoryClient }])
      }]
    }
  }
}
output hostname string = api.properties.configuration.ingress.fqdn
output factoryJob string = factory.name
resource bootstrap 'Microsoft.App/jobs@2025-01-01' = {
  name: '${prefix}-bootstrap'
  location: location
  identity: { type: 'UserAssigned', userAssignedIdentities: { '${bootstrapIdentity}': {} } }
  properties: {
    environmentId: managed.id
    configuration: { triggerType: 'Manual', replicaTimeout: 120, replicaRetryLimit: 0
      secrets: empty(bootstrapDnsCredentials) ? [] : [{ name: 'dns-bootstrap', value: bootstrapDnsCredentials }]
      registries: [{ server: registry, identity: bootstrapIdentity }]
      manualTriggerConfig: { replicaCompletionCount: 1, parallelism: 1 } }
    template: {
      containers: [{
        name: 'bootstrap'
        image: image
        command: ['python', '-m', 'tempo_certificates.bootstrap']
        resources: { cpu: json('0.25'), memory: '0.5Gi' }
        env: concat([
          { name: 'AZURE_CLIENT_ID', value: bootstrapClient }
          { name: 'KEY_VAULT_URL', value: vaultUrl }
          { name: 'ACME_ENVIRONMENT', value: environment }
          { name: 'ACME_CONTACT_EMAIL', value: bootstrapContactEmail }
          { name: 'ACCEPTED_ACME_TERMS_URL', value: bootstrapTermsUrl }
        ], empty(bootstrapDnsCredentials) ? [] : [{ name: 'SPACESHIP_CREDENTIALS', secretRef: 'dns-bootstrap' }])
      }]
    }
  }
}
// Daily DeviceRegistry export (03:17 UTC) and its manual, dry-run-by-default restore.
// Both run in the private environment; storage stays reachable only via private endpoints.
var backupEnv = concat(common, [
  { name: 'BACKUP_BLOB_ENDPOINT', value: backupEndpoint }
  { name: 'BACKUP_CONTAINER', value: backupContainer }
])
resource backupJob 'Microsoft.App/jobs@2025-01-01' = {
  name: '${prefix}-registry-backup'
  location: location
  identity: { type: 'UserAssigned', userAssignedIdentities: { '${backupIdentity}': {} } }
  properties: {
    environmentId: managed.id
    configuration: { triggerType: 'Schedule', replicaTimeout: 300, replicaRetryLimit: 1
      registries: [{ server: registry, identity: backupIdentity }]
      scheduleTriggerConfig: { cronExpression: '17 3 * * *', replicaCompletionCount: 1, parallelism: 1 } }
    template: {
      containers: [{ name: 'backup', image: image, command: ['python', '-m', 'tempo_certificates.backup', 'export']
        resources: { cpu: json('0.25'), memory: '0.5Gi' }
        env: concat(backupEnv, [{ name: 'AZURE_CLIENT_ID', value: backupClient }])
      }]
    }
  }
}
resource restoreJob 'Microsoft.App/jobs@2025-01-01' = {
  name: '${prefix}-registry-restore'
  location: location
  identity: { type: 'UserAssigned', userAssignedIdentities: { '${restoreIdentity}': {} } }
  properties: {
    environmentId: managed.id
    configuration: { triggerType: 'Manual', replicaTimeout: 300, replicaRetryLimit: 0
      registries: [{ server: registry, identity: restoreIdentity }]
      manualTriggerConfig: { replicaCompletionCount: 1, parallelism: 1 } }
    template: {
      containers: [{ name: 'restore', image: image, command: ['python', '-m', 'tempo_certificates.backup', 'restore']
        resources: { cpu: json('0.25'), memory: '0.5Gi' }
        // Reset by every deployment: no backup selected and dry run.
        env: concat(backupEnv, [
          { name: 'AZURE_CLIENT_ID', value: restoreClient }
          { name: 'RESTORE_BLOB', value: '' }
          { name: 'RESTORE_APPLY', value: 'no' }
        ])
      }]
    }
  }
}
