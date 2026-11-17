---
"tempo": minor
---

Add signed production OTA updates with 30-day checks, optional hourly testing, immediate manual checks, maintenance-window installation, progress, postponement and rollback blocking.

Add a phone-page form to save the PRIM transport key (stored in NVS since the persistent-credentials change) and show whether one is configured. Setup uses local HTTP and requires a trusted LAN.

Initial USB bootstrap, credential provisioning and physical power-loss/touch/display qualification are required before enabling OTA on a device. No hardware qualification is claimed by automated tests.

On the display, update action feedback ("Request queued…", "Update service busy") now stays visible for 5 seconds instead of being overwritten by the next status refresh.
