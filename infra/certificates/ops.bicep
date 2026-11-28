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
// Alerts match service log events by NAME, whatever emits them: Container Apps
// console output or Application Insights traces (e.g. Azure Functions). isfuzzy
// tolerates a table that does not exist yet. These rules are scoped to the Log
// Analytics WORKSPACE, where workspace-based Application Insights stores traces
// in `AppTraces` (column `Message`); `traces` is only the name inside a classic
// Application Insights resource query and does not exist in a workspace.
// https://learn.microsoft.com/azure/azure-monitor/reference/tables/apptraces Contract: the log message contains
// the exact snake_case event name (see docs/DEVICE_CERTIFICATE_SERVICE.md).
// Every rule is stateful: one email when it fires, one when it resolves.
var logs = 'union isfuzzy=true (ContainerAppConsoleLogs_CL | project TimeGenerated, Message = Log_s), (AppTraces | project TimeGenerated, Message)'
var alerts = [
  { name: 'worker-cycle-failed', severity: 1, events: ['worker_cycle_failed'], minimum: 1, window: 'PT30M', every: 'PT15M'
    text: 'A background worker cycle failed (store, Key Vault or DNS credentials unavailable).' }
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
      query: '${logs} | where ${join(map(alert.events, event => 'Message contains_cs \'${event}\''), ' or ')}'
      timeAggregation: 'Count'
      operator: 'GreaterThanOrEqual'
      threshold: alert.minimum
      failingPeriods: { numberOfEvaluationPeriods: 1, minFailingPeriodsToAlert: 1 }
    }] }
    actions: { actionGroups: [actionGroup.id] }
  }
}]
