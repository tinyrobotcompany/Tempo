---
"tempo": minor
---

Remove the compiled PRIM transport key. Firmware now reads the key from a dedicated NVS namespace, so it survives firmware updates and never appears in build artifacts. Firmware builds no longer read `.env`.

Migration: after installing this firmware, provision the key once over the paired local setup with `scripts/provision-transport-key.py` (or the `/api/credential` endpoint). Until then, transport shows the missing-key state. Provisioning uses local HTTP protected by the pairing token, so do it on a trusted LAN.

Validation: host tests cover credential validation, persistence, erase and failed-write preservation, authenticated-only `/api/credential` (401 without or with a wrong token), and provisioning-script URL/key validation. No hardware qualification is claimed.
