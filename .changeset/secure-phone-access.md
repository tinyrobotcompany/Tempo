---
tempo: minor
---
Require a unique provisioned HTTPS identity for phone control, validate certificates and keys before provisioning, and add device-side revocation of all paired phones. Use native browser location on HTTPS. This requires a one-time USB migration to the HTTPS identity partition and a trusted certificate; old-layout devices reject these OTA packages. Public DNS and certificate renewal deployment remain required before production rollout.
