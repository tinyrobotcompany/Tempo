# External power and display sleep

Decision: 15 November 2026. Tempo uses an external mains-to-low-voltage adapter exclusively. No internal battery, RTC backup cell, UPS, battery charging feature, or battery capacity indicator is part of the product. Mains circuitry remains outside the printed enclosure.

## Buy and connect

Recommended prototype supply: **Official Raspberry Pi 15W USB-C supply, 5.1 V / 3 A, black EU version**, [buy from Letmeknow (France)](https://letmeknow.fr/fr/alimentations/3790-alimentation-officielle-raspberry-pi4-type-c-153w-noir-0644824914886.html). Select the EU variant for France. The manufacturer specifies 5.1 V / 3 A (15.3 W by multiplication) and a captive 1.5 m USB-C cable; this is not a measurement of Tempo consumption. The retailer incorrectly lists 3.15 A; use the [manufacturer specification](https://www.raspberrypi.com/products/type-c-power-supply/?variant=raspberry-pi-psu-eu-b). The live supplier page checked on 15 November 2026 listed 4 units in stock at €9.90 TTC, and its product image shows two round EU pins suitable for French sockets. Stock and prices can change. This replaces the unavailable RobotShop recommendation with its ambiguous plug photograph. Its Raspberry Pi product name does not require a Pi in Tempo.

Connect to the board's **USB TO UART Type-C power/programming port**, the port already verified for flashing. Leave the physical power switch on for automatic restart after an interruption. Do not inject higher voltages, use a PD voltage-trigger cable, or assume the USB OTG port is the intended power input. Disconnect the adapter when using this same port for development USB; do not add an unverified dual-source harness. Leave battery headers unconnected.

Evidence: [Waveshare hardware description](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X) identifies the power/programming port; the [published schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-X/ESP32-P4-WIFI6-Touch-LCD-X-Schematic.pdf) identifies its USB0_5V supply path. Adapter selection is an engineering recommendation based on these specifications, not a completed qualification of the received assembly. Before freezing the sale BOM, record the actual board revision and test cold boot, full backlight, Wi-Fi requests, flash writes, voltage drop, and a 72-hour enclosed run with this adapter. No measured current or certification of the finished product is claimed.

## Display & sleep

Settings → Display & sleep and the phone appearance form provide independent inactivity timers:

- Dim after: Never, 1, 2, 5, 10 minutes, or custom 1–1440 minutes.
- Screen off after: the same choices. When both are enabled, screen-off must be later than dim.
- Dim level: 1–100% relative to normal brightness, using a quadratic perceptual approximation. The PWM request is `max(1, normal × level² / 10000)`. With normal brightness 65 and dim level 10, the driver receives 1% PWM (previously 10%). This is not a calibrated measurement of luminance; integer PWM resolution limits distinctions at the lowest settings.
- Preview dim/off: apply the unsaved level for five seconds, then recover automatically. Touch also ends a preview. Previews never persist settings.

Timers start on boot and restart on touch, applying settings, or a phone view command. They use elapsed monotonic time, independent of time synchronisation, time zones, internet access or delayed rendering. Never is the default for new installations. No schedule is required. Status polling and automatic view rotation do not reset inactivity.

Touch restores normal brightness. The first complete gesture on an off screen is consumed; lift and touch again to interact. Backlight failures retry, and uncertain wake gestures are also consumed. Only the backlight is switched off: touch, clock, Wi-Fi, phone controls and data workers stay active. This is not ESP deep sleep.

The display shows a prominent saving banner, disables duplicate saves while pending, and confirms SAVED only after the NVS worker reports a successful commit. A failed save is clearly reported. The phone likewise waits for persisted settings confirmation.

## Persistence and migration

Schema v4 adds idle timers while preserving appearance and the selected dim level. Existing v3 Dim migrates to dim after one minute; Screen off migrates to off after one minute; disabled remains Never. Former schedule windows no longer apply. Versions 1 and 2 default to Never. A v3 rollback record retains appearance but disables scheduled sleep, since older firmware cannot represent the new timers. Version 2 and 1 compatibility records are also retained.

Old schedule-based phone requests are rejected; reload the phone page to use the new controls. Clients omitting all power fields preserve saved idle settings. Partial power fields and invalid/custom fractional delays are rejected transactionally.

## Power loss and network loss

After external power returns, boot loads committed settings, remembered Wi-Fi credentials and location; reconnects; synchronises time; and refreshes weather/transport automatically. No clock can measure elapsed unpowered time without backup hardware: cold boot must remain visibly unsynchronised until time is obtained. There is no last-second save guarantee; wait for the UI's Saved confirmation before unplugging.

During a network outage, already fetched data remains in RAM. Weather shows cached status and age; transport lists retain service identity and data age but suppress countdowns/arrival predictions while disconnected or stale. A connected Wi-Fi icon means a LAN address, not proof that providers are reachable; failed fetches retain original timestamps and age out. Cold boot has no persisted forecast/departure cache and displays unavailable/loading data until fetched. Reconnection triggers refresh without user action.

## OTA contract — signed application delivery

External-only power makes interrupted-update handling mandatory. The OTA worker downloads into the inactive slot, verify signature, complete image integrity and hardware compatibility before switching the boot target, retain the previous bootable image, trial-boot with local health checks, and roll back a failed trial. Internet/provider availability must not determine whether otherwise healthy firmware is accepted. Never overwrite the active image or mark an incomplete download bootable.

Signed manifest verification, inactive-slot writes and UI boot-health checks implement the application OTA path. See [OTA.md](OTA.md). Physical interruption qualification is still required. Settings migration must remain readable after rollback. Release qualification must cut power during download, flash writes, activation, trial boot and settings commits, and prove recovery to a valid old/new image. No battery or shutdown grace period may be assumed. See PRD OTA-01–08 and RELEASING.md.

## Test a feature branch without merging

Build and flash directly from the feature worktree. Merging and flashing are independent. Stop any automatic main-branch device watcher first so it cannot overwrite the trial image. Use the existing connected USB port and retain the production SDK configuration and private environment locally (never commit secrets). Run host tests, then `./scripts/idf.sh build` in the worktree. Verify the generated partition table matches the installed layout before using `./scripts/idf.sh -p PORT app-flash`; do not erase flash or flash NVS. Record the branch commit and firmware ELF hash alongside the boot log.

To return to stable firmware, build and app-flash main through the same process. For a changed partition layout, investigate before flashing instead of treating an app-only upload as safe. Keep the watcher stopped during manual qualification.

| Test | Action | Expected |
| --- | --- | --- |
| Immediate dim | Normal brightness 65, dim level 10, Preview dim | Visible reduction for five seconds, then normal; log requests 1% PWM. |
| Immediate off | Preview off, then wait; repeat and touch | Off for five seconds then automatic recovery; touch wakes without clicking through. |
| Idle dim and off | Dim 1 minute, off 2 minutes; save and stop touching | Prominent SAVED; dim at one minute, off at two. |
| Wake | Touch while off, lift, touch again | First gesture wakes only; second operates controls; timers restart. |
| Never | Set both timers to Never | Display stays at normal brightness. |
| Custom | Set dim 3, off 4 | Dim/off follow those delays. Off less than or equal to dim is rejected. |
| Offline / no clock | Repeat idle tests without network/time synchronisation | Identical power behaviour; no schedule dependency. |
| Phone | Change a setting or view while off | Display wakes; passive status polling does not keep it awake. |
| Persistence | Save distinctive timers, wait for SAVED, power-cycle | Settings retained; startup awake, then timers run. |
| Failure | Host seam simulates failed save/backlight writes | No false save success; backlight retries; uncertain first gestures consumed. |

Host tests cover timer boundaries, migration, validation, persistence, delayed render callbacks, previews, wake handling and offline operation. Physical perceived brightness and power interruption behaviour still require device qualification; PWM duty alone cannot prove luminance.
