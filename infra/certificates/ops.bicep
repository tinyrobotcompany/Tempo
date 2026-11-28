param location string
param prefix string
@secure()
@minLength(6)
@description('Alert recipient from the ALERT_EMAIL repository secret; never a literal in this repository.')
param alertEmail string
resource workspace 'Microsoft.OperationalInsights/workspaces@2023-09-01' = {
  name: '${prefix}-logs'
  location: location
  properties: { sku: { name: 'PerGB2018' }, retentionInDays: 30, workspaceCapping: { dailyQuotaGb: 1 } }
}
output workspaceId string = workspace.properties.customerId
@secure()
output sharedKey string = workspace.listKeys().primarySharedKey

resource actionGroup 'Microsoft.Insights/actionGroups@2023-01-01' = {
  name: '${prefix}-alerts'
  location: 'global'
  properties: {
    groupShortName: 'tempoalerts'
    enabled: true
    emailReceivers: [{ name: 'operator', emailAddress: alertEmail, useCommonAlertSchema: true }]
  }
}
// Service log events (stderr of the Container Apps, via the legacy console table).
// Every rule is stateful: one email when it fires, one when it resolves.
var alerts = [
  { name: 'worker-cycle-failed', severity: 1, events: ['worker_cycle_failed'], minimum: 1, window: 'PT30M', every: 'PT15M'
    text: 'A worker lane failed a whole cycle (store, Key Vault or DNS credentials unavailable).' }
  { name: 'device-job-failed', severity: 2, events: ['device_job_failed'], minimum: 3, window: 'PT1H', every: 'PT15M'
    text: 'Three or more certificate/DNS job attempts failed within one hour.' }
  { name: 'device-job-invalid', severity: 2, events: ['device_job_invalid'], minimum: 1, window: 'PT30M', every: 'PT15M'
    text: 'A persisted job was invalid and stopped before provider work.' }
  { name: 'nonce-cleanup-failed', severity: 2, events: ['nonce_cleanup_failed'], minimum: 1, window: 'PT30M', every: 'PT15M'
    text: 'Expired replay nonces could not be deleted; API admission is being denied.' }
  { name: 'nonce-cleanup-budget', severity: 3, events: ['nonce_cleanup_budget_reached'], minimum: 1, window: 'PT30M', every: 'PT15M'
    text: 'A nonce sweep hit its deletion cap; a backlog of expired nonces is building.' }
  { name: 'certificate-expiring', severity: 1, events: ['certificate_expiring_soon', 'certificate_expiry_check_failed'], minimum: 1, window: 'PT2H', every: 'PT1H'
    text: 'An enabled device certificate expires within 14 days (renewal is stuck), or the hourly expiry check failed.' }
  { name: 'ca-rate-limited', severity: 1, events: ['ca_rate_limited'], minimum: 1, window: 'PT30M', every: 'PT15M'
    text: 'The certificate authority returned an ACME rateLimited error.' }
  { name: 'registry-backup-failed', severity: 2, events: ['registry_backup_failed'], minimum: 1, window: 'PT30M', every: 'PT15M'
    text: 'The daily DeviceRegistry export failed.' }
]
resource rules 'Microsoft.Insights/scheduledQueryRules@2023-12-01' = [for alert in alerts: {
  name: '${prefix}-${alert.name}'
  location: location
  properties: {
    displayName: '${prefix} ${alert.name}'
    description: alert.text
    severity: alert.severity
    enabled: true
    scopes: [workspace.id]
    evaluationFrequency: alert.every
    windowSize: alert.window
    autoMitigate: true
    criteria: { allOf: [{
      query: 'ContainerAppConsoleLogs_CL | where ${join(map(alert.events, event => 'Log_s contains_cs \'${event}\''), ' or ')}'
      timeAggregation: 'Count'
      operator: 'GreaterThanOrEqual'
      threshold: alert.minimum
      failingPeriods: { numberOfEvaluationPeriods: 1, minFailingPeriodsToAlert: 1 }
    }] }
    actions: { actionGroups: [actionGroup.id] }
  }
}]
// Absence alert: no completed DeviceRegistry export in the last day (job missing,
// image without the backup module, schedule not running, or every attempt failed).
resource backupMissing 'Microsoft.Insights/scheduledQueryRules@2023-12-01' = {
  name: '${prefix}-registry-backup-missing'
  location: location
  properties: {
    displayName: '${prefix} registry-backup-missing'
    description: 'No successful DeviceRegistry export was logged in the last 24 hours.'
    severity: 2
    enabled: true
    scopes: [workspace.id]
    evaluationFrequency: 'PT1H'
    windowSize: 'P1D'
    autoMitigate: true
    criteria: { allOf: [{
      query: 'ContainerAppConsoleLogs_CL | where Log_s contains_cs \'registry_backup_completed\''
      timeAggregation: 'Count'
      operator: 'LessThan'
      threshold: 1
      failingPeriods: { numberOfEvaluationPeriods: 1, minFailingPeriodsToAlert: 1 }
    }] }
    actions: { actionGroups: [actionGroup.id] }
  }
}
