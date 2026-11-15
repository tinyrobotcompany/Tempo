# Home information display product requirements

Version 0.3 • 12 November 2026 • Requirements baseline and proposed acceptance targets

Build a mains-powered home display in a custom 3D-printed enclosure. One 10.1-inch touchscreen presents a clock, local weather and nearby public transport. An iPhone browser on the same Wi-Fi controls the device through a QR-code entry point. Additional views and cities must be possible without replacing the hardware.

This baseline records the user's requirements. Items explicitly marked proposed are implementation choices or measurable targets, not additional user commitments. The product name is provisional.

## Agreed scope

- One 10.1-inch ESP32-P4 touchscreen in landscape on a desk or shelf. This replaces the earlier 14-inch/Pi proposal. Working model: Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1; exact supplier revision and bundle remain to be confirmed.
- Standalone embedded device with onboard Wi-Fi coprocessor; no Raspberry Pi is required. Additional views must fit the firmware, memory and flash budgets.
- Clock appearance inspired by reference Image 1: black background, large orange seven-segment-style time, white secondary information and coloured accents. Add the full date.
- Weather dashboard inspired by reference Image 2, using location-based API data. The later API-only decision supersedes the initial request for physical weather sensors.
- Île-de-France nearby departures and disruptions, including relevant RATP and other regional services. Other cities later.
- Local iPhone browser control via QR code, easy view navigation, configurable appearance and brightness, optional automatic rotation.
- Automatic time synchronisation, daylight-saving changes, adjustable dimming and date/time settings.
- OTA updates are a first-release requirement, including recovery from failed updates.
- Purchased electronic components, custom application and enclosure; no battery backup, GPS module or outdoor sensor assembly. The smaller integrated display was selected to reduce cost and simplify the electronics; no fixed budget is specified.

## Users and journeys

The household owner powers the device, joins home Wi-Fi, sets the home location and selects nearby services. Household members glance at time/weather/departures and switch views without a phone if needed. A paired phone changes settings and installs updates. No account with this product's developer is required for everyday local control; external data providers may require credentials.

First-use flow: power on → on-device Wi-Fi selection and password entry → show LAN control URL and QR code → pair iPhone with on-screen confirmation → select/search a home city → confirm location → select radius, stops and directions → show clock. Touchscreen setup is the proposed reliable first-use method; it avoids requiring an already-connected phone to configure Wi-Fi.

## Functional requirements

| ID | Requirement | Acceptance condition |
| --- | --- | --- |
| DSP-01 | One display hosts independent clock, weather and transport views. | Each view is reachable from device and paired phone; adding a fourth registered view does not require changes to the first three. |
| CLK-01 | Large time, weekday, date, local outdoor temperature and humidity. | Orange-on-black default resembles the reference hierarchy; date remains visible; weather units and outdoor label are explicit. |
| CLK-02 | Automatic time and DST, city-based timezone selection, 12/24-hour and date format settings. No manual date/time entry. | Time synchronises on Wi-Fi; city selection controls DST. Remove diagnostic time-source/time-zone text from the clock face. |
| CLK-03 | Honest clock state after outages. | On cold boot without trusted time, show unsynchronised state rather than a plausible but incorrect date. No battery-backed clock is assumed. |
| WEA-01 | Location-based current conditions and forecast. | Temperature, feels-like, humidity, wind speed/direction/gusts, pressure, precipitation, condition, daily high/low and forecast are available when supplied. Missing fields show unavailable, never zero by default. |
| WEA-02 | Explain weather provenance. | Show provider, location and update time; distinguish model-derived conditions from local sensor measurements and forecast rain from measured rain. No indoor temperature or humidity claim. |
| LOC-01 | Store a fixed home location. | Searchable standard IANA time-zone city list replaces raw coordinate entry. Phone-assisted location is preferred for precise weather/transport location when available; the purchased board has no onboard GPS. City centres are approximate and must not be represented as the user’s exact location. User can change/delete the location; deletion and phone location remain to implement. |
| TRN-01 | Discover nearby stops and associated lines. | Configurable radius, mode filters and favourites; directions remain distinct; grouped station results avoid duplicate platform clutter. Default radius proposed: 800 m straight-line, labelled as distance rather than walking time. |
| TRN-02 | Display upcoming departures. | Stop, line, direction/destination, departure time or minutes and prediction status are visible. Support bus, metro, tram, RER and Transilien where supplied. |
| TRN-03 | Display relevant disruptions. | Link alerts to selected stops/lines and validity periods; show cancellations where supplied; expire resolved alerts. An API outage is not shown as normal service. |
| TRN-04 | Fit all relevant services into readable pages. | Configured favourites appear first; additional nearby results paginate. Selection is configurable; no promise to display every route simultaneously. |
| TRN-05 | Respect provider coverage and quotas. | Distinguish live, scheduled, stale, unavailable and no-service states. Requests are shared across views and phones; 429 responses back off. |
| NAV-01 | Simple local and phone navigation. | Visible view selector and touchscreen gestures/buttons; phone selects views; display wakes from dim/off state by touch. |
| NAV-02 | Optional automatic view rotation. | Enable/disable, order, included views and dwell time persist. Manual interaction pauses rotation for a configurable interval. |
| CFG-01 | Customise appearance. | Preset/custom colours, selectable clock fonts, day/night brightness, dimming schedule, units, language/date format and default view persist across reboot/update. Inactive seven-segment bars are invisible. Add a locally stored name/household-name welcome. Proposed initial languages: English and French. |
| CFG-02 | Adjust actual display brightness. | Phone slider and schedule change hardware backlight; software darkening alone does not fulfil this requirement. Screen-off mode can be reversed locally. |
| CTL-01 | QR access on home Wi-Fi. | Current LAN address and hostname are shown; iPhone Safari reaches controls without installing an app. IP-address fallback works when local hostname discovery fails. |
| CTL-02 | Pair and revoke household phones. | Physical display confirmation or short-lived pairing code authorises control; sessions can be revoked. QR contains no provider token or Wi-Fi password. |
| NET-01 | Recover from network changes. | Automatic reconnect; Wi-Fi can be changed on-device; no router port forwarding. Clock and local settings remain usable during internet failure. |
| EXT-01 | Add views and providers later. | Versioned view registration and separate transport/weather adapters; new code arrives through signed releases. Arbitrary third-party scripts are outside v1. |
| OPS-01 | Make the device maintainable. | Show version, connection state, provider errors and update result; export redacted diagnostics; allow settings export/import and confirmed factory reset. |

## OTA requirements

OTA means remote delivery over home internet/Wi-Fi of signed ESP32-P4 firmware, including the display application, embedded runtime and bundled phone-control assets. Maintain a compatible ESP32-C6 Wi-Fi firmware version; coordinated coprocessor updates require their own recovery validation. Routine updates must not require opening the enclosure or reflashing storage.

| ID | Requirement | Acceptance condition |
| --- | --- | --- |
| OTA-01 | Check, download and install from the control panel. | Display installed/available version, release notes, size, progress and outcome; owner initiates install by default. Optional scheduled installs are explicit opt-in. |
| OTA-02 | Authenticate releases. | Verify release signature, content integrity and hardware compatibility before activating anything. Reject unsigned, corrupted and incompatible releases. Private signing keys never reside on the device. |
| OTA-03 | Preserve a working system. | Write the inactive firmware slot while retaining the previous bootable slot. Only select a fully verified candidate for trial boot. |
| OTA-04 | Automatically roll back failed boots. | A candidate failing local health checks or exhausting trial attempts returns to the previous version without phone intervention. Health checks include display, local control, settings and services; internet/provider availability is not a success condition. |
| OTA-05 | Survive power/network interruptions. | Interrupt download, writing, activation and trial boot in hardware tests; after power returns, a valid old/new system boots. No battery is assumed. Interrupted download is safely retried. |
| OTA-06 | Preserve settings and credentials. | Wi-Fi, location, favourites and themes survive success and rollback. Schema migrations are backward compatible or recover a pre-update snapshot. |
| OTA-07 | Bound update operations. | Check free space, available power/thermal fault indicators and compatibility; prevent simultaneous installs; report failure and retain a usable system. |
| OTA-08 | Provide recovery and servicing. | Retain last-known-good release, document USB flashing recovery, expose update history without secrets, and test signing-key rotation. Bootloader, partition-table and Wi-Fi coprocessor updates have separate tested recovery procedures; routine application OTA must not overwrite them. |

Proposed release gate: 20 successful normal update cycles and at least five power cuts at each relevant phase, including configuration migration and success-marker persistence. These are engineering qualification targets, not evidence that arbitrary storage hardware cannot fail.

## Quality and privacy targets

Proposed targets: clock visible within 45 seconds of power-on; local view changes within 300 ms; phone settings reflected within one second on healthy LAN; 72-hour continuous operation without crash, thermal throttling or unbounded storage growth. Validate night readability and clock legibility at 2–3 metres, and detailed weather/transport readability at 0.5–1.5 metres. Target four or five transport rows per page at 1280 × 800 landscape, subject to a full-size mockup. Test current iPhone Safari and the previous major supported release at implementation time.

Keep home coordinates, Wi-Fi credentials and provider tokens on the device; transmit only necessary request data to the chosen providers. Redact secrets and precise location from diagnostics by default. Pairing, session authentication, request-origin checks and rate limits apply even on a home LAN. No analytics or remote-access tunnel by default.

Freshness defaults are proposed: weather refresh every 10 minutes and stale after 30 minutes; transport cadence follows the actual account quota, with stale status after two missed expected refresh intervals. Expired departures must not keep counting down as if live. Store fetch time and provider observation/prediction time separately.

## Enclosure and electronics

Custom printed bezel, rear electronics housing, stable stand, ventilation, service access and cable strain relief. Electronics stay at low voltage inside; one external low-voltage adapter powers the integrated display board. Exact split-print design depends on printer build volume, which is still unknown. No final CAD dimensions until panel, connector orientation and board mounting are measured.

A complete BOM must state quantities, manufacturer links, included accessories, cable ends, power needs and unresolved compatibility checks. Touchscreen and hardware brightness control are required implementation features supporting phone-free setup and reliable dimming.

## Release sequence

1. Validate panel/touch/backlight, P4/C6 networking, firmware memory budgets and API account access on the bench.
2. Prove signed A/B OTA and rollback on the actual hardware before investing in final enclosure work.
3. Build clock, settings, Wi-Fi onboarding and QR pairing.
4. Add weather and Île-de-France transport adapters with stale/offline states.
5. Finalise enclosure from measured components; complete thermal, readability, transport and OTA qualification.

## Deferred scope and unresolved decisions

Physical weather sensors, native iOS application, infrared remote, battery backup, cloud control, commercial fleet management, other cities and third-party downloadable plugins are deferred. No requirement for a custom PCB in v1; bought boards and a custom harness fulfil the from-scratch assembly approach.

Resolve during engineering: exact panel/backlight behaviour, printer volume, exact board power requirements and flash partition budget, actual PRIM account quota and coverage for the chosen home, public release hosting and signing-key custody, and final local HTTPS provisioning. No private address or provider key should be committed to project documents.

## Decision history

On 2 November 2026, the user selected the smaller 10-inch-class ESP32-P4 display after comparing cost and readability. The integrated 10.1-inch kit replaces the Pi/14-inch architecture. All three views, iPhone control, extensibility and signed OTA with rollback remain in scope.

## Supporting documents

- [Hardware and software design](SYSTEM_DESIGN.md)
- [Electronics and assembly BOM](BOM.md)
- [Source register and validation notes](SOURCES.md)

## UI refinements agreed 12 November 2026

- Wi-Fi setup uses a clean network/password form plus a separate status card. Connect immediately displays a spinner and connecting state, prevents duplicate attempts, and shows success/failure. Scans and connection work cannot block navigation.
- A persistent Wi-Fi indicator on Clock, Weather and Transport distinguishes connected, connecting and offline. Connected means the station obtained an IP address; it is not an internet-reachability guarantee.
- Profile setup saves a name or household names and displays “Welcome <name>”. No cloud account is required.
- Custom RGB colour controls and Digital/Modern/Slim clock styles complement the three presets. The Modern font must be rendered at native size rather than enlarged from a small bitmap.
- Manual time setup and time-source/UTC labels on the clock face are removed. Existing devices previously set to manual mode migrate to automatic synchronisation.
- The reported intermittent solid-blue screen remains a release-blocking hardware defect until reproduced and verified resolved. It is not an OTA update screen; no OTA download path currently exists.
- OTA delivery, recovery qualification and phone control remain mandatory product requirements, not completed features.


## Weather increment (firmware 0.3.0)

Firmware 0.4.0 presents twelve upcoming hourly temperature/precipitation-probability points on the weather page, six on the clock summary, and three daily forecasts. Forecasts use temperature curves, precipitation bars, condition icons and comparable daily low/high range bars. The clock preserves large time/date and adds a weather summary (location, freshness, condition, temperature, feels-like, humidity, today's high/low and precipitation chance). A separate transport summary area is reserved and explicitly unconfigured until transport integration. Provider attribution and model/fetch timestamps appear on the weather page. Null measurements are unavailable, not zero; offline/stale cached results are labelled. On-device confirmation is pending; this increment does not mark transit, phone geolocation, phone control or OTA complete.


### Phone location and mobility update (0.5.1)

Use QR-based local browser control on the same Wi-Fi, including an iPhone Personal Hotspot. No paid hosting or domain is required for onboarding. Provide an optional, prepared Apple Shortcut for user-triggered phone positioning: Add shortcut once → Use my iPhone location → check map → Confirm location. Customers must never build or edit workflows, enter device IP addresses or configure a cloud service. Address search/map adjustment is a complete alternative for iPhone and Android. Location permission is requested explicitly by iOS; this is not background tracking.

Persist exact home coordinates and a locality name independently of the display timezone. Never change home location solely because Wi-Fi changes. Provide Change location through the phone panel, invalidate old weather immediately after saving, and use the same coordinates for future nearby transport. Keep the last known timezone while resolving a new one.

Implementation status: local panel and shortcut callback handling implemented; the signed generic shortcut is now supplied and its source/output/download verified locally. Clean-iPhone installation, callback/tab behaviour, hotspot reachability, complete firmware build/flash and device acceptance remain outstanding. Transit and signed OTA with rollback remain mandatory and pending. Before sale, validate the entire first-use journey without developer assistance, recovery after a network change, and commercial suitability of all data providers.

### Transport refinement: hub priority, pickup times and maps

- Within the configured 2 km radius, prioritise RER, then métro, then bus/tram at equal priority. Within each category, sort by distance. Discovery must collect rail hubs independently of the nearest bus-platform limit; sorting an already truncated eight-platform list is insufficient.
- Show the exact boarding stop/station, direction/destination and platform where provided. Show an explicit predicted arrival clock time plus countdown, followed by the next service. Distinguish predicted arrival, predicted departure and scheduled-only data; never label departure-only data as arrival. Include observation age and stale/unavailable states. The existing `5 / 18 min` display represents two departures, not service frequency, but must be replaced by clearer wording.
- Provide station locations on a map with the saved home location, mode/line identification and selection of a station to view its services. Station maps do not require moving-vehicle coordinates.
- Uber-style moving vehicles are conditional on a verified, timestamped vehicle-position feed and its coverage/access terms. Stop-level arrival predictions alone do not establish live vehicle coordinates. Do not animate inferred positions as measured live locations.
- Firmware 0.7.0 implements mode-prioritised paginated discovery, explicit station-area mapping, a geographic station map and the refined arrival/departure presentation, pending live rail acceptance. Street-map tiles and live vehicle coordinates are not implemented. Current five-minute polling must be reviewed against available quotas before promising continuously refreshed arrival estimates across multiple hubs.
