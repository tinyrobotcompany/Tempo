---
"tempo": patch
---

Add Azure Monitor log alerts for the certificate service: worker cycle failures, three or more failed device jobs in an hour, invalid jobs, nonce cleanup failures and backlogs, certificates within 14 days of expiry, and Let's Encrypt rate limiting. Alerts email one recipient taken from the new required `ALERT_EMAIL` repository secret; infrastructure preview and apply fail until it exists. The worker now logs `certificate_expiring_soon` (count only, hourly), `certificate_expiry_check_failed` and `ca_rate_limited`; these take effect after the next image build and reviewed `imageDigest` change. No device, firmware or API contract change. Validated with service tests, compiled-template alert checks and the full host suite.
