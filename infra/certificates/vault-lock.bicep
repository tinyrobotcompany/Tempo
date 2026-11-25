param vaultName string
resource vault 'Microsoft.KeyVault/vaults@2023-07-01' existing = { name: vaultName }
resource deletionLock 'Microsoft.Authorization/locks@2020-05-01' = {
  name: 'tempo-preserve-certificate-secrets'
  scope: vault
  properties: { level: 'CanNotDelete', notes: 'Protect the vault resource. Secret deletion/rotation and disaster recovery remain separate controls.' }
}
