# Tempo

A standalone 10.1-inch ESP32-P4 touchscreen for a desk or shelf with clock, location-based weather and nearby Île-de-France public transport. Controlled locally from an iPhone browser and packaged in a 3D-printed enclosure.

## Project documents

- [Product requirements](docs/PRD.md) — agreed scope, acceptance criteria and mandatory OTA recovery.
- [Hardware and software design](docs/SYSTEM_DESIGN.md) — proposed architecture, integrations, enclosure concept and engineering sequence.
- [Electronics and assembly BOM](docs/BOM.md) — quantities, manufacturer links and display procurement checks.
- [Development and first hardware run](docs/DEVELOPMENT.md) — firmware status, build commands and verification.
- [Sources](docs/SOURCES.md) — primary references and validation limits.

Selected platform: Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1 kit, with exact supplier revision to confirm. Embedded firmware runs the display and serves the iPhone control panel; no Raspberry Pi is required. This supersedes the earlier 14-inch/Pi proposal.

Current firmware: **0.8.16**. Features include ten clock styles, equal weather/transport summary cards, location-based weather, nearby departures, service alerts, route diagrams, street maps and local iPhone controls. Wi-Fi profiles, screen brightness, colours and automatic view rotation are configurable. OTA delivery remains outstanding; the USB watcher is not OTA. See [development status](docs/DEVELOPMENT.md).

## Configure and build

Install ESP-IDF 6.1 for the tested ESP32-P4 revision, then copy `.env.example` to `.env` and set your own `IDFM_PRIM_API_KEY`. The build generates a private credential header; never publish build artifacts containing the key. Dependencies are restored from the component manifest and lock file. The patched LVGL adapter under `firmware/vendor` is required.

```sh
cp .env.example .env
# Edit .env with your PRIM API key.
./scripts/idf.sh build
```

Follow [development instructions](docs/DEVELOPMENT.md) for initial flashing, hardware configuration and host tests.

Start `python3 scripts/watch-device.py` once in a normal Terminal from this folder to build and app-flash changes automatically. Close other serial monitors first. The watcher never flashes a failed build and refuses partition layout changes. This is USB development automation; it is not OTA. See [development instructions](docs/DEVELOPMENT.md).
