---
"tempo": patch
---

Add factory tooling and a step-by-step runbook (docs/FACTORY_RUNBOOK.md) for locking down an ESP32-P4 board: a verified whole-flash backup and guarded restore (`scripts/factory-backup.sh`), a lockdown script that burns Secure Boot v2 and Development-mode flash encryption eFuses plus the identity HMAC key in Espressif's order (`scripts/factory-lockdown.py`), and an approval-gated, manually started workflow that registers one device's PUBLIC identity key (`scripts/factory-register.sh`). Everything is a dry run by default. Each device step needs its own typed confirmation, and keys are passed to espefuse through a pipe so they never touch disk. The firmware build itself is unchanged and no device has been touched. The lockdown script requires a signed Development-mode build with `CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=5`. Validated with mocked tools and a full rehearsal against espefuse virtual ESP32-P4 v3.0 eFuses.
