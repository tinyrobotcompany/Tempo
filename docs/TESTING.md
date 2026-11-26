# Tests, coverage and regression policy

Run `bash scripts/test-all.sh`. The installed pre-commit hook runs this against the staged snapshot on every commit; CI runs the same command. Tests use deterministic fixtures and controlled platform substitutes, with no production credentials, external API calls or attached device required. The complete runner currently requires macOS/Xcode, Node 22+, Python, CMake and actionlint.

## Layers

| Layer | Coverage and purpose |
| --- | --- |
| Core unit tests | Settings encoding/validation, calendar/timezone handling, weather parsing, transport parsing/filtering/ranking, map projection and phone protocol. `test_core`, `test_weather`, `test_transport`, `test_transport_json_filter`, `test_phone_protocol`. |
| Host integration tests | Real network/time and weather/transport worker code against controlled ESP-IDF/HTTP seams; real settings persistence and asynchronous worker against in-memory NVS; real phone HTTP handlers and embedded assets; LVGL UI and map PNG decoding/cache; browser editor, map interaction and phone connection/controller flows. |
| Regression tests | Prior UI startup/render races, automatic view rotation, clock/date spacing, Wi-Fi/password/QR behaviour, lost replies without duplicate writes, location changes during requests, stale reverse-lookup suppression, transport discovery/departures, retries/timeouts, response caps, map cache leases, watcher/partition safeguards and HTTPS native location. These run with the unit/integration suite, not as an optional subset. |
| Tooling tests | Hook staged-index behaviour, release/version/note validation, coverage gate failure cases, diagnostic summary privacy, PR review error handling, security-result checks and changeset validation. |
| Hardware acceptance | Actual panel/DMA/touch, Wi-Fi coprocessor compatibility, power loss, wired flashing, long-run memory/reset stability and provider behaviour. Host substitutes cannot prove these. |

## Enforced 80% floor

`bash scripts/test-coverage.sh` instruments host C targets using LLVM and browser modules using Node's built-in coverage. It clears old raw profiles before measuring so stale results cannot pass a gate.

- Core unit-only profile: runs only the five core binaries listed above, independently of integration-test profiles. Each included core implementation file must have **at least 80% executable-line coverage**.
- Firmware unit + integration profile: **each included first-party C implementation file** must have at least 80%. New C/INC files in the application/components tree fail if missing instrumentation.
- Browser profile: at least 80% overall and per shipped JavaScript module. Missing modules fail the inventory check; the byte-identical shared location module is tested once and its duplicate equality is verified.
- Branch/function metrics are reported by Node but are not represented as line coverage. Reaching 80% does not prove every branch, timing condition or hardware behaviour is correct.

Explicit C exclusions: `main.c` is ESP-IDF/BSP startup, physical I/O and reset supervision; generated fonts, city data and timezone table; third-party ESP-IDF/LVGL/cJSON; test code. The generated timezone implementation still runs in tests but does not inflate the percentage. Host tests compile conditional host paths, not all ESP_PLATFORM-only paths. Build/generator/CI tooling is tested separately and is not included in these application coverage percentages. Do not advertise this as 80% coverage of all repository files or all hardware paths.

Reports:

- `build-host/coverage/unit.json`: independent core unit-only results.
- `build-host/coverage/firmware.json`: per-file combined firmware results and exclusions.
- `build-host/coverage/html/index.html`: annotated application source coverage.
- `build-host/coverage/browser.txt`: browser line/branch/function results.
- `build-host/test-results/latest.log`: complete latest test run.

The CI host job retains these reports/logs as `host-validation-<commit>` for 14 days, including on failure. Local hook snapshots are temporary; run the full suite in the working tree when persistent local reports are needed. No live device log or `.env` is uploaded by CI.

## Adding regressions

For every functional bug, add a test that reproduces the failure and asserts the user-visible/state result before fixing it. Prefer deterministic fake clocks and response fixtures over live Internet calls or timing sleeps. Preserve existing failure cases; do not lower the floor or exclude application code to make a patch pass. New device-specific defects also require a hardware reproduction/verification record.

Before publishing firmware, complete representative on-device boot, touch, Wi-Fi reconnect, phone controls, weather/transport refresh, location changes, automatic rotation and soak tests. Record firmware version, test conditions and observed outcome. OTA power-cut and rollback acceptance remains outstanding until OTA is implemented.

Power-management tests cover policy, settings migration, phone contracts and real LVGL wake/offline behaviour. Hardware qualification is listed in [POWER_MANAGEMENT.md](POWER_MANAGEMENT.md), including no-battery power cuts and real backlight/touch wake.
