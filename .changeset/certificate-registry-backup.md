---
"tempo": patch
---

Back up the certificate service's DeviceRegistry daily to a separate private, geo-redundant storage account. Backups are write-once for 90 days and kept for 365. A manual restore job runs as a dry run by default and only adds missing rows. The export and restore jobs each have their own least-privilege identity. Alerts fire on a failed export and when no export completes within 24 hours. Operators must re-run `bootstrap.bicep` before approving the deployment, because it adds two delegable blob roles. The jobs work after the next image build and reviewed `imageDigest` change. No device, firmware or API change. Validated with service unit tests, compiled-template backup and alert checks, and the full host suite.
