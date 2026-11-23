# Secure phone access delivery checklist

- [x] HTTPS-only phone server and physical pairing revocation.
- [x] Device-generated identity/TLS keys; public-only manufacturing export.
- [x] Signed CSR enrollment, renewal and address-update client.
- [x] Independent certificate verification and atomic certificate replacement.
- [x] Production Secure Boot/flash-encryption prerequisite checks.
- [x] Remove laptop private-key/certificate import tooling.
- [x] Companion Azure service, manufacturing admission and DNS-01 automation source.
- [ ] Complete full regression suite and per-file coverage after integration.
- [ ] Select Azure deployment subscription and approve deployment parameters.
- [ ] Provision scoped DNS credentials directly to cloud Key Vault.
- [ ] Bootstrap ACME service account in Azure after subscriber agreement approval.
- [ ] Configure production enrollment endpoint in signed firmware.
- [ ] Review and execute board-specific manufacturing/security migration.
- [ ] Real-device/browser/router issuance, renewal, DHCP and outage acceptance.

No shipping or merge claim should be inferred while live prerequisites remain.
Per-phone expiring sessions are separate from device certificate enrollment.
