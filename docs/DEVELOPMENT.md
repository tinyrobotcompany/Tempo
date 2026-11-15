# Firmware development and first hardware run

12 November 2026. Development is local without Git, as requested.

## Current milestone

The first application targets the Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1 using its managed board support package 2.0.3 and LVGL 9.5.0. The user confirmed ESP32-P4 revision v3.2 and 32 MB flash using esptool on 12 November 2026. ESP-IDF 6.1 is installed on this Mac; vendor examples also exercise 5.5.5 and 6.0.2. The manifest's version range is not a claim that all toolchain versions have passed our builds.

Implemented source:

- Landscape clock with orange seven-segment digits, date and manual Paris time entry. Cold boot shows an unsynchronised clock until time is set; it never invents a date or weather observation.
- Touch navigation between clock, weather, transport and settings. Weather/transport are labelled placeholders.
- Brightness slider through the BSP, three clock colours and optional 20-second view rotation. Settings persist in versioned NVS; settings view pauses rotation.
- Two 8 MiB application slots, OTA metadata and rollback enabled. Initial boot-health logic checks UI progress before accepting a trial image.

Not implemented: Wi-Fi provisioning, automatic time sync, live weather/transport, phone pairing/web server, scheduled dimming, signed OTA download/release verification and C6 update coordination. The application is a hardware milestone, not the completed PRD. No secure-boot eFuses are changed. Do not deploy this as the production OTA release.

## Board identification

Run from the project folder in your Mac Terminal:

```sh
./scripts/device-info.sh /dev/cu.usbmodem5C940599581
```

The port may change after reconnecting; use `ls /dev/cu.*` to find it. This reads chip identity/revision and flash capacity and may reset the board. It does not write flash. Confirm ESP32-P4, revision and 32 MB flash, and confirm the product label says the 10.1-inch P4/C6 kit. Rev1.x and Rev3.x need different build configurations; do not force-flash a mismatched image.

## Build

The launcher uses an already-active ESP-IDF environment or the installed macOS ESP-IDF 6.1 activation metadata. The component manager downloads the pinned Waveshare/LVGL packages and their dependencies on first configuration; keep its generated `dependencies.lock` once resolution succeeds.

For confirmed Rev3.x hardware:

```sh
./scripts/idf.sh -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.rev3' build
```

If the chip is older, select the manufacturer-supported silicon profile before building. Do not reuse a generated `sdkconfig` from a different revision. No connected hardware is needed to compile. Firmware output belongs in `firmware/build/`.

After a successful build and confirmed revision, preserve the factory firmware before the first flash. For confirmed 32 MB flash, use the active ESP-IDF environment's esptool:

```sh
mkdir -p backups
python -m esptool --port /dev/cu.usbmodem5C940599581 read-flash 0 0x2000000 backups/factory-p4.bin
```

Keep backups private; they can include stored settings. This backs up P4 flash, not the separate C6. Once the backup is verified and the build is for the correct revision:

```sh
./scripts/idf.sh -p /dev/cu.usbmodem5C940599581 flash monitor
```

Flashing replaces the factory P4 application and partition table. It does not update C6 firmware. The first bring-up should check landscape orientation, touch in all four corners, settings save/reboot and backlight changes. Supply the kit's specified power; a data cable connection alone does not establish adequate power for the panel.

## Automated checks

```sh
./scripts/test.sh
./scripts/test-ui.sh
```

The first suite tests settings limits, rotation boundaries, date validation, leap years and Paris DST gaps. The second compiles/runs actual LVGL on the Mac with a headless display, exercising navigation, save/backlight callbacks and the UI heartbeat. It uses the cached LVGL 9.5.0 archive, or `LVGL_ARCHIVE` can point at another local copy of that exact version. These checks cannot validate physical touch coordinates, backlight electronics, NVS power-loss behaviour or OTA recovery.

## Verification on 12 November 2026

- Core tests passed with compiler warnings treated as errors.
- UI compiled against LVGL 9.5.0; headless navigation/settings/heartbeat tests passed.
- Espressif partition-table generator accepted the custom OTA layout.
- Full target build attempted with ESP-IDF 6.1; stopped during dependency resolution because the session shell could not reach `components-file.espressif.com`. No target firmware binary has been produced.
- USB port `/dev/cu.usbmodem5C940599581` exists, but the session sandbox returned `Operation not permitted` when opening it. Chip identity could not be read, and nothing was flashed.

## Code layout

`firmware/components/station_core` contains portable settings, calendar and view logic. `station_ui` contains LVGL rendering and touch events, with callbacks for hardware/storage. `firmware/main` connects the vendor BSP, NVS and trial-boot health checks. `scripts` provides build/device/test entry points. `tests` contains host-side checks. Hardware pin assignments are delegated to the vendor BSP rather than guessed.

Before network features are released, extend health checks for the local server and C6 communication, validate signed-image rejection, and perform physical power-cut rollback tests. Provider outages must not cause a healthy firmware image to roll back.

## Landscape touch correction

The first hardware run displayed the UI but navigation taps did not activate buttons. The boot log confirmed display/touch discovery. Tracing the installed BSP and LVGL adapter showed that ROTATE_90 rotates display output while the single-pointer input path forwards native touch coordinates unchanged. The original application supplied no input transform.

The application now installs a custom touch reader under the LVGL lock. It reads the existing GT911 driver and maps native coordinates to landscape with x = raw_y and y = 799 - raw_x, the inverse of the adapter's display rotation. Out-of-range points are discarded; BSP swap/mirror flags stay disabled to avoid applying the transform twice. Touch-down logs report raw and mapped positions.

The regression test now feeds physical-coordinate press/release events into an LVGL pointer and verifies that all four navigation buttons change view. This replaces direct-event-only navigation coverage. Corner/bounds mapping tests and the host UI suite pass. The complete ESP-IDF target build now succeeds using the downloaded dependencies. No managed dependency files were modified. The mapped build still requires a physical touch check after flashing.

Exit an existing serial monitor with Ctrl+] before running:

```sh
./scripts/idf.sh -p /dev/cu.usbmodem5C940599581 flash monitor
```

Tap Settings, then each navigation button. A tap near the Settings button centre should log landscape coordinates around (1105,739). If taps still fail, collect the new `Touch raw=... landscape=...` lines (or report that none appear). This session cannot directly observe hardware touch events.

## Time-entry keyboard correction

The keyboard inherited LVGL's BOTTOM_MID alignment, so the application's positive y offset placed it outside the clipped settings container. Explicit TOP_LEFT alignment restores the intended position (40,440). A regression test failed on the original layout, then passed after the fix; it checks containment and sends a numeric key event through the keyboard to the linked time field. The full target build also passes. Verify the physical keyboard after flashing.

Time currently advances through ESP-IDF system timers after manual entry. Automatic SNTP synchronisation remains unimplemented. The planned Wi-Fi time service will synchronise at startup and periodically, retain a running clock through network outages while powered, and apply Paris DST rules. Complete power loss requires fresh synchronisation or manual entry in this battery-free design. Saving a timestamp to flash does not measure time elapsed while unpowered. See [Espressif system time](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/system_time.html).


## Firmware 0.2.0 interaction and network fixes (12 November 2026)

This section supersedes earlier statements that Wi-Fi/time are absent or that the latest complete target build is validated. The previous hardware milestone linked successfully; the 0.2.0 source currently has passing host tests and compiler checks for all six application translation units, but its full link and hardware execution are still pending. ESP-IDF component-manager process inspection is denied in this session. A trial offline build could not resolve managed dependency aliases and is not a release artifact.

- Colour, brightness and rotation saves enqueue without waiting for NVS. A separate worker coalesces rapid changes and reports completion/failure. The UI updates the colour immediately. Save-duration logs and the existing UI heartbeat watchdog support hardware diagnosis. The multi-minute freeze is not yet confirmed resolved on hardware.
- Manual-time keyboard starts hidden, opens when its text field is tapped, closes with keyboard Done/Cancel or an accepted Set time, and disappears when changing views. Set time validates exact `YYYY-MM-DD HH:MM`, rejects invalid calendar times/DST gaps, queues the clock update and displays success only after the worker completes it. Manual setting disables automatic sync.
- Settings → Wi-Fi / time accepts a 2.4 GHz SSID and password, saves them on the device and connects via the existing C6 firmware/SDIO link. Credentials are not logged; touch coordinates are debug-only. Credentials currently reside in ordinary NVS, without encryption.
- Automatic time uses SNTP at connection and hourly. The clock keeps running while powered and offline. After power loss there is no battery-backed time: automatic mode waits for Wi-Fi/SNTP; manual mode requires setting the time again. SNTP sets UTC; local DST conversion is independent.
- Settings → Home location accepts latitude,longitude. An HTTPS Open-Meteo lookup with `timezone=auto` returns the IANA zone; firmware uses bundled POSIX rules for DST and caches the zone for offline use. UTC is displayed until a home zone is resolved. The lookup needs a valid clock for TLS; automatic time should therefore be enabled during initial setup. This is location-derived timezone selection, not a GPS sensor or automatic browser geolocation. iPhone geolocation remains part of the later browser control panel.
- `scripts/generate_timezones.py` regenerates the checked-in zone rules from the host's zoneinfo data. Regenerate with updated tzdata when civil-time rules change; these rules describe current/future civil time, not historical conversion.
- OTA delivery is still outstanding. Dual application slots, OTA metadata and trial-boot rollback checks remain in place. Add network/OTA health checks before enabling OTA delivery; do not describe USB automation as OTA.

### Automatic USB development updates

The user has already flashed the original partition layout. Close the serial monitor (`Ctrl+]` in IDF monitor), then run once from the project directory:

```sh
python3 scripts/watch-device.py --port /dev/cu.usbmodem5C940599581
```

Keep this Terminal running while development continues. It builds immediately and after stable source changes, and app-flashes only after a successful build. Enter retries a failed build/flash; Ctrl+C stops. Logs appear in the Terminal. The first run records the existing build's partition binary in `build-host/watch-partition-table.bin`, assuming it is the layout already flashed to this device. Future partition differences are refused even after retry; deliberate partition migrations require a separate full-flash procedure. NVS is not erased by app-flash. OTA and the iPhone panel are not yet available.

Verification commands:

```sh
./scripts/test.sh
./scripts/test-ui.sh
python3 tests/test_watcher.py
```

Hardware acceptance still required: repeated colour changes followed immediately by navigation; save persistence after restart; keyboard show/dismiss/reopen; manual-time success and clock display; Wi-Fi connection/reconnection; cold-start SNTP; home-zone lookup and cached-zone restart; brightness/rotation. If the device freezes, collect the `settings: Save completed ... ms` output and any `UI stopped responding`/reset logs via `./scripts/idf.sh -p /dev/cu.usbmodem5C940599581 monitor` after stopping the watcher.


## Firmware 0.2.1: display freeze, time setting and Wi-Fi discovery

The user confirmed that 0.2.0 built/flashed successfully, then reported a reboot when setting the time and approximately one-minute freezes on colour changes. The earlier assumption that asynchronous NVS persistence alone would fix the freeze was incorrect.

Evidence: the installed `espressif/esp_lvgl_adapter` README explicitly documents an ESP32-P4 PPA hang with 90°/270° rotation plus `TRIPLE_PARTIAL`. Our main.c selected exactly that combination. The installed IDF 6.1 `esp_driver_ppa/src/ppa_srm.c` retains conditional `bypass_mb_order`; it does not contain the adapter patch's unconditional `ppa_ll_srm_bypass_mb_order(platform->hal.dev, true)`. The adapter also supports DOUBLE_FULL with rotation, selecting its full-frame rotation function and three physical frame buffers. We now select that supported mode, removing the partial-region repair/rotation path without modifying the SDK or managed dependencies. This is a strong matching failure mechanism; a reboot backtrace is still required to establish conclusively whether the Set time reboot came from that hang and our existing UI watchdog.

- Colour buttons use explicit pressed styles without animated transitions. A white outline, named selection and coloured clock preview update immediately, before the nonblocking settings-save request. Saving/saved/failure remains visible separately.
- Clock requests now run in their own task/queue, independent of hosted Wi-Fi initialisation, scanning, retries and HTTP timezone lookup. The Set time button disables while pending; confirmation appears only after completion. The worker logs operation duration/error, and startup logs the reset reason and selected refresh mode. The UI watchdog remains enabled.
- Wi-Fi setup now scans on opening. The selector shows nearby SSIDs, RSSI and whether security is enabled. Duplicates collapse to the strongest AP, up to 20 networks; hidden empty SSIDs are omitted. The UI uses the exact raw SSID for connection and sanitises control characters only in displayed rows. Password entry is masked and clears after an accepted connection request. Rescan, no-results, scan failure and a 20-second scan timeout have explicit UI states. Driver operations remain outside LVGL callbacks.
- Scan timeout begins after `esp_wifi_scan_start` succeeds; initial C6 transport setup is governed by ESP-Hosted's own limits. Initial station connection/reconnect attempts are paused for scans; reconnect resumes afterward.
- Runtime health and OTA rollback behaviour are preserved. OTA delivery is still outstanding.

Validation: `scripts/test.sh`, `scripts/test-ui.sh`, and `scripts/test-network.sh`. Network tests execute the actual firmware worker functions against deterministic RTOS/driver seams: a time request completes without executing a queued Wi-Fi request; driver failure is reported; scans are nonblocking; results are deduplicated; missing scan completion times out; empty results work. UI tests exercise pointer taps, manual-time validation/completion, hidden keyboard behaviour, immediate teal/blue preview and selection outline, navigation with pending saves, detected-network selection and scan failure/no-results. These do not emulate PPA/DSI hardware.

Hardware acceptance: verify the boot log says version 0.2.1 and `Display refresh: DOUBLE_FULL, rotation=90`; repeatedly select Orange/Teal/Blue and verify immediate preview/outline and responsive navigation; set a valid time and verify it on Clock; open Wi-Fi, select a detected home network and connect; test Rescan and navigation during scanning. Record any panic/backtrace or `UI stopped responding` message if a reboot recurs. The physical fix remains unverified until these checks run.

Primary driver reference: https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html (PPA Freeze Fix section). The installed component README/patch and SDK source were also inspected directly.


## Firmware 0.2.2: setup and appearance

Supersedes manual-time UI instructions above. The user confirmed 0.2.1 flashed and reported poor Wi-Fi feedback/layout and intermittent solid-blue output. This firmware has no update-screen path or OTA downloader. Serial access was attempted again and denied with `Operation not permitted`; no hardware blue-screen log was available. Source investigation found the backlight was enabled before the first application refresh. The backlight now stays off until that refresh completes. This addresses a startup exposure window, **not a proven cause of the intermittent runtime blue screen**. Every 30 seconds, logs show UI heartbeat, submitted-frame count and uptime; submitted frames are not proof that the physical panel scanned them correctly. Reset-reason and watchdog logs remain enabled. DOUBLE_FULL rotation remains unchanged.

New setup UI separates Wi-Fi, home city, profile and appearance. Connection requests set shared connecting state before queueing, visibly disable duplicate requests, animate a spinner and show success or a 45-second worker deadline. Slow hosted-driver initialisation can exceed that deadline before returning; the UI remains navigable and does not promise a fixed duration. Network tests exercise queue acceptance, duplicate rejection, IP acquisition and timeout. The connection indicator means DHCP/IP success, not proven internet availability.

The standard bundled IANA zone.tab list supplies representative time-zone cities, coordinates and the matching existing POSIX timezone rule. Selection saves coordinates and zone together without needing online geocoding, and reports completion only after persistence. The list is searchable but not a database of every municipality or street. City centres are not sufficiently precise for reliable nearby transit discovery. The board has no GPS hardware; phone-assisted location remains pending with the phone control panel. Regenerate the city list explicitly with `python3 scripts/generate_cities.py` after tzdata updates; normal builds use the committed data. City data is public domain.

Settings schema 2 adds RGB colour, clock style and a UTF-8 name of up to 48 bytes. The new NVS `settings_v2` record is preferred; absent records migrate from six-byte schema 1. Saves also write a compatible legacy `settings` record for old-image rollback (custom colours fall back to Orange there). New profile/style fields are not shown by older firmware. Core tests cover old-format migration, new-format roundtrip, invalid unterminated names, and all city coordinates/time-zone rule references.

Digital and Slim render active bars only. Modern uses a native 240px Montserrat glyph subset generated offline by `scripts/generate_clock_font.c`; its SIL OFL licence is included beside the generated font. UI tests render real LVGL frames to `build-host/*-022.ppm` for visual review, exercise presets/custom sliders, profile persistence callbacks, city search, hidden keyboard, connection spinner/success/timeout, failed/empty scans, font selection and connection icons on all main pages. Host AddressSanitizer covers application UI/core memory accesses; the prebuilt LVGL host library is not instrumented.

Validation commands:

```sh
./scripts/test.sh
./scripts/test-network.sh
./scripts/test-ui.sh
```

Full ESP-IDF build was attempted and blocked by component-manager `psutil` process enumeration under the current sandbox. All seven application translation units, including the native clock font, compile with the installed ESP32-P4 toolchain. These checks do not replace a full link or physical display test.

Build, flash and retain the serial log in a normal terminal:

```sh
./scripts/idf.sh -p /dev/cu.usbmodem5C940599581 build app-flash monitor
```

Physical acceptance still required: version 0.2.2, first frame/backlight startup, connection spinner and failure recovery with a deliberately wrong password, successful Wi-Fi and automatic time after migration from manual mode, search/select city and persistence after reboot, profile/custom colour/font persistence, keyboard dismissal/navigation, repeated view changes, and observation/log capture of any solid-blue recurrence. Never label the blue-screen defect resolved solely on host tests. OTA delivery remains outstanding.


## Firmware 0.2.3: blank-display startup regression

After flashing 0.2.2 the user reported no display output. Source tracing found a task-notification contract violation introduced by the startup backlight change: main held the LVGL lock and called `lv_refr_now()` before restoring brightness. Rotated DOUBLE_FULL calls `display_bridge_pipeline_wait_free_buf()`, which waits on the calling task's notification. VSYNC routes the wakeup to the adapter task (`display_bridge_v9_get_notify_task` defaults to `ctx->task`). A manual refresh from main can therefore wait indefinitely with brightness zero, preventing both backlight restoration and entry into the UI health loop. The host UI's immediate flush callback cannot reproduce this hardware/RTOS path. This is an evidenced code defect matching the symptom; no device boot log was accessible to confirm the observed instance.

0.2.3 removes the forced main-task refresh and the explicit zero-brightness step. Main builds the UI under the lock, releases it to the adapter task, restores configured brightness unconditionally, logs `UI released to renderer; backlight restored to ...%`, and enters the unchanged health/rollback loop. The adapter now both waits for and receives VSYNC notifications in its own task. No changes to managed components, SDK, partitions or stored preferences are required.

`python3 tests/test_startup.py` guards against synchronous rendering from main and frame-completion gating of backlight restoration. All seven application translation units pass the ESP32-P4 compiler checks. Physical boot verification is still required; this repair must not be confused with resolution of the earlier intermittent solid-blue screen.


## Firmware 0.2.4: reproduced rotated-buffer completion race

Evidence: `build-host/device-runtime.log` from 0.2.3 contains three application watchdog resets after heartbeat/frame counts stop (first reset at 62.362 seconds, later resets at about 32 and 62 seconds). Software resets are explicit; no brownout/panic is shown. Wi-Fi connects and time synchronises before each stall. The C6 version mismatch remains a separate compatibility warning, not an established cause of these freezes.

Tracing rotated DOUBLE_FULL found a lost-completion race in esp_lvgl_adapter 0.6.4: `blit_full_frame` publishes the submission flag; `flush_full_rotate` subsequently enqueues the retired panel buffer. A frame ISR between those operations consumes the flag while the busy list is empty. The rendering task then enqueues and waits; future interrupts cannot release it because the submission flag was already consumed. That exact interleaving is reproduced deterministically using the original adapter function bodies, and still strands the buffer after ten subsequent frame events.

0.2.4 uses a local override of adapter 0.6.4. It checks the rotated pipeline's busy list under its existing lock before consuming the submission flag. An early callback defers to the next recurring DMA-frame event. The patched actual functions release the buffer exactly once under all four tested interrupt schedules. The exact patch and provenance are in `firmware/vendor/esp_lvgl_adapter/STATION_PATCH.md`. SDK and managed components are untouched. The watchdog remains enabled. The USB development watcher now includes vendored sources in its change fingerprint.

Validation: deterministic original-fails/patched-passes race test; startup guard; six watcher tests; core and network-worker tests; target compilation of all application translation units and the complete patched v9 adapter bridge. ESP-IDF explicitly reports that it selects the local override. Full linking could not complete here because manifest re-resolution needs access to the component registry, which this session cannot reach. The user's normal build must resolve the updated lockfile; do not manually fabricate lockfile hashes.

Run `python3 tests/test_display_race.py` to reproduce the evidence. Physical acceptance: flash 0.2.4, retain monitor output for at least 15 minutes (longer than the reported 2–5 minute recurrence), exercise clock/settings/colours and navigation, and confirm heartbeat and frame counts continue without `UI stopped responding`. This is a reproduced and corrected driver defect strongly matching the runtime stalls; final attribution and resolution require that device run. No claim that OTA or automatic geolocation is implemented changes with this fix.


## Firmware 0.3.0: weather and clock summary

Open-Meteo current/hourly/daily requests now run in a dedicated 8192-byte-stack, priority-3 weather task, independent of LVGL, clock, Wi-Fi scanning and location persistence. The network service publishes committed home coordinates, a display name and a location generation under its existing lock; loading a saved location at boot also publishes it. A location generation change invalidates weather immediately, and responses obtained during a location change are discarded. No GPS or phone location is implied: the current city selector uses representative city-centre coordinates.

Provider request: HTTPS certificate bundle, 12-second HTTP timeout, no redirects, bounded 24 KiB response, explicit Celsius/km/h/mm, UTC Unix timestamps, automatic provider timezone, 12 forecast hours and three days. The UI presents six upcoming hourly points. Current values: temperature, feels-like, relative humidity, weather code/day-night, wind direction/speed/gusts, sea-level pressure and preceding-interval precipitation. Daily values: high/low, weather code, maximum precipitation probability and precipitation amount. Precipitation probability includes rain/snow; labels deliberately say “precip”. Current precipitation includes its provider-supplied interval. This is model-derived outdoor weather, not an indoor sensor or local rain-gauge reading.

Only successful HTTP 200 + valid JSON responses replace the cache. Optional missing/null/out-of-range or wrong-unit measurements render as `--`; malformed/empty/error responses do not become zero readings. Refresh every ten minutes; failure retries at 60/120/240/480/600-second intervals, capped at ten minutes. Manual refresh is coalesced and rate-limited to one attempt per minute. Reconnection schedules another attempt, respecting the one-minute recent-attempt window. UI is usable throughout HTTP activity. Cache is in RAM; reboot requires a new fetch. Fetch time and model conditions time are separate. Data is stale after 30 minutes by wall-clock or monotonic cache age; model conditions older than 30 minutes are also stale. Offline/retry/stale states remain visible alongside retained values. Past hourly entries disappear, and the clock chooses the daily summary matching today's provider-local date instead of blindly using a cached yesterday row.

Clock: large time and date plus a tappable weather card (location/freshness, temperature/condition, feels-like/humidity, daily high/low/precipitation chance). The adjacent transport area is explicitly “Not set up yet”. Weather: current conditions, six metric cards, upcoming hours, three day cards, refresh action and Open-Meteo/model attribution with update and condition timestamps. Digital/Modern/Slim fonts, custom colours, profile, connectivity icon and the adapter race patch remain intact.

Validation performed:

- `scripts/test-weather.sh`: strict parser fixtures plus the real worker loop with deterministic HTTP/clock/network seams. Covers prerequisites, chunk assembly, HTTPS config, refresh limits, retry/backoff, HTTP 503, malformed JSON, oversized bodies, cached/offline/stale conditions, reconnection and in-flight location changes.
- Live HTTPS request for public Paris city-centre coordinates succeeded; the production parser accepted the real response, including six hourly and three daily entries. This verifies provider schema, not ESP TLS/Wi-Fi operation.
- Core/network tests, startup guard and the original-fails/patched-passes display race test pass. UI tests and application AddressSanitizer checks pass; the LVGL library remains uninstrumented.
- All nine application translation units compile with the ESP32-P4 toolchain. Full build attempted; CMake dependency processing selected the local display patch but failed on sandbox-denied `psutil` process enumeration. No SDK/managed-component edits were used to bypass this.
- Rendered `build-host/weather-ready-030.png`, `clock-weather-030.png` and `clock-weather-modern-030.png` were visually reviewed. These are synthetic test-data previews, not screenshots of the physical device or current weather measurements.

Build/flash/log in the normal terminal, after closing any existing monitor with Ctrl+]:

```sh
./scripts/idf.sh -p /dev/cu.usbmodem5C940599581 build app-flash monitor 2>&1 | tee build-host/device-runtime-030.log
```

On-device acceptance: boot version 0.3.0; connect Wi-Fi and choose a home city; confirm `weather: Fetch complete: HTTP 200, ... ESP_OK`; check current weather and forecasts on both pages. Change city and verify old data vanishes before the replacement appears. Disconnect Wi-Fi and verify cached/offline labels; reconnect and verify recovery. Exercise refresh, navigation, colours and all clock fonts during requests. Let the device run at least 20 minutes to cover scheduled refreshes and verify display heartbeat/frame counts keep advancing without watchdog resets. Transport, iPhone geolocation/control and OTA delivery remain separate next steps.


## Firmware 0.3.1: HTTP request-line transmit buffer

The device log showed successful Wi-Fi, SNTP and TLS certificate validation followed by `HTTP_CLIENT: Out of buffer` and `weather: Fetch complete: HTTP 0, 0 bytes, ESP_FAIL`. Inspection of installed IDF 6.1 esp_http_client.c:1779–1800 establishes that this exact message is emitted when the complete outgoing request line does not fit the TX buffer. esp_http_client.h defines the default as 512 bytes. Our combined current/hourly/daily query exceeds that limit, despite fitting our own 1024-byte URL array. The 24 KiB response-body accumulator is unrelated; no HTTP response was obtained.

Fix: explicitly set weather HTTP `buffer_size_tx` to `sizeof(url)+32` (1056 bytes), enough for the longest accepted URL plus GET/protocol/CRLF/terminator. Receive buffering, bounded body accumulation, certificate verification, redirects and retries are unchanged. The C6 version-mismatch warning is not the cause of this deterministic local request-construction error.

The prior mock HTTP client did not model the SDK's request-line limit, and a desktop curl success could not expose it. The real-worker seam now applies the 512-byte default unless overridden and fails requests whose complete first line exceeds capacity. With unpatched production configuration, the test reproduces the failed first fetch; with this correction all worker cases pass. A parser/URL regression assertion records that the actual production query exceeds 512 bytes. Weather/network tests and ESP32-P4 application compiler checks pass. Physical success still requires booting 0.3.1 and seeing HTTP 200 with a nonzero body followed by populated weather cards.


## Firmware 0.4.0: graphical weather layouts

The clock keeps its large time/date and adds condition icons, current temperature, daily low/high, feels-like/humidity and a six-hour temperature curve with precipitation probability bars. The weather page groups current conditions in one header, then shows a twelve-hour graph and three daily forecasts with comparable low/high range bars. Temperature follows the selected accent; precipitation stays blue with a fixed 0–100% scale. Both series share actual forecast timestamps. Missing measurements leave gaps; missing hours are not joined. The temperature scale includes padding and at least a six-degree span so near-flat forecasts are not exaggerated.

The request now asks for thirteen hourly records to supply twelve future points after excluding the current partial hour. The weather model holds twelve points; the clock selects six. Vector icons and LVGL draw callbacks require no image assets or new background tasks. Existing offline/stale states, location invalidation, HTTP buffer sizing and display race fix are retained.

Validation: weather parser/worker and graph-selection regressions, LVGL rendering/interaction tests, application AddressSanitizer checks, display-race/startup guards and all ten application translation units compiled with the ESP32-P4 compiler. Host previews use sample data. Full ESP-IDF build remains blocked by component-manager process inspection in this session; firmware 0.4.0 has not been flashed or validated on hardware here.


## Firmware 0.5.0: local phone panel and precise location

See [iPhone setup](IPHONE_SETUP.md) for protocol, deployment state and hardware acceptance. New `phone_setup.c` serves embedded assets using ESP HTTP Server; `phone_protocol.c` validates bounded commands. Phone commands reach LVGL through a queue, with persistence through existing workers. QR links track the actual station IP and use a per-boot random token. `station_net/place` persists the locality name; `zone_pending` retains the previous zone while a new coordinate lookup is pending, including after reboot. Existing city records remain readable. Network switching does not move the saved home coordinates.

The companion static HTTPS page is complete in source but not published/configured. Local address search and map confirmation do not require that helper. Live address-service responses, Safari positioning/navigation and hotspot reachability are unverified. The SDK full build is blocked by process-inspection permissions; no 0.5.0 device image is claimed. No Git operations were performed.

## Firmware 0.5.1: no-hosting location handoff

Supersedes the hosted helper described in 0.5.0. The embedded panel now uses `web/shortcut.js` to launch a generic Apple Shortcut and validate its text callback against a random request nonce and five-minute expiry. The user confirms the returned map point before any location save. Existing queued firmware operations are unchanged. `scripts/test-phone.sh` covers callback parsing, locale decimals, expiry, mismatched requests and real HTTP handlers.

The editable workflow is generated by `scripts/build-location-shortcut.py`. Only a validated signed `companion/shortcut/Weather Station Location.shortcut` is bundled. The CLI signing path failed, but native Shortcuts File → Export → Anyone succeeded. The complete signed export and canonical source are supplied with hashes; `python3 tests/test_shortcut_artifact.py` checks signature integrity and both coordinate outputs. Phone-server tests exercise the real installer download as well as the unavailable-installer branch. The development watcher now watches web assets and the installer too. The latitude-only export under build-host remains a diagnostic artifact, not a deliverable. See [customer setup and release gates](IPHONE_SETUP.md). No new image has been flashed or claimed tested on an iPhone.

Latest build attempt: ESP-IDF failed during component-manager `get_cmake_pid()` because `psutil.pids()`/`sysctl()` is denied by this execution environment. Logs: `firmware/build/log/idf_py_stderr_output_47788` and `idf_py_stdout_output_47788`. No flash attempted after that failed build.

### 0.5.1 launch URL correction

An iPhone screenshot reported a missing shortcut named `Weather+Station+Location`. The launch builder used form encoding (`URLSearchParams`), which emitted literal plus signs for spaces. Shortcuts treated those plus signs as name characters. The original test also used the form decoder and concealed the mismatch. Launch query values now use `encodeURIComponent`, producing `Weather%20Station%20Location`. A regression test failed against the original code with the exact screenshot name, then passed after the correction. It also verifies all three nested callback URLs retain their request state and pairing-token fragment. The signed installer is unchanged; only the embedded phone asset needs updating, followed by a page reload. Real-iPhone retest remains pending.

### Automatic phone reconnection

Added embedded `web/connection.js` and its HTTP route. Transport serializes fetches, deduplicates status reads, applies six-second request deadlines and retries read-only status checks with bounded backoff. App UI preserves unsaved drafts/map selection, disables device operations until a valid response, retries on foreground/online events, and distinguishes re-pairing from a temporary outage. Failed writes are not replayed. `scripts/test-phone.sh` includes deterministic transport recovery and actual app-script UI recovery tests. Production C target compilation checks pass; full device acceptance remains pending in the restricted build environment.

## Firmware 0.5.2: cross-tab iPhone callback fix

The photographed “Location request expired or belongs to another browser” message came from the client request check. Its `sessionStorage` request record is unavailable when Shortcuts returns in a different tab/browser. The device now issues and validates an authenticated random nonce through POST `/api/location-request` (`begin`/`validate`) with a ten-minute monotonic-clock expiry. Pairing is still required; callbacks are not trusted merely because they contain coordinates. Read-only validation is idempotent to survive a lost response. Explicit map confirmation remains required to persist location.

Removed the client-side pending-session requirement. A new-tab app-script test starts with empty session storage and verifies that parsing happens only after device validation, the map receives the coordinates, and no location-save command is sent. Server tests verify missing authentication, missing request, incorrect nonce, valid cross-tab validation, repeated validation without writes and expiry. Installation help is expanded by default. Signed shortcut is unchanged. Source and host/target checks pass; real iPhone validation remains pending.

## Firmware 0.5.3: location save feedback and accented display text

Confirm location immediately becomes “Saving location…” with a spinner and disabled state. A live status message immediately below the button reports progress, success or failure; the existing message above the map remains mirrored. No scrolling is necessary to see the result. Tests exercise the actual page handler with a pending network request and failure.

Embedded Montserrat defaults did not contain accented letters. Added generated 4-bit fallback glyphs for text sizes 18/24/32/48, retaining the original ASCII and symbol fonts. Generation scans Latin extensions, combining marks, Greek/Cyrillic and typography ranges for glyphs supported by the bundled Montserrat source. This is not complete worldwide Unicode coverage; unsupported scripts still need suitable font packs. Tests explicitly check French accents, œ/Œ, ß and curly apostrophes at all four sizes. Phone input is normalised to NFC before UTF-8 byte-limited truncation; protocol tests preserve accented locality names. Regenerate using `scripts/generate_text_fonts.c` and the LVGL Montserrat-Medium.ttf; SIL OFL remains in fonts/OFL.txt.

Phone and native UI tests pass, as do target compilation checks. Real-device rendering/save acceptance is pending. Transport live-data access still requires PRIM credentials; this update does not claim a completed transport integration.

## Transport API contract preflight

The initial traffic-only probe was authenticated successfully on the host Terminal, but returned 2,766,727 bytes. This is not evidence that nearby-stop discovery or SIRI departures are authorized. The next integration gate is `scripts/check-transport-services.py --latitude <latitude> --longitude <longitude>`. It queries up to eight nearby platforms, tests up to two mapped SIRI platforms, and checks a filtered line report with geographic shapes disabled. Requests are bounded to 4 MiB for desktop diagnostics only, disallow redirects and never print credentials or raw response bodies. Fixture files and summary are written owner-only under ignored `build-host/transport-preflight/`.

Only unambiguous numeric `stop_point:IDFM:<id>` platform identifiers are mapped to `STIF:StopPoint:Q:<id>:`. Ambiguous rail/area identifiers are retained in the discovery fixture for explicit mapping work rather than guessed. If discovery has no mappable platform, the official documented Châtelet platform is used solely to test departure API access, not as the user's nearby service. The traffic response is checked for the requested line ID. Quotas and actual compact response sizes must inform worker limits and polling before firmware activation. PRIM generic Navitia schedules must not be represented as live departures; live data uses SIRI StopMonitoring.

Tests: `python3 tests/test_transport_preflight.py` verifies platform mapping boundaries, provider errors, request filtering and credential-safe owner-only captures. Live preflight remains blocked in the agent environment by unavailable DNS; the user's normal Terminal can access PRIM. No transport firmware is activated by this script.


## Firmware 0.6.0: nearby transport

The user's November 13 preflight confirmed eight nearby numeric platforms and two live visits each at the first two platforms. The traffic probe used fallback RER C (C01727), because nearby discovery did not include line identifiers; its 2.55 MB response is not evidence of traffic coverage for the user's local buses. Firmware obtains actual line references from SIRI departures instead.

The background transport task discovers up to eight supported platforms within 1 km of the confirmed home coordinates. Previous/next selects a platform; up to six line/destination rows show the next two reported departures. The clock shows the selected platform and its first departure row. Departures publish before optional line-name and traffic requests complete. Expected times are used where present; aimed times are labelled scheduled. Expired times are removed and data older than six minutes is marked out of date. A location change invalidates old results; queued results from another location or selection are rejected.

Discovery is cached for six hours, selected-platform departures refresh every five minutes, and traffic for up to three actual lines refreshes every fifteen minutes. Manual refreshes have a ten-second minimum interval. A conservative 900-request ceiling applies per running 24 hours (all transport endpoints combined); this development counter resets on reboot and does not replace provider-side quotas. HTTP 429 backs off fifteen minutes. No network request or JSON parsing runs on the LVGL task.

Each response is capped at 128 KiB, including chunked transfers. Oversized traffic responses are rejected and displayed as unavailable; they are never interpreted as an absence of disruption. Complete JSON, matching stop/line identifiers, TLS certificate verification, no redirects and HTTP status checks are required. Existing saved data can remain visible with its age during an outage. Traffic notices rotate every eight seconds.

`IDFM_PRIM_API_KEY` is read from the ignored root `.env` by `scripts/generate_transport_config.py`. CMake generates an owner-only header inside its ignored build directory; the key is not put on the compiler command line or exposed to the phone panel. The development firmware binary contains this credential: do not distribute it as a commercial product with a shared personal key. The watcher notices `.env` changes without printing its contents.

Validation: `scripts/test-transport.sh` exercises trimmed captured fixtures, stop matching, expired departures, invalid timestamps, string-valued provider status, Unicode, line relevance, actual worker HTTP size/deadline/authorization/quota handling and old-selection/location rejection. Native UI tests exercise navigation, refresh feedback, stale values and location invalidation. Weather, network, core, phone and watcher regression suites are also checked. ESP32-P4 translation-unit compilation passes. Full build is blocked at ESP-IDF component-manager process inspection (`psutil` / denied `sysctl`); see `build-host/transport-build.log`. No 0.6.0 flash or on-device acceptance is claimed.

Limits: only numeric platform identifiers with verified SIRI quay mapping are supported; SNCF stop-area mapping remains outstanding. The eight platforms are selectable, not all polled simultaneously. Line metadata and actual local-line traffic endpoints still require device/live acceptance. Large traffic responses remain unavailable until a smaller provider contract or streaming implementation is added. OTA download/update remains outstanding.

To build and app-flash automatically from an ordinary Terminal, close serial monitors and run `python3 scripts/watch-device.py` from the project folder. Confirm firmware 0.6.0 and check that the selected nearby platform, departure minutes, accents, clock summary, stop switching and offline/stale states behave correctly on the display.


## Firmware 0.6.1: PRIM TLS protocol mismatch

Confirmed cause: the device log showed `mbedtls_ssl_handshake returned -0x7780`, then HTTP 0 / zero bytes. The independent host probe in `build-host/transport-tls.json` rejected all TLS 1.2 variants with `TLSV1_ALERT_PROTOCOL_VERSION`; TLS 1.3 succeeded with `TLS_AES_256_GCM_SHA384`. Firmware 0.6.0 enabled TLS 1.2 only, so no authenticated API request could be sent.

Enabled TLS 1.3 and certificate-based ephemeral key exchange in both sdkconfig and sdkconfig.defaults, retaining TLS 1.2 for other services. Transport explicitly requests TLS 1.3 through ESP-IDF's HTTP client. A target compile guard rejects configurations lacking TLS 1.3 or ephemeral key exchange. Certificate-bundle validation and hostname checks are retained; no insecure fallback is used. The production worker HTTP test asserts the TLS version, failed before the fix and passes after it. Weather/network tests also pass. Target compile checks use the requested TLS configuration; full IDF build still fails in the restricted environment at process inspection. Firmware 0.6.1 must be built/flashed through the normal Terminal watcher; successful on-device HTTPS retrieval remains to be verified.

Stop the serial monitor with Ctrl+] before running the watcher. After flashing, capture a fresh log and confirm `transport: HTTP 200` with complete responses and departures on screen. A subsequent traffic-size rejection is distinct from this handshake failure.


## Firmware 0.6.2: PRIM cross-signed certificate chain

The 0.6.1 runtime capture progressed past protocol negotiation but failed with “No matching trusted root certificate found”. The desktop diagnostic captured the actual presented chain: PRIM → WE1 → GTS Root R4 cross-signed by GlobalSign Root CA. The desktop verified chain instead ends at self-signed GTS Root R4. The SDK full bundle already contains GTS Root R4, while its default verification path follows the presented cross-sign to the absent GlobalSign root.

Enabled `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` in both existing and clean-build configurations. This selects ESP-IDF's trusted-certificate callback, allowing chain construction through the existing GTS root. No downloaded certificate was added as a trust anchor, no deprecated roots enabled, and hostname/signature checks remain in force. A compile guard prevents transport building without cross-signed verification. TLS configuration regression checks run as part of `scripts/test-transport.sh`.

Independent validation: OpenSSL successfully verified the exact captured server chain and PRIM hostname against the installed SDK's existing `cacrt_all.pem`. Transport parser/HTTP/configuration tests pass, and application target compilation is checked with the requested configuration. Full ESP-IDF build remains blocked by process inspection in this agent environment; build log is `build-host/transport-cross-signed-build.log`. The fix is in source; on-device acceptance requires flashing 0.6.2 through the normal Terminal watcher and checking complete HTTP 200 responses.


## Firmware 0.6.3: two-kilometre transport search

Increased the nearby-platform query radius from 1 km to 2 km, including screen labels and the desktop preflight default. This changes geographic eligibility only: discovery still requests the eight closest supported platforms, so closer bus stops can fill the list before a rail hub 1.5–1.8 km away. SNCF stop-area mapping also remains outstanding. This release must not be described as guaranteed RER hub coverage. Hub discovery/prioritisation and verified stop-area departure mapping are separate follow-up work.


## Firmware 0.7.0: hubs, pickup predictions, station map and disruptions

Discovery now scans pages across the 2 km radius before applying the eight-station display limit. Stops are ordered RER, métro, bus/tram (equal rank), then other modes; distance breaks ties. Rail platforms with an explicit `netex_monomodal_stopplace` code are grouped into station areas and queried as `STIF:StopArea:SP:<id>:`. Known member quays are accepted when a provider returns platform-level MonitoringRefs; unrelated platforms are rejected. No area identifier is guessed from a station name. Discovery is bounded to 32 pages of 16 platforms and 90 seconds; failures or exhaustion show partial discovery and retry after five minutes. Complete discovery is cached six hours. Live RER discovery/response acceptance at the user's home remains to be checked; new hub fixtures exercise the documented contracts, not a claimed live rail capture.

Each row identifies destination and boarding point/platform where provided. It prefers expected arrival, then expected departure, then scheduled arrival/departure. Clock time and countdown are shown together, with an explicit Arrives/Departs/scheduled label for each of the next two visits. Reported cancellations remain visible. Positive expected-versus-aimed differences are displayed as delay minutes; a provider delay status without an amount is labelled “Delay reported”. The five-minute departure fetch cadence is unchanged; the locally updated countdown is not a new provider prediction. Observation age and stale states remain visible.

The Transport Map button opens an on-device geographic station map with a home marker, north orientation and 1 km grid. Numbered markers correspond to station names/modes/distances, and both markers and list entries select the station's services. This is a geographic overview without street tiles or route geometry. Stations lacking valid coordinates remain accessible in the list. It does not display inferred vehicle positions; live vehicle tracking needs a separately verified coordinate feed.

Disruption queries cover all distinct displayed lines (up to six), refreshed every thirty minutes. Active notices rotate, use public line codes when resolved, and disclose partial/unavailable coverage. Up to eight notices are retained; truncation is marked partial. A new streaming JSON projector drops heavy unused impact/geometry subtrees while retaining line associations, notice text, severity and status. It accepts at most 8 MiB of input, retains at most 256 KiB and has a 60-second total read budget checked between bounded reads; normal responses remain capped at 128 KiB. Certificate verification and quota limits are unchanged. Truncated/malformed/oversized responses never mean “no disruption”. The captured RER C JSON (4.4 MB pretty-printed capture, originally about 2.55 MB on the wire) projects to 221,244 bytes and yields relevant active alerts without parsing the full tree into RAM.

Validation: parser tests cover hubs on a later page after sixteen bus platforms, priority/distance, deduplication, station-area/member-quay matching, unrelated stops, arrival preference, delay and cancellation. UI tests cover passage labels, platform detail, map selection, stale data and location invalidation; rendered previews were inspected. HTTP tests exercise streaming projection through the production worker. Filter tests include one-byte and larger chunk boundaries, >320 KiB discarded fields, malformed discarded subtrees and output bounds. The full captured traffic payload passes AddressSanitizer/UndefinedBehaviorSanitizer projection tests. Application target compilation checks pass. USB watcher deployment is recorded separately in ESP-IDF build/flash logs; runtime acceptance is not inferred from host tests.

Development note: the running USB watcher flashed intermediate saves at 08:43:45, 08:44:31, 08:47:08 and 08:47:57 on November 13, causing intentional screen resets. Remaining streaming work was staged outside watched directories and copied in one final batch. Those flash events do not establish the absence of unrelated runtime crashes.

Deployment confirmation: the running user Terminal watcher successfully built 0.7.0 and flashed it at approximately 08:54 on November 13. `idf_py_stdout_output_99010` includes the streaming filter compilation; `idf_py_stdout_output_570` records verified flash data and the final RTS reset. This confirms deployment, not live RER results or device soak-test acceptance.


## Firmware 0.8.0: street map, station search and service details

The RER omission was reproduced against the live API: `stop_point:IDFM:monomodalStopPlace:43186` (Robinson, 1,477 m) and `...:46007` (La Croix de Berny, 1,818 m) were rejected by the old numeric-only stop-point parser. These now map to SIRI StopArea SP identifiers. Discovery separately queries RapidTransit, then Metro, then general nearby platforms, each bounded to eight pages and 90 seconds overall, retaining RER > métro > bus/tram ordering. The live Robinson SP query returned valid visits.

Search uses PRIM places, with a touch keyboard and visible loading/error/results states. A multimodal search result is resolved through its actual stop_points before querying SIRI: Robinson's multimodal ID 427230 is not its RER SP ID 43186. This avoids treating an entire station complex as an unrelated platform. Search and line-map selections retain a selection generation so an older in-flight response cannot overwrite them. Near home returns to the two-kilometre discovery list.

The native map now downloads OpenStreetMap raster street tiles with TLS validation, a dedicated background worker, a bounded 24-tile RGB565 RAM cache, cache-control expiry and an identifiable User-Agent. Attribution remains visible. Drag pans; +/- changes zoom; Home recentres; markers and the scrollable station list open live services. Tiles may take several seconds to load on the device; unavailable tiles leave station selection usable. No PRIM credentials are sent to the tile server. Use of the public OSM tile service is for this prototype; commercial distribution must select a suitable production tile-provider arrangement under its usage policy.

Departure cards are taller and scrollable. Tapping one opens stop-specific predictions, platform where supplied, freshness and relevant disruption notices. Arrival-only visits at a journey terminus are excluded from boardable services. The line-stations map fetches actual stops for the chosen line (47 for RER B in the live capture, bounded to 80). PRIM's sampled route geometry was empty and its stop list alphabetical: no invented route polyline or vehicle position is drawn. This is a station-location map, not a track diagram. Departures retain the existing five-minute provider refresh and local countdown; disruption refresh remains thirty minutes.

Validation: captured live nearby/search/station-membership/SIRI/line-stop fixtures, streaming network projection, Web Mercator round trips, selection-generation protection, search throttling, UI search keyboard/results, service details, zoom and station selection. Tile tests cover active pixel leases, eviction/expiry, cancellation, response size, cache headers and decoding a genuine OSM PNG. Existing transport, weather, phone and settings UI tests pass; rendered service cards and details were inspected. All application translation units compile with the P4 compiler. Changes were staged outside the USB watcher's paths and copied only after checks to avoid intermediate flashing. Full device build/flash and runtime acceptance are recorded below when observed.

Deployment: the normal Terminal watcher completed the full 0.8.0 build (`idf_py_stdout_output_11188`, followed by configuration-stable rebuild `19437`) and successfully app-flashed it (`idf_py_stdout_output_19876`: verified hash, RTS reset). Binary size 0x2ad080; 67% of the 8 MiB app partition remains free. The agent's separate build attempt was blocked by sandbox process inspection, but the watcher build succeeded. On-device interaction and sustained runtime stability have not yet been observed after this flash.


## Firmware 0.8.1: map memory fault and transport responsiveness

Both map entry points share the tile worker. The bundled LVGL 9.5 LodePNG implementation returns an `lv_draw_buf_t *` disguised by its `unsigned char **` API. Version 0.8.0 treated that descriptor as 256×256 RGBA pixels and freed only the descriptor. An expanded test running the actual HTTP/decode/conversion worker reproduced an AddressSanitizer heap-buffer-overflow just beyond a 40-byte allocation. The previous decoder-only test verified dimensions, not the production conversion, and missed this fault. Version 0.8.1 reads `decoded->data` with its stride and destroys the complete draw buffer with `lv_draw_buf_destroy`. ASAN/UBSAN now passes the production worker, including 40 further tile loads and cache reuse. UI navigation tests now provide real RGB565 tile buffers and verify leases are released on leaving a map.

Transport selection previously waited behind uncancellable metadata/disruption downloads for every displayed line, plus a ten-second selection gate. Station search could resolve a complex but not reach its SIRI fetch promptly. User commands now increment a generation checked between HTTP reads and requests. Obsolete work is discarded, and selected-station departures run without the selection gate. The connection timeout is six seconds; body reads are bounded to two seconds and each streamed response has a twenty-second total budget checked between reads. The API/provider/network still determine actual response latency. Departures publish before background notices; line labels publish progressively without claiming disruption coverage before completion.

Nearby discovery still queries RER and métro independently, but stops generic bus pagination once the ranked display list is full. Automatic departure polling changes from five to two minutes, scheduled from the beginning of the cycle; remaining daily request budget can slow it further. The 900-request cap and 429 backoff remain enforced. Every displayed line still receives a disruption query; user interactions can interrupt that background work. Removed the redundant Next: prefix from the second passage.

Verification: actual transport worker test selects a searched station during a disruption body read, verifies cancellation, resolves Robinson's station complex to its RER stop area and observes published upcoming departures without a refresh-cycle wait. Parser/HTTP/selection tests pass. Real PNG conversion passes ASAN/UBSAN, repeated cache reuse and pixel-content assertions. UI tests cover populated tiles, zoom, leaving maps, search/details and absence of Next:. All application units compile for ESP32-P4. Saved runtime log predates 0.8.0 and direct serial open was denied by the execution environment, so these findings are reproduced code-path evidence, not a newly captured on-device panic trace. Device build/flash and subsequent runtime acceptance are recorded separately.

Deployment: the Terminal watcher successfully built 0.8.1 (`idf_py_stdout_output_24718`) and flashed it (`idf_py_stdout_output_25779`: hash verified, RTS reset). Build metadata confirms version 0.8.1. Binary size 0x2ad2c0, with 67% of the app partition free. Post-flash touchscreen interaction and sustained device stability remain unobserved by the agent.


## Firmware 0.8.2: La Défense large-hub response and startup latency

Live diagnostic requests around La Défense (approximate test coordinate 48.8922, 2.2369; not written to the device) returned RER discovery in 0.786 s, métro discovery in 0.331 s, general discovery in 0.335 s and SIRI departures in 0.645 s on the development computer. La Défense SP:470549 returned 243 visits in 334,417 bytes. The 0.8.1 departure path capped unprojected SIRI responses at 128 KiB, rejected this valid response and waited two minutes before repeating the same failure. This is a reproduced firmware limit, not evidence that PRIM itself is generally slow. No current serial panic/runtime capture is available; the most recent saved device log still predates 0.8.0.

SIRI now uses a streaming field projection, bounded to 8 MiB wire input and 512 KiB retained JSON, preserving status/errors, stop and line identity, timing, destinations, platforms and cancellation/delay fields. The actual La Défense response projects to 156,302 bytes and produces six display rows. Error/malformed/oversize responses still fail explicitly; TLS validation and quota enforcement remain intact. Request logs now identify departures versus discovery/metadata/disruptions and include elapsed milliseconds.

Startup publishes the first available high-priority station's departures before completing métro/bus discovery, with a visible additional-discovery indicator. It reuses that successful preview rather than downloading it again at the end. Temporary failures retry at 15, 30, 60 and 120 seconds; authentication and quota backoffs remain separate. Reconnection retries immediately. Failed disruption checks wait five minutes before another background attempt unless a new line appears; merely changing destinations/row count no longer forces a full disruption download. Automatic departure refresh remains two minutes subject to remaining quota.

The captured response also exposed a train already boarding: its expected arrival was past, while its departure was still ahead. The parser now uses the upcoming departure for an arrived/at-stop train, instead of discarding it as an expired arrival. Its departure platform is used accordingly.

Validation: the new captured-response HTTP test failed on 0.8.1 and passes with the streaming path. Tests check actual La Défense rows, the boarding train to Poissy, provider-error preservation and oversized input rejection. Actual worker tests verify first publication before métro discovery, transient retry timing, reconnect retry, station selection during background traffic and stale-response suppression. ASAN/UBSAN passes the production HTTP/filter/parser/worker test. Existing model and UI tests pass; all application translation units compile for ESP32-P4. Desktop API timings do not establish on-device latency; device runtime acceptance remains pending after deployment.

Deployment: the running Terminal watcher successfully built version 0.8.2 (`idf_py_stdout_output_32635`) and app-flashed it (`idf_py_stdout_output_33705`: hash verified, RTS reset). Build metadata confirms 0.8.2, binary size 0x2ad780, 67% app-partition space free. Device-side retrieval timing and touchscreen acceptance still require observation after this flash.


## Firmware 0.8.3: departure rows outside the visible scroll area

The fresh user capture at 15:18 confirms a complete departure HTTP 200 response (7,966 bytes on the wire, 3,568 retained) in 2,081 ms and an advancing display heartbeat. It does not contain parsed-row/publication/UI diagnostics, so it cannot alone prove where the result was lost. An independent live La Défense métro query and current parser yielded two service rows.

A UI regression reproduces a concrete blank-list mechanism: scroll the six-row list, change station, then publish a one-row result. LVGL retained scroll_y=292 and the updated first card was outside the visible viewport. The test failed before the fix with lv_obj_is_visible(first_card)==false. The list now returns to the top when location, station selection or selection generation changes, or the row count shrinks. Normal refreshes with the same count preserve user scroll position. The corrected test reports scroll_y=0 and first-row-visible=1; the rendered preview was inspected.

Added bounded diagnostics: parsing logs selected station ID, known member-quay count, success, rows and UTC; publication logs accepted/superseded results; the transport list logs revision, station ID, row count, validity, visible-card count and scroll offset. API credentials are never included. This permits a future capture to distinguish HTTP success from parser, state or viewport problems. Tests cover station changes, smaller result sets, preserving scroll for ordinary updates, existing transport parsing/scheduling, and ESP32-P4 application compilation. This is a reproduced UI defect consistent with the report; device runtime confirmation remains pending.


## 0.8.4 — Wi-Fi memory, password visibility and interaction feedback

14 November 2026. Five successful Wi-Fi profiles are retained in NVS, most recent first. The existing last-network record remains compatible. New credentials replace a remembered password only after IP acquisition; failed attempts preserve the working profile. Saved scan results are marked “saved”; leave the password field blank to reuse credentials. Show/Hide reveals only the currently typed value and resets to masked on selection/submission. Credentials are never returned by the phone API.

Wi-Fi shows a spinner, elapsed connection time, scan status and automatic-retry countdown. Header connection text animates while connecting. Common touchscreen buttons and service cards acknowledge taps briefly; asynchronous operations retain their own progress/result messages. Station/map selections now report queue failures. The phone panel adds pending button captions/spinners for view changes, settings and address searches, blocks duplicate requests, and protects the pending location request from status-poll re-enabling.

QR generation previously cached the requested URL before successful rendering, allowing a failed generation to become a blank visible QR. Cache updates now follow success only, failures remain hidden and retry automatically. The disconnected and connected-but-server-unready messages are distinct. A quiet zone is enabled for reliable scanning.

Feedback audit: navigation, themes, font, brightness, rotation, profile and city use immediate visual changes plus save/result text; Wi-Fi scan/connect/reconnect use progress states; weather/transport refresh and station/line lookup use request/loading/result states; map actions change the map and acknowledge taps; rejected station/line selections report retry messages; phone saves/search/location/view changes show pending states and errors; reconnection preserves edits. External Shortcuts execution remains owned by iOS.

Validation: deterministic real network-worker tests cover multiple saved profiles, blank-password reuse, failed replacement preservation, saved scan flags and startup restore. LVGL tests cover Show/Hide without changing the entered value, masked submission, saved-network connection, QR rendering failure/retry and reconnect. Phone protocol/server/connection/UI tests pass, including immediate view-request feedback. ESP32-P4 translation-unit compilation passes. Full watcher build/flash and on-device acceptance are recorded separately below; OTA is still outstanding.

Deployment verified: local watcher completed the full 0.8.4 build and app-flash on 14 November at 14:49. Build log `firmware/build/log/idf_py_stdout_output_49258`; flash log `firmware/build/log/idf_py_stdout_output_50412` ends with “Hash of data verified” and “Hard resetting via RTS pin”. Physical touchscreen and runtime soak acceptance remain unobserved by the agent.


## 0.8.5 — deferred scan regression and phone setup recovery

The Wi-Fi page skipped its initial scan when opened while `network.connecting` was true. No later transition retried it, leaving the initial disabled “Looking for networks” dropdown indefinitely. A failing LVGL regression test reproduced this path before editing. The page now retains a deferred scan request, visibly waits for connection completion/timeout, then starts it automatically. A dedicated spinner beside the list and animated text show waiting/scanning; failures explicitly replace the placeholder. Tests verify the animation changes, scanning starts, and the results dropdown opens.

Phone setup: serial access remained denied (`Operation not permitted`); the available capture was from 13 November and the last known HTTP address timed out. Consequently the exact live QR failure was not established. The one-shot HTTP startup failure path is now recoverable every five seconds; partial route registration stops the incomplete server before retry, and the URL is exposed only when all routes are registered. Fault-injection server tests cover start failure, partial route failure, recovery and idempotent start. The UI shows a waiting spinner and a direct Wi-Fi settings button. Transition logs identify Wi-Fi, IP, or HTTP-server unavailability without logging pairing tokens. Scan logs record start, failures and result counts without SSIDs/passwords.

Validation: network and phone suites, LVGL regression tests and ESP32-P4 translation-unit checks passed. The rendered connected setup page was visually inspected and contains a clear QR. These host checks do not establish physical-device QR availability; deployment result follows.

Deployment: watcher completed the 0.8.5 full build and app-flash; `firmware/build/log/idf_py_stdout_output_56113` confirms hash verification and hard reset. Physical Wi-Fi scanning and QR availability remain to be observed.


## 0.8.6 — keyboard usability and redundant rendering

Capture from 14 November 15:09:46: disconnects at 491546 and 542431 ms precede a station stop/start at 556292/556419 ms. Association succeeds at 559366 ms and DHCP at 560403 ms: approximately four seconds from the successful attempt to IP acquisition. Weather HTTP 200 and six La Défense departure rows follow. Disconnect reasons were absent, so the failed attempts are not diagnosed; added reason-code and IP-acquisition logs for recurrence. Two disruption downloads were incomplete despite HTTP 200; this is separate from association success. No reset/panic appears in this capture.

Display heartbeat/frame counters advanced 171 and 178 times over consecutive 30-second windows, approximately 5.7–5.9 Hz. Host reproduction showed 20 redraws in two idle seconds with the Wi-Fi keyboard open. Repeated unchanged text/style setters and header alignment caused unnecessary invalidation. Conditional text/colour/alignment updates reduce the identical host scenario to five frames (cursor feedback remains). This demonstrates reduced unnecessary work, not a measured physical touch-latency improvement.

The shared keyboard grows from 1216×226 to 1248×310, retains four rows, uses 32-pixel text, clearer key spacing and contrasting pressed-key colours. Key popovers are disabled. The password field and Show/Connect controls remain above it; navigation remains below. The same keyboard serves profile, city and station search input.

Tests cover lowercase/uppercase/numeric/backspace entry, keyboard dimensions/font, open/close, and the idle redraw budget. Existing network/phone/UI suites and ESP32-P4 compilation pass. The rendered keyboard was visually inspected. Device deployment and physical typing acceptance are recorded separately.

Deployment pending: watcher was stopped for the runtime capture. Agent full-build attempt was blocked by ESP-IDF component-manager process inspection (`psutil` / permission denial), logged in `/tmp/keyboard-086-full-build.log` and `firmware/build/log/idf_py_stderr_output_61000`. No 0.8.6 flash is confirmed; restart the existing Terminal watcher to build/app-flash.


## 0.8.7 — transport layout, route diagrams and location clarity

The transport heading now has a fixed one-line height and separate metadata line for station position, distance and update status, preventing the overlap shown in the user's photo. Long headings truncate within their allotted space.

Line service details now open a Route diagram rather than the geographic station list. Verified PRIM `lines/{line}/route_schedules` table rows provide travel order. A captured RER B response (duration=60, count=16) yields eight directional branch combinations and 47 distinct stations; the streaming projection drops timetable columns/geometry/disruptions while retaining ordered rows. The schematic displays a coloured connector with selectable station nodes and a direction/branch selector. Initial branch selection favours the selected boarding station and destination. All route indices refer to the shared station model used for live-service selection. Responses remain bounded to 8 MiB wire/256 KiB retained; at most 16 branches and 80 distinct stations are displayed, with incomplete coverage marked. It is a per-direction line diagram, not a full network/interchange map. Failed loads show Retry on the same page.

The native street map continues to use OSM tiles. Static marker/list positions update only when geometry, station revision or location changes; repeated identical drag positions/status labels are ignored. Marker events bubble into map dragging and a drag does not select a station. The host idle-map regression records zero redraws over two seconds. Physical drag latency is not measured by this test.

Home location now displays the device's saved place name, coordinates and time zone separately, and refreshes if the phone changes that record. The old picker selected Paris by time-zone equality even for precise Châtenay-Malabry coordinates. The picker now defaults to Keep saved location; searching/applying a city explicitly replaces the shared location with city-centre coordinates. Manual city saves now persist the city name as well. Phone Confirm location and screen Use city both update the same device NVS record; there is no independent phone location authority.

Apple automatic Wi-Fi password sharing is not implemented/supported by this ESP32 project. The existing phone panel does not read iOS saved Wi-Fi passwords. A separate phone-assisted provisioning workflow would be needed; none is claimed by this release. Apple/Google browser-map libraries are not integrated into the native LVGL screen.

Validation: actual captured route payload passes the production streaming HTTP path/parser, with eight branches, 47 stations and verified first/last stop order. UI tests exercise branch rendering, station selection, saved-place display, geographic-map idle redraw budget, keyboard and existing navigation. Network/phone/transport suites and ESP32-P4 translation-unit compilation pass. Rendered location, heading and schematic previews were inspected. Full device deployment and physical acceptance remain separate.

Deployment pending: no watcher build/flash log appeared after releasing 0.8.7. Start `python3 scripts/watch-device.py` in the normal Terminal to build and app-flash. This includes the previously pending 0.8.6 keyboard improvements. No new hardware acceptance is claimed.


## 0.8.8 — nearby return and distant-station map loading

Transport navigation previously only reopened the current station and the nearby reset action was confined to search. The bottom Transport tab (and phone-requested Transport view) now resets to the saved-location nearby list. Near me is available on services, detail, search, route diagram and street-map pages. Services preserves the chosen station. My location on the street map recentres the geographic viewport without changing service selection.

Before a searched/route station replaces the nearby snapshot, its nearby station list and location generation are cached. Near me restores that list immediately if the home generation matches, resets selection to its first prioritised station and clears stale departures/alerts while refreshing. A changed location rejects the cache and triggers discovery. Existing operation and selection generations reject obsolete replies.

The street map previously fitted both the remote station and home and loaded tiles by grid/index order using a fresh TLS client per tile. It now opens at zoom 15 centred on the selected station, falling back to home only if station coordinates are unavailable. Centre tiles are queued first; the tile worker honours FIFO request order independent of cache slot. Successful sequential tile requests reuse the HTTP client/connection; transport errors discard it for a clean retry. TLS validation, bounded cache/response size and image ownership rules remain unchanged. Loading progress reports acquired/required tile counts; service selection remains usable while tiles load.

Validation: UI tests exercise Near me and Transport-tab reset, remote-station centre/first tile, service navigation and existing map/keyboard behaviour. Production-worker tests verify cached nearby restoration and invalidation after moving home. Real PNG tile-worker tests verify one client for two sequential requests, alongside lease/eviction/cache checks. Transport/phone/UI suites and ESP32-P4 compile checks pass. Layout preview inspected. Network latency and physical map performance still require device observation.

Deployment confirmed: local watcher built and app-flashed 0.8.8; `firmware/build/log/idf_py_stdout_output_72650` records hash verification and hard reset. Physical-device acceptance remains unobserved.


## 0.8.9 — route progress and multimodal connections

Root cause: ordered route rows selected a monomodal stop ID. This queried only that mode; the nearby eight-station limit also cannot represent every boarding point in a large interchange.

Route loading now has a prominent animated panel, followed by an animated connecting-lines status. Failures expose Retry. Route rows retain the provider's parent stop-area ID. A single line/stop_areas request enriches the diagram with connecting line/mode labels; no station names or example-specific IDs are hard-coded. Ordered routes are published before enrichment. Existing branch selection survives enrichment.

Tapping a route stop opens an animated connection picker. It queries the parent's stop_points at depth 3, shows modes and line codes, and allows selection of up to 80 boarding points independently of the eight nearby stations. Returned-data truncation is signalled. The device's saved home location remains unchanged. Provider-defined interchange membership is used; proximity alone does not imply an interchange. Line summaries are bounded; the picker provides the boarding-point list.

Validation: production HTTP projection/parser tests with live public PRIM captures verify 47 route parents, interchanges across multiple stations (including metro, bus and tram), all 21 returned boarding points in a captured interchange, and selection beyond index 8. Worker lookup tests verify the parent-area endpoint and depth. LVGL host tests verify animated loading, failure/retry visibility, enriched labels, and a scrollable list beyond eight entries; existing navigation/Wi-Fi/QR/weather/keyboard assertions pass. Rendered loading, route and connection views inspected. ESP32-P4 translation-unit compilation passes. Physical interaction and runtime stability remain to be checked on the display.

Deployment confirmed: the watcher built version 0.8.9; `firmware/build/log/idf_py_stdout_output_79482` verifies the current binary at 0x20000 and records a hard reset. Physical-device interaction/stability acceptance remains unobserved.


## 0.8.10 — internal RAM exhaustion after interchange expansion

Capture at 16:15 on November 14 shows 0.8.9 connecting Wi-Fi and synchronising time, then mbedtls_ssl_setup returning -0x008D (insufficient memory) and an SDIO RX allocation failure for 1536 bytes. Both weather and transport subsequently fail to connect. The linked ELF places the four enlarged transport search/route snapshots in internal BSS: two at 0xc830 bytes and two at 0xd778 bytes (212816 bytes total). This is a firmware memory-placement regression, not evidence of an automatic-rotation fault.

The four persistent snapshots now use ESP-IDF EXT_RAM_BSS_ATTR with PSRAM BSS enabled. A target compile-time guard rejects configuration without this capability. Internal RAM remains available for Wi-Fi/DMA/TLS; TLS verification and allocation configuration are unchanged. Every 30 seconds the health log now reports free, largest and minimum internal 8-bit heap to make future pressure visible. Host transport and full LVGL UI regressions pass. Target linker placement and device recovery require verification after rebuild.

Deployment blocked in this session: the watcher did not produce a new build after the capture. Direct build fails during ESP-IDF component-manager configuration because psutil process enumeration is denied (`sysctl: Operation not permitted`). No 0.8.10 flash is claimed. `scripts/check-transport-memory.py` inspects ELF sections and correctly rejects the 0.8.9 internal-BSS placement; run it after the local rebuild to verify the fix in the linked target.


## 0.8.11 — balanced clock landing screen

Implemented the approved 1280×800 proposal: centred clock/date, 416-pixel compact weather card and 776-pixel transport card, equal card height and 24-pixel gutter. The clock header shows the saved location beside Wi-Fi. Other pages retain date/time. Existing digital, slim and modern clock choices and custom colours remain.

Weather shows current conditions, feels-like/low/high, and a six-hour temperature sparkline; the full weather view retains its detailed charts. The wider departure summary shows two service rows from the current selected station snapshot (not fabricated departures from multiple hubs), station/destination, first waiting time, secondary prediction or delay/scheduled status, and separate disruption text. Cancels/stale data remain explicit. Both cards are tappable and provide pressed feedback; updating spinners preserve cached content.

Validation: full host LVGL tests pass, including two rows, cancellation, animated loading with retained predictions, all font choices and 20-second automatic rotation. Rendered digital and modern clock screens inspected. ESP32-P4 translation units compile. Existing memory-placement checker confirms the 0.8.10 external-BSS fix remains in the built target. Preview: `docs/design/clock-implemented.png` (host fixture data, not current live weather/departures).

Deployment confirmed: watcher built version 0.8.11 and app-flashed it; `firmware/build/log/idf_py_stdout_output_2500` records hash verification and hard reset. The linked target passes `scripts/check-transport-memory.py` (212816 bytes in external BSS). Physical display acceptance remains unobserved.


## 0.8.12 — clock composition and Light font

Reworked the landing screen as a complete composition: quieter 18px header, centred clock/date, a continuous summary panel with a fine column divider, destination-first departure rows and a compact centred navigation bar. Full-size navigation returns on the other views. The station name appears in the transport heading; rows show destinations, boarding/platform information and predictions. Weather retains its sparkline and removes the redundant permanent chart caption; warning/loading feedback remains.

Added a fourth clock font, Light, on both the device and iPhone settings. Uses Inter at weight 300, optical size 32 and 192px native rasterisation, with no runtime scaling/layers. Font source and SIL OFL license are included; `scripts/generate_light_clock_font.c` reproduces the clock glyph asset using FreeType. Existing font IDs, stored settings and user selections are preserved. Light uses ID 3; validation and the phone contract accept it.

Validation: host LVGL suite (all previous screens, prediction/cancellation/loading feedback, keyboard and 20-second rotation), settings round-trip for Light, phone parser bounds and full phone test suite pass. ESP32-P4 translation units including the generated Light font compile. Actual LVGL Light rendering inspected; `docs/design/clock-light-implemented.png` uses illustrative fixture data. Deployment pending: no watcher build appeared for 0.8.12. A direct build attempt failed during component-manager process enumeration (`PermissionError: Operation not permitted`, sysctl). No 0.8.12 flash is claimed; restart the local watcher to build and flash.


## 0.8.13 — equal widgets, clock spacing; current navigation unchanged

User correction: keep the CURRENT bottom buttons exactly as they are; do not restore the earlier full-width navigation. The entire show_view/navigation styling block was compared byte-for-byte against 0.8.12 before release and is unchanged.

Weather and transport are separate equal 592×328 cards, aligned at y=350 on the screen, with 32px page margins and gutter. Both use 24px internal padding and matching header/content baselines. Clock/date spacing is corrected, including scaling digital/slim glyph geometry so it cannot collide with the date. Long summary labels have fixed single-line heights and ellipsise instead of overlapping boarding information. Cancellation/out-of-date waiting labels use smaller typography to fit their reserved column.

Host LVGL regression tests pass with explicit assertions for equal card dimensions, aligned tops, single-line destination bounds and unchanged current navigation geometry. Light and digital renders inspected. Target translation-unit compilation passes. Preview: docs/design/clock-equal-implemented.png (illustrative fixture data). Deployment verified: watcher built and flashed v0.8.13; idf_py_stdout_output_10243 records hash verification and hard reset. Linked-target external-BSS check passes.


## 0.8.14 — equal optical spacing and useful landing summaries

Time/date alignment now measures visible glyph bounds with LVGL's placement formula. All four clock styles anchor their visible bottom at the same point; the date's visible text is centred between the time and widget top. Segment positions derive from fixed base geometry so changing digits cannot accumulate offsets. Pixel-level tests of all four styles at 00:00, 11:11, 12:34 and 23:59 measure exactly 32px of whitespace above and below the date.

Transport now shows up to three available service rows, preserving destination, boarding/platform details, countdown and next prediction or readable delay/scheduled status. The boarding subtitle includes HH:MM when fresh, replacing redundant repetitions of the station name. Weather adds peak hourly rain probability across the upcoming six forecast samples and wind speed; the chart labels actual forecast times rather than claiming its first future sample is 'Now'. Feels-like and daily range remain. Both widgets retain equal dimensions and loading/cached/error states. Current bottom navigation was verified byte-for-byte unchanged.

Validation: full host UI suite including pixel alignment, three-service content, delays, weather fields, existing navigation/keyboard/QR and auto-rotation passes. ESP32-P4 translation-unit compilation passes. Rendered preview uses test fixtures. Deployment verified: watcher built and app-flashed v0.8.14; idf_py_stdout_output_12162 confirms hash verification and hard reset. The linked image passes the external-BSS memory check.


## 0.8.15 — graph-free weather and independent nearby departures

Removed the line graph from the clock weather card; retained the detailed weather view chart. The summary shows conditions, temperature, feels-like, daily range, upcoming six-sample peak rain probability, wind, humidity and update time. Equal card dimensions, optical clock/date spacing and the complete current bottom navigation block are unchanged.

The previous three-row UI still used only the selected station response, so stations with one direction legitimately displayed just one service. Added an independent nearby overview: query up to three priority-ordered nearby stops, reusing selected-stop data, and display one actual departure group per stop before filling remaining spaces with distinct directions at the selected nearby stop. Each entry carries its station and original fetch timestamp. Failed refreshes preserve prior data with stale indication; loading is animated. Selected-station detail remains independent. Requests stay serial, cancellable and quota-limited; polling allowance now accounts for the extra departure requests. Fewer rows remain possible when providers supply fewer predictions. Overview delay indicators come from each prediction; full service alerts remain in transport details.

Large overview scratch buffers and persistent snapshots use external BSS. Validation: production-worker regression proves one selected-stop row produces three nearby-stop rows and failed refresh retains its original timestamp; transport suite passes. Host LVGL verifies multi-station rendering, freshness/loading feedback, equal cards, unchanged navigation and 32px date whitespace across all four styles. Target translation units compile. Rendered preview docs/design/clock-summary-0815.png uses illustrative fixtures. Deployment verified: local watcher built and app-flashed 0.8.15; idf_py_stdout_output_17759 records hash verification and hard reset. Linked-image external-BSS check passes for all seven required buffers (226,256 bytes).


## 0.8.16 — ten clock styles, consistent navigation and larger street-map cache

Added Bold, Hairline, Condensed, Wide, Rounded Digital and Outline Digital to the existing Digital, Modern, Slim and Light designs. The four new Inter variants are rasterised at native size by scripts/generate_clock_variants.c (weight/width parameters; existing SIL OFL license). No runtime font transforms or large composition layers. IDs 0–3 remain compatible; validation, persistent settings and phone controls accept IDs 0–9. The date uses the same visible-ink spacing in every style. All four screens now use the clock screen's existing centred 148×52 navigation buttons, positions, typography and colours, as requested.

Street-map cache increased from 24 to 72 tiles (9 MiB maximum RGB565 data). Tile and download buffers explicitly allocate in PSRAM to protect internal memory used by TLS/Wi-Fi. Only requested viewport tiles are downloaded; no speculative bulk fetches. Existing centre-first ordering and HTTP connection reuse remain. Viewport generation invalidates abandoned requests and prevents stale results being published after pan/zoom. Cache expiry and tile leases remain respected. Added animated loading feedback and per-tile byte/time diagnostic logs. This improves revisiting explored areas within the cache; first-time loading still depends on network/server latency and no on-device speedup is yet claimed.

Validation: host map tests traverse over 24 tiles and then retrieve earlier tiles without network requests, verify cancellation, expiry, leases, cache headers and real PNG decode. Full host UI suite passes: all ten styles at four times have equal 32px date whitespace, all screens share navigation geometry, map idle redraw remains zero. Settings/core and phone suites pass, and ESP32-P4 translation units compile. Bold and Outline renders inspected. Released to watcher; deployment pending.
