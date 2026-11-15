---
"tempo": patch
---

Fix clean-runner dependency restoration using verified hashes for the published and local-cache ZIP formats. Keep ESP32 transport paths linked in CI with an explicitly non-secret build fixture so memory-placement checks test the real allocation layout. Run firmware builds only when firmware build inputs change.

Ignore only three confirmed historical fake QR-token findings in secret scanning. Restrict authenticated review requests to the intended API origin and reject redirects. Preserve the trusted-base Codex review boundary; the initial infrastructure PR requires a separately reviewed bootstrap run before that workflow exists on main.

Validation: downloaded archives compared byte-for-byte after prefix removal; dependency/tamper, firmware-filter, aggregate-gate and review-destination regression tests; full host suite and full-history secret scan.
