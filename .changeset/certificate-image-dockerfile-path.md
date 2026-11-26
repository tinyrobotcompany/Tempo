---
"tempo": patch
---

Fix the certificate service image build on main, which failed with "Unable to find 'Dockerfile'" because Azure CLI resolves `--file` from the repository root rather than the build context. The image build now passes the repository-relative Dockerfile path, and the mocked pipeline test asserts that path exists. No firmware, device or deployed-resource behaviour changes; the build still does not deploy.
