# iPhone setup and location — firmware 0.5.2

## Current delivery state

The firmware contains the local phone server, QR screen, appearance/view controls, precise-location persistence and hotspot help. Version 0.5.1 replaces the proposed hosted HTTPS helper with a generic Apple Shortcut handoff. No paid hosting or domain is required for this approach. The callback protocol, installation UI and conditional installer download are implemented and tested locally.

**Signed installer now supplied:** the command-line signing path rejected the generated source, but creating the complete workflow in Apple's native editor and choosing File → Export → Anyone succeeded. The signed archive is bundled by CMake. Its archive signature, exact source payload, coordinate references and HTTP download have passed local checks. Installation and positioning on a real iPhone are still unverified; do not advertise this as production-validated yet.

Full firmware linking/flashing, Safari interaction and real hotspot reachability remain unverified in this execution environment. Local socket binding, network DNS and ESP-IDF component-manager process inspection were denied. Source checks and deterministic tests are not device acceptance.

## Normal use

1. Connect the display through Settings → Wi-Fi. The display's touchscreen remains the initial network setup method.
2. On the display, open Settings → iPhone setup.
3. Scan the QR code with an iPhone on the same home Wi-Fi. The page is served by the display itself.
4. Change name, brightness, colour, font, view rotation or the active screen. Save appearance and wait for confirmation.
5. For the intended automatic-positioning flow, select Add location shortcut once, then accept Apple's installation screen. We supply the complete shortcut; the customer must not edit actions, IP addresses or tokens. The signed installer is supplied; Safari installation still needs on-device validation.
6. Tap Use my iPhone location, allow location access when iOS asks, then return to the local panel and check the map pin. Select Confirm location to save it. Alternatively, search an address and adjust the map without installing anything. Android users can use this same address/map path.

This is user-triggered positioning, not continuous tracking. When moving the display, connect its Wi-Fi, scan the current QR and repeat Use my iPhone location → Confirm location. The phone must be at the display's intended location. No customer account or subscription is required by our control panel. Map, geocoding and weather services still require internet access.

The display stores coordinates and a locality name, not merely a time-zone city. Weather requests use the saved coordinates. The existing location-generation signal invalidates old weather and is ready for a future nearby-stop lookup. Transport itself is not implemented in this version.

The clock's timezone is resolved independently from the coordinates. The previous working zone remains in use while resolution is pending; the pending flag survives restart. The new zone and bundled daylight-saving rules take effect after successful resolution. The display does not move its saved location merely because Wi-Fi changes. To move it, repeat positioning and confirmation from the local phone panel.

## Using iPhone Personal Hotspot

On the iPhone, open Settings → Personal Hotspot and enable Allow Others to Join. Enable Maximise Compatibility where available, which selects 2.4 GHz for this board. Keep the hotspot settings page open during scanning. On the display, select Settings → Wi-Fi → Rescan, select the iPhone and enter its hotspot password. See [Apple's hotspot troubleshooting](https://support.apple.com/en-ie/119837) and [Wi-Fi security / compatibility mode](https://support.apple.com/en-ca/guide/security/secfd166f620/web).

The existing Wi-Fi worker reconnects to the last selected network when it becomes available again. This increment does not implement automatic switching between several saved networks. To return to home Wi-Fi, select it on the display. The saved location is unchanged throughout.

Open iPhone setup and rescan its QR code after a network/IP change or device restart. The page uses the actual DHCP address rather than assuming a home or hotspot subnet. Reachability from the hotspot-hosting iPhone requires on-device confirmation; if iOS/network policy blocks that path, another client on the same hotspot can use the panel. Internet access and local-panel reachability are separate acceptance checks. No SoftAP provisioning, Bluetooth connection or USB tethering is added.

## Shortcut packaging and commercial distribution

`scripts/build-location-shortcut.py` generates the editable unsigned workflow under `companion/shortcut/`. It obtains Current Location, extracts Latitude and Longitude, and returns three text lines: `WSLOC1`, latitude, longitude. It embeds no device address, pairing token, user name or location. Apple's documented [signing command](https://support.apple.com/en-gb/guide/shortcuts-mac/apd455c82f02/mac) and [Anyone file export](https://support.apple.com/en-gb/guide/shortcuts-mac/apdf01f8c054/mac) are the intended distributor-side packaging mechanisms. The CLI path remains unsuitable in this environment; use the successful native File → Export → Anyone path. The canonical native source and artifact hashes are supplied alongside the signed file.

Only the final signed `companion/shortcut/Weather Station Location.shortcut` is eligible for firmware embedding. Customers download it from their display through Add location shortcut. The same artifact serves all devices. CMake rejects a plain plist at this production path; the archive header check is only a packaging guard, not cryptographic verification. `python3 tests/test_shortcut_artifact.py` verifies the pinned native export, archive signature and unpacked workflow content. It does not simulate iOS runtime or installation.

The local page launches Apple's documented [x-callback-url flow](https://support.apple.com/en-nz/guide/shortcuts/apdcd7f20a6f/9.0/ios/26). The `result` query returns coordinates; a random request nonce stored on the display and a ten-minute expiry bind the response to an authenticated request. The callback validates through `/api/location-request`; it does not rely on browser session storage. A new Safari tab or different default browser can return using the pairing token carried in the fragment. Validation is read-only and idempotent so a lost HTTP reply can be retried; it never saves the returned coordinates. Starting a new location request replaces the previous nonce. The pairing token remains in the fragment. Query parameters are removed from the visible URL immediately. Coordinates reach the display over local HTTP; do not log callback URLs. The shortcut should keep its supplied name, because the launch URL names it. A renamed/deleted shortcut must be recoverable through reinstall or address search.

No hosted helper is configured or contacted by the active phone page. `companion/location-helper/` is retained solely as the superseded prototype. Address lookup sends the requested address/coordinates to IGN; map loading sends the viewed area to OpenStreetMap. These disclosures appear in the page. Address search currently targets France. Live service responses and CORS remain unverified here.

For a product, the shortcut is optional convenience. Address/map setup must remain a complete supported path, including on Android. Do not promise a frictionless one-tap install until Safari download/import, permissions, app switching and callback tab/session behaviour have passed on clean iPhones. Also test denied permission, missing Shortcuts, renamed shortcut, multiple displays, network changes and a phone using a non-Safari default browser. Customers must never be asked to run developer signing tools or troubleshoot firmware.

## Local access and worker boundaries

The QR contains a fresh random 128-bit token generated each boot. API requests require that token in an Authorization header, and CORS is not enabled. Tokens are stored in the phone tab's session storage and removed from the visible URL after loading. Responses are not cached. There is no port forwarding or remote internet control. The local control panel is HTTP; it is intended for a trusted home network or password-protected hotspot and does not protect against an active attacker on that network.

Public static page assets expose no device settings. Authenticated JSON requests are limited to 1024 bytes and nesting depth 8. Invalid coordinates, settings ranges, fractional integer fields, control characters and trailing JSON data are rejected. Socket count and receive/send timeouts are bounded.

HTTP handlers queue commands; only the LVGL timer changes UI state. Existing settings and network workers perform NVS writes. Location-save completion and appearance-save completion are polled before the phone reports success. A full queue reports Busy instead of blocking. No network calls, TLS, geocoding or flash writes occur in a UI event callback.

## Validation and device acceptance

Run `python3 tests/test_shortcut_artifact.py`, `scripts/test-phone.sh`, `scripts/test-network.sh`, `scripts/test-weather.sh`, `scripts/test-ui.sh`, and existing display/startup guards. Phone tests execute production HTTP handlers with deterministic platform seams and embed real assets through the ESP-IDF CMake embedding script. UI tests cover QR visibility, network address changes, hotspot guidance and remote command application. Host previews are in `build-host/phone-setup-050.png` and `build-host/weather-header-050.png`.

Before accepting this release on hardware: build/flash 0.5.1, scan the QR in Safari, save/reload appearance, change precise location and restart, verify forecast coordinates/locality, install the final signed shortcut on a clean iPhone, deny/retry location permission, drag the map, switch to iPhone hotspot, rescan the changed QR, and check the display remains responsive throughout. Confirm the old display race regression does not return. OTA delivery remains outstanding.

## GPS hardware confirmation

The documented ESP32-P4-WIFI6-Touch-LCD-10.1 has no onboard GPS/GNSS receiver. The C6 is the Wi-Fi/Bluetooth coprocessor. This conclusion is based on the [manufacturer hardware description](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X) and [schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-X/ESP32-P4-WIFI6-Touch-LCD-X-Schematic.pdf), not a claim of physically inspecting every production revision.

## Automatic reconnection

The phone panel retries interrupted status requests automatically after 1, 2, 4, 8 and then at most 15 seconds between attempts. It polls every five seconds after successful responses. Requests are serialized and concurrent status reads share one request, so a six-second timeout cannot overlap the next scheduled poll. Returning to Safari or a browser online event triggers an immediate retry. Background polling pauses when the page is hidden.

A reconnecting indicator replaces the terminal timeout error. Unsaved form values and map selection remain in the current page. Display actions are disabled while disconnected and restored after a valid authenticated status response. Saving is never automatically replayed: a lost response might mean the device already saved, so the page reports an unconfirmed save rather than claiming success. A changed IP address requires the current QR link; HTTP 401 requires pairing again because tokens currently rotate on boot. Neither is silently bypassed.

Tests simulate timed-out requests, network loss/recovery, capped backoff, request deduplication, interrupted writes and expired pairing. A test executes the actual page script to verify draft/map preservation and automatic control restoration. These tests do not establish the cause of the photographed device timeout; live HTTP/network/reset logs were not supplied for that event. The original overlapping polling is a confirmed code defect and a possible contributor, not a proven complete explanation of that event.
