---
"tempo": patch
---

Add Azure Monitor log alerts for the certificate service: worker cycle failures, three or more failed device jobs in an hour, invalid jobs, nonce cleanup failures and backlogs, certificates within 14 days of expiry, and Let's Encrypt rate limiting. Alerts match documented snake_case log-event names in Container Apps console logs or Application Insights traces, so they do not depend on the backend's language or host. They email one recipient from the new required `ALERT_EMAIL` repository secret; infrastructure preview and apply fail until it exists. The expiry and rate-limit events will be emitted by the replacement backend. No device, firmware or API change. Validated with compiled-template alert checks and the full host suite.
