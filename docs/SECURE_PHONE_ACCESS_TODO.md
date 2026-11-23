# Secure phone access implementation

Branch: feature/secure-phone-access, based on main 8d0c800.

- [x] HTTPS-only control server; no HTTP fallback when identity is missing or invalid.
- [x] Unique device identity provisioning, without modifying settings or OTA slots.
- [x] Hostname, certificate/key matching and credential provisioning validation.
- [x] Local certificate-authority test procedure; public-certificate import procedure.
- [x] Revocable pairing without breaking ordinary refresh/restart reconnection.
- [x] Negative-path tests, full coverage gates and ESP32-P4 build.
- [x] Domain/DNS and certificate renewal deployment instructions.
- [ ] Live certificate trust and browser checks on the physical device.

The user owns tinyrobotcompany.io; tempo.tinyrobotcompany.io is the proposed
namespace. DNS-provider access, public issuance and production backend deployment
are not yet configured. Never share one private key among devices,
commit private material, bypass certificate validation, or enable irreversible eFuse
settings as part of this prototype change.

Production follow-up (not implemented by the HTTPS prototype):

- [ ] Device-generated private keys and authenticated CSR enrollment.
- [ ] Automated DNS updates and certificate renewal service.
- [ ] Per-phone credentials, expiring invitations and renewable short sessions.
- [ ] Production Secure Boot/NVS encryption and controlled recovery validation.
