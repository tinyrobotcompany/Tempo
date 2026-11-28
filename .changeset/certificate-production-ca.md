---
"tempo": patch
---

Switch the certificate service to production Let's Encrypt (`acmeEnvironment = 'production'`), so devices that trust only ISRG Root X1/X2 accept their certificates. After deployment the CA account bootstrap workflow must be re-run once to register the production account; until then issuance fails closed. The documented steps cover the terms URL check, ordering, verification and rollback. A Node test pins the production default and checks that both the worker and the CA bootstrap job receive it. No firmware or API change.
