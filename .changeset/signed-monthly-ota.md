---
"tempo": minor
---

Add signed production OTA updates with 30-day checks, optional hourly testing, immediate manual checks, maintenance-window installation, progress, postponement and rollback blocking. Publish validated GitHub firmware releases and release notes after firmware-related main merges.

Remove compiled PRIM credentials. Provision the transport key once through paired local setup or the private provisioning script; keep it in device NVS across updates. Existing setup uses local HTTP and requires a trusted LAN.

Initial USB bootstrap, credential provisioning and physical power-loss/touch/display qualification are required before enabling OTA on a device. No hardware qualification is claimed by automated tests.
