param storageName string
resource storage 'Microsoft.Storage/storageAccounts@2023-05-01' existing = { name: storageName }
resource deletionLock 'Microsoft.Authorization/locks@2020-05-01' = {
  name: 'tempo-preserve-device-registry'
  scope: storage
  properties: { level: 'CanNotDelete', notes: 'Protect the fleet registry resource. Does not protect Table entities; verified isolated backups are also required.' }
}
