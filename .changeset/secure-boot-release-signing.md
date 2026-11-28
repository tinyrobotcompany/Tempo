---
"tempo": minor
---

Published firmware is now built from a production profile with Secure Boot v2 (RSA-3072) and flash encryption in Development mode, and is Secure Boot signed before the OTA manifest is signed. The release fails if the enrollment URL, the HMAC identity key slot, Secure Boot, flash encryption or the anti-rollback secure version is missing. A local command builds the signed set for the one-time USB flash during lockdown.

Compatibility: releases now target layout `ab-8mb-secure-v1` (partition table at 0x10000, new encrypted `nvs_keys` partition, app slots unchanged). Unlocked `ab-8mb-https-v1` boards reject these packages and need the USB lockdown; NVS contents must be provisioned again afterwards. Publication needs the `SECURE_BOOT_SIGNING_KEY` secret and `TEMPO_IDENTITY_HMAC_KEY_ID` variable in `production-ota`.

Validation: host tests with disposable keys cover the configuration gate, the espsecure sign/verify round trip, tampered and wrong-key rejection, the layout identifier and workflow ordering. Not yet validated on a locked board.
