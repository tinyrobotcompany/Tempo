---
"tempo": patch
---

Enforce an 80% executable-line coverage floor for core unit tests, host-tested firmware implementations and shipped browser modules. Add settings persistence/failure tests, browser map/editor and secure-helper integration tests, stale-location regression coverage, and tests for coverage gates themselves. Preserve CI logs and coverage reports for 14 days.

Weather diagnostics now distinguish transport, HTTP, parse and overflow failures and include elapsed time and location generation without logging coordinates or credentials. Add a privacy-preserving serial-log summary command and troubleshooting guide.

Validation: full host suite, coverage gates, workflow lint and ESP32-P4 build. Hardware-only startup, DMA, power-loss and soak tests remain separate; no device runtime validation is claimed by host coverage.
