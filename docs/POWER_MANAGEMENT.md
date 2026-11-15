# External power and night mode

Decision: 15 November 2026. Tempo uses an external mains-to-low-voltage adapter exclusively. No internal battery, RTC backup cell, UPS, battery charging feature, or battery capacity indicator is part of the product. Mains circuitry remains outside the printed enclosure.

## Buy and connect

Recommended prototype supply: **Official Raspberry Pi 15W USB-C supply, 5.1 V / 3 A, black EU version**, [buy from Letmeknow (France)](https://letmeknow.fr/fr/alimentations/3790-alimentation-officielle-raspberry-pi4-type-c-153w-noir-0644824914886.html). Select the EU variant for France. The manufacturer specifies 5.1 V / 3 A (15.3 W by multiplication) and a captive 1.5 m USB-C cable; this is not a measurement of Tempo consumption. The retailer incorrectly lists 3.15 A; use the [manufacturer specification](https://www.raspberrypi.com/products/type-c-power-supply/?variant=raspberry-pi-psu-eu-b). The live supplier page checked on 15 November 2026 listed 4 units in stock at €9.90 TTC, and its product image shows two round EU pins suitable for French sockets. Stock and prices can change. This replaces the unavailable RobotShop recommendation with its ambiguous plug photograph. Its Raspberry Pi product name does not require a Pi in Tempo.

Connect to the board's **USB TO UART Type-C power/programming port**, the port already verified for flashing. Leave the physical power switch on for automatic restart after an interruption. Do not inject higher voltages, use a PD voltage-trigger cable, or assume the USB OTG port is the intended power input. Disconnect the adapter when using this same port for development USB; do not add an unverified dual-source harness. Leave battery headers unconnected.

Evidence: [Waveshare hardware description](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X) identifies the power/programming port; the [published schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-X/ESP32-P4-WIFI6-Touch-LCD-X-Schematic.pdf) identifies its USB0_5V supply path. Adapter selection is an engineering recommendation based on these specifications, not a completed qualification of the received assembly. Before freezing the sale BOM, record the actual board revision and test cold boot, full backlight, Wi-Fi requests, flash writes, voltage drop, and a 72-hour enclosed run with this adapter. No measured current or certification of the finished product is claimed.

## User behaviour

Settings → Power & night mode, or the iPhone appearance form, provides:

- Off (default): use normal brightness all day.
- Dim: use the selected 1–100% night brightness, capped at normal brightness.
- Screen off: set the backlight to zero during the schedule.
- Configurable start/end in local time; initial values 23:00–07:00. Start is inclusive, end exclusive. Equal start/end is rejected for an enabled schedule. Intervals may cross midnight or occur within the same day.

The saved location's time-zone rules determine local time and daylight-saving transitions. With no trusted clock after cold boot, normal brightness remains available for setup until time synchronises. Editing/saving settings or choosing a view on the phone wakes the display for 60 seconds. Physical touch also restores normal brightness for 60 seconds from the last touch. The entire first gesture on a dark screen is consumed so it cannot activate a hidden control; lift and touch again to interact.

Only the backlight is switched off. Touch scanning, the UI heartbeat, system clock, Wi-Fi, phone server and data workers continue. This is not ESP deep sleep, and no deep-sleep power figures apply. Dimming/screen-off does not write settings on every timer tick. Backlight changes and clock-trust state are logged under `power`.

## Power loss and network loss

After external power returns, boot loads committed settings, remembered Wi-Fi credentials and location; reconnects; synchronises time; and refreshes weather/transport automatically. No clock can measure elapsed unpowered time without backup hardware: cold boot must remain visibly unsynchronised until time is obtained. There is no last-second save guarantee; wait for the UI's Saved confirmation before unplugging.

Settings schema v3 stores the night schedule separately from the compatible v2/v1 records retained for older firmware. Older installations migrate with night mode disabled. Successful save means the NVS commit completed; failures are shown. Power-cut behaviour still requires physical qualification.

During a network outage, already fetched data remains in RAM. Weather shows cached status and age; transport lists retain service identity and data age but suppress countdowns/arrival predictions while disconnected or stale. A connected Wi-Fi icon means a LAN address, not proof that providers are reachable; failed fetches retain original timestamps and age out. Cold boot has no persisted forecast/departure cache and displays unavailable/loading data until fetched. Reconnection triggers refresh without user action.

## OTA contract — delivery remains unimplemented

External-only power makes interrupted-update handling mandatory. Future OTA must download into the inactive slot, verify signature, complete image integrity and hardware compatibility before switching the boot target, retain the previous bootable image, trial-boot with local health checks, and roll back a failed trial. Internet/provider availability must not determine whether otherwise healthy firmware is accepted. Never overwrite the active image or mark an incomplete download bootable.

Existing A/B partitions and UI boot-health checks are foundations, not a completed signed OTA system. Settings migration must remain readable after rollback. Release qualification must cut power during download, flash writes, activation, trial boot and settings commits, and prove recovery to a valid old/new image. No battery or shutdown grace period may be assumed. See PRD OTA-01–08 and RELEASING.md.

## Acceptance checks

Host tests cover policy boundaries, overnight/daytime intervals, invalid/equal times, unset clock fallback, wake timeout, migration, phone validation, UI heartbeat while dark, wake-gesture suppression and offline departure rendering. Existing service tests cover saved networks, retry/reconnection and retained cache timestamps.

Hardware acceptance still required: real panel black at 0%, first-touch wake without click-through, schedule across local DST changes, browser control while dark, interrupted saved-settings writes, automatic cold boot with router unavailable then restored, full-brightness load and enclosed thermal soak. Firmware build/host tests cannot establish electrical or touch-panel acceptance.
