---
"tempo": patch
---

Add a repeatable release preparation command that updates the firmware version, consumes validated change notes, and maintains CHANGELOG.md and versioned release notes. Tag releases require matching notes and passing CI/security checks before a source-only GitHub draft is created. OTA distribution remains a separate future implementation.

Validation: automated release preparation/error-path tests and workflow lint. No device firmware changes or hardware validation are implied.
