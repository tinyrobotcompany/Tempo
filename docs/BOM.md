# Tempo — electronics and assembly BOM

Updated 15 November 2026 • Current standalone 10.1-inch ESP32-P4 prototype

Quantities are for one device. The Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1 is already owned and running Tempo. Do not purchase another display or a Raspberry Pi. The exact commercial bundle/enclosure SKU and power-adapter label have not been recorded. This BOM replaces the Pi and 14-inch HDMI design. See the [design](SYSTEM_DESIGN.md) and [PRD](PRD.md).

## Verified prototype configuration

- ESP32-P4 revision v3.2; 32 MB external flash and 32 MB PSRAM, confirmed by device diagnostics.
- Integrated ESP32-C6 Wi-Fi coprocessor over SDIO; no separate Wi-Fi module required.
- 10.1-inch touch display: 800 × 1280 native configuration, rotated to 1280 × 800 landscape in firmware; JD9365 display controller and GT911-family touch driver detected in boot logs.
- Weather and Île-de-France transport use Internet APIs and the saved precise location. No outdoor sensor station is installed.
- Location comes from iPhone setup/address-map selection; no GPS receiver is installed. The timezone city is not a substitute for precise transport coordinates.
- Local touch controls and the local-network iPhone browser panel provide control. Home Wi-Fi or an iPhone hotspot supplies connectivity.
- Network time synchronisation and configured timezone rules handle the clock and daylight saving. No battery-backed RTC is specified.
- Two 8 MiB OTA app slots are already allocated. **OTA download/update delivery is not implemented yet**; the current build uses wired flashing. The slots alone do not constitute working OTA.

Firmware configuration and partition layout are in `firmware/sdkconfig.defaults` and `firmware/partitions.csv`. Hardware values above are from the prototype logs; a replacement unit must be checked for compatibility.

## Core electronics

| Item | Qty | Selection and source | Purpose and procurement check |
| --- | --- | --- | --- |
| Integrated display and processor kit | 1 | [Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm), select 10.1-inch variant | Already owned and operational. P4 runs firmware/UI; C6 supplies Wi-Fi. Retain supplied antenna and display/touch interconnects. Manufacturer lists 33672 for the 10.1-inch kit and 33150 for the EN variant; confirm the received bundle before ordering replacements. |
| External power adapter | 1 | Manufacturer-specified supply for exact kit, EU mains plug | Confirm input voltage, current and connector from supplied revision documentation before ordering. One adapter powers complete board/display; reuse bundled adapter if appropriate. Exact PSU SKU pending. |
| Power/programming cable | 1 | Host-compatible USB data cable to board's USB-to-UART Type-C port | Already used successfully for flashing/debugging. Retain the working USB data cable; it is currently required for firmware updates and recovery. A power-only lead cannot flash firmware. |
| Controller/display mounting parts | 1 set | Kit-supplied standoffs and screws where included | Complete only missing parts after checking board and panel drawings. |
| C6 service adapter | Conditional, 1 | Compatible USB-to-UART adapter and service leads | Only if onboard access cannot recover C6; verify schematic, voltage levels and header before purchase. Keep as bench equipment, not installed appliance hardware. |

Manufacturer [board documentation](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X) identifies the integrated components, USB flashing port and separate C6 flashing pads. No Raspberry Pi, Pi cooler, M.2 HAT, SSD, HDMI cable or external USB touch cable is required. Onboard flash holds firmware/settings and PSRAM supports runtime graphics. A microSD card is optional development storage, not a v1 requirement or the OTA recovery mechanism.

## Enclosure and harness parts

| Item | Qty | Specification |
| --- | --- | --- |
| Printed front bezel, rear housing and stand | 1 set | Parametric design after panel measurement; PETG initial material; split parts according to printer build volume. |
| Filament | 1 spool | Allow a 1 kg spool for housing and prototype iterations; consumption depends on final geometry. |
| Threaded inserts and screws | 1 assortment | M3 for printed housing; do not use M3 screws in the display mounts. Manufacturer specifies M2.5 rear mounts with 4 mm depth. Screw length must account for bracket thickness without exceeding that depth; check the actual received assembly. |
| Insulating board standoffs | 1 set | Match integrated controller/display mounting holes; use supplied kit hardware where applicable. |
| Rubber feet | 4 | Non-slip, sized for stand; check stability under touchscreen presses. |
| Cable strain reliefs and grommets | 1 set | Match one external low-voltage feed; no raw mains wiring inside the print. |
| Cable ties and adhesive mounts | 1 set | Keep leads away from antenna and panel edges. |
| Panel padding | 1 set | Thin non-conductive compliant pads; do not clamp active glass or cover ventilation. |

No weather sensors, weather mast, GPS, infrared receiver, remote handset, battery, UPS, speakers or custom PCB are required for v1. Touchscreen provides local setup/navigation; the iPhone is the primary remote. Automatic scheduled dimming needs no ambient-light sensor. Ambient-responsive dimming can be added later if wanted.

## Procurement and assembly checks

The display, chip revision, memory capacity, touch and backlight operation are established. Before enclosure procurement, record the received bundle label, supply voltage/current label, connector clearances and measured mounting geometry. Select the kit appropriate for our own printed casing; do not assume an enclosed variant has identical dimensions. No price or stock guarantee is recorded.

Test the board's backlight driver for brightness range, dim/off and touch wake. DDC/CI is not used for this integrated panel. Confirm 1280 × 800 landscape rendering and matching touch rotation before freezing the bezel.

Assembly: external supply → board's documented power input; supplied board/display/touch interconnects → panel; service USB cable → development computer only when flashing/debugging. Power down before connecting ribbons and mount on insulating standoffs. Preserve access to BOOT/RESET and C6 service pads.

Bench qualification: cold boot, touch, brightness, Wi-Fi, peak memory, flash slot sizes and power draw. Once OTA delivery and signing are implemented, qualify signature verification, rollback and interrupted updates; these are outstanding acceptance tests, not completed capabilities. Perform a 72-hour enclosed runtime and thermal test before selling the product. Exact printed dimensions follow measurements. The existing Pi 4 is optional development equipment and adds no required cost to this build.

## Remaining purchases and decisions

- Enclosure, bezel and stand: not yet dimensioned or printed; use the actual assembly to finalise CAD.
- PETG, inserts, correctly sized M2.5 display fasteners, housing screws, feet, padding and strain relief: buy only quantities required by the final CAD; avoid duplicating kit-supplied mounting parts.
- Dedicated mains adapter: reuse the supplied compatible adapter if present. The exact adapter rating/SKU remains unverified; do not guess from USB connector shape. Record the device/supply label before purchasing a replacement.
- No Pi, HDMI hardware, external GPS, weather sensors, microSD, battery/UPS or remote handset is required for the current configuration.
- Internet access and API credentials are service prerequisites, not electronics. The PRIM token serves transport; `OPENAI_API_KEY` is development/CI tooling only and does not belong in the appliance firmware BOM.

Manufacturer reference: [Waveshare board, SKU and mounting documentation](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X), checked 15 November 2026. No enclosure dimensions, PSU specification or supplier prices are invented here.
