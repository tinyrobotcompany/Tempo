# Tempo

[![CI](https://github.com/tinyrobotcompany/Tempo/actions/workflows/ci.yaml/badge.svg?branch=main)](https://github.com/tinyrobotcompany/Tempo/actions/workflows/ci.yaml)
[![Security audit](https://github.com/tinyrobotcompany/Tempo/actions/workflows/security-audit.yaml/badge.svg?branch=main)](https://github.com/tinyrobotcompany/Tempo/actions/workflows/security-audit.yaml)
[![PR checks](https://github.com/tinyrobotcompany/Tempo/actions/workflows/pr-checks.yaml/badge.svg)](https://github.com/tinyrobotcompany/Tempo/actions/workflows/pr-checks.yaml)
[![Codex review](https://github.com/tinyrobotcompany/Tempo/actions/workflows/codex-pr-review.yaml/badge.svg)](https://github.com/tinyrobotcompany/Tempo/actions/workflows/codex-pr-review.yaml)
[![Required checks](https://github.com/tinyrobotcompany/Tempo/actions/workflows/ensure-codex-ruleset.yaml/badge.svg?branch=main)](https://github.com/tinyrobotcompany/Tempo/actions/workflows/ensure-codex-ruleset.yaml)
[![Draft release](https://github.com/tinyrobotcompany/Tempo/actions/workflows/release.yml/badge.svg)](https://github.com/tinyrobotcompany/Tempo/actions/workflows/release.yml)

[![Firmware version](https://img.shields.io/badge/firmware-0.8.16-blue)](CHANGELOG.md)
[![Hardware](https://img.shields.io/badge/hardware-ESP32--P4-blue)](docs/BOM.md)
[![Display](https://img.shields.io/badge/display-10.1%22%20touchscreen-blue)](docs/BOM.md)
[![Coverage gate](https://img.shields.io/badge/coverage%20floor-80%25-blue)](docs/TESTING.md)
[![OTA](https://img.shields.io/badge/OTA-not%20implemented-orange)](docs/RELEASING.md)

Workflow badges report GitHub results once these workflows are published and run; they do not assert that checks are already active. The firmware badge reports the source version, not a published release.

A standalone 10.1-inch ESP32-P4 touchscreen for a desk or shelf with clock, location-based weather and nearby Île-de-France public transport. Controlled locally from an iPhone browser and packaged in a 3D-printed enclosure.

External mains power only: use a 5 V USB-C adapter. [Power supply purchase link, display sleep and recovery behaviour](docs/POWER_MANAGEMENT.md). No internal battery or UPS.

## Project documents

- [Product requirements](docs/PRD.md) — agreed scope, acceptance criteria and mandatory OTA recovery.
- [Hardware and software design](docs/SYSTEM_DESIGN.md) — proposed architecture, integrations, enclosure concept and engineering sequence.
- [Electronics and assembly BOM](docs/BOM.md) — quantities, manufacturer links and display procurement checks.
- [Development and first hardware run](docs/DEVELOPMENT.md) — firmware status, build commands and verification.
- [Sources](docs/SOURCES.md) — primary references and validation limits.
- [Printed case CAD](mechanical/README.md) — parametric build123d case with colours set in `mechanical/colourway.toml`; [concept mock-ups](docs/design/enclosure/enclosure-mockups.html).

Current platform: the already-owned Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1 kit, with 32 MB flash and PSRAM. Embedded firmware runs the display and serves the iPhone control panel; no Raspberry Pi is required. This supersedes the earlier 14-inch/Pi proposal.

Current firmware: **0.8.16**. Features include ten clock styles, equal weather/transport summary cards, location-based weather, nearby departures, service alerts, route diagrams, street maps and local iPhone controls. Wi-Fi profiles, screen brightness, colours and automatic view rotation are configurable. OTA delivery remains outstanding; the USB watcher is not OTA. See [development status](docs/DEVELOPMENT.md).

## Configure and build

Install ESP-IDF 6.1 for the tested ESP32-P4 revision, then provision the PRIM key through paired phone setup after installation; firmware builds no longer read `.env`. Dependencies are restored from the component manifest and lock file. The patched LVGL adapter under `firmware/vendor` is required.

```sh
cp .env.example .env
# Edit .env with your PRIM API key.
./scripts/idf.sh build
```

Follow [development instructions](docs/DEVELOPMENT.md) for initial flashing, hardware configuration and host tests.

Start `python3 scripts/watch-device.py` once in a normal Terminal from this folder to build and app-flash changes automatically. Close other serial monitors first. The watcher never flashes a failed build and refuses partition layout changes. This is USB development automation; it is not OTA. See [development instructions](docs/DEVELOPMENT.md).

## Contribution checks

Install the mandatory local commit hook with `bash scripts/setup-hooks.sh` in each clone. Run the full suite with `bash scripts/test-all.sh`. See [repository guardrails](docs/REPOSITORY_GUARDRAILS.md) for prerequisites, PR checks, Codex review secrets, required-check activation and draft releases.

## Releases

See the [release strategy](docs/RELEASING.md) and [changelog](CHANGELOG.md). Prepare versions and release notes with `python3 scripts/release.py prepare`; validated tags produce source-only GitHub drafts. OTA distribution is not yet implemented.

## Testing and troubleshooting

The full suite includes unit, host integration and regression tests, with an enforced 80% executable-line coverage floor for core units, host-tested firmware and browser modules. See [testing scope and exclusions](docs/TESTING.md) and [troubleshooting/log capture](docs/TROUBLESHOOTING.md). Run `bash scripts/test-all.sh`; reports are written under `build-host/coverage/` and `build-host/test-results/`.
