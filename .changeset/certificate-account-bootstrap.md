---
"tempo": patch
---

Add an approval-gated, manually started workflow that runs the certificate service's CA bootstrap job once. It takes the Spaceship DNS credential, contact email and accepted Let's Encrypt terms URL from repository secrets. The bootstrap job stores the DNS credential and ACME account in the private Key Vault, and the credential is removed from the job afterwards, including on failure. No firmware, device or Bicep resource changes.
