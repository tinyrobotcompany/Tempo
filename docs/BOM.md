# Home display electronics and assembly BOM

2 November 2026 • Revised for the selected standalone 10.1-inch ESP32-P4 display

Quantities are for one device. The working kit is Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1; the user selected this display class, but an exact supplier revision/bundle has not yet been confirmed. This replaces the previous Pi and 14-inch HDMI BOM. See the [design](SYSTEM_DESIGN.md) and [PRD](PRD.md).

## Core electronics

| Item | Qty | Selection and source | Purpose and procurement check |
| --- | --- | --- | --- |
| Integrated display and processor kit | 1 | [Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm), select 10.1-inch variant | P4 runs firmware/UI; C6 supplies Wi-Fi. Verify exact revision, flash/PSRAM, antenna and all display/touch interconnects are included. |
| External power adapter | 1 | Manufacturer-specified supply for exact kit, EU mains plug | Confirm input voltage, current and connector from supplied revision documentation before ordering. One adapter powers complete board/display; reuse bundled adapter if appropriate. Exact PSU SKU pending. |
| Power/programming cable | 1 | Host-compatible USB data cable to board's USB-to-UART Type-C port | Required for initial flashing and wired recovery; check kit contents and current rating. A power-only lead cannot flash firmware. |
| Controller/display mounting parts | 1 set | Kit-supplied standoffs and screws where included | Complete only missing parts after checking board and panel drawings. |
| C6 service adapter | Conditional, 1 | Compatible USB-to-UART adapter and service leads | Only if onboard access cannot recover C6; verify schematic, voltage levels and header before purchase. Keep as bench equipment, not installed appliance hardware. |

Manufacturer [board documentation](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X) identifies the integrated components, USB flashing port and separate C6 flashing pads. No Raspberry Pi, Pi cooler, M.2 HAT, SSD, HDMI cable or external USB touch cable is required. Onboard flash holds firmware/settings and PSRAM supports runtime graphics. A microSD card is optional development storage, not a v1 requirement or the OTA recovery mechanism.

## Enclosure and harness parts

| Item | Qty | Specification |
| --- | --- | --- |
| Printed front bezel, rear housing and stand | 1 set | Parametric design after panel measurement; PETG initial material; split parts according to printer build volume. |
| Filament | 1 spool | Allow a 1 kg spool for housing and prototype iterations; consumption depends on final geometry. |
| Threaded inserts and screws | 1 assortment | M3 for printed housing; board fasteners follow actual mounting holes, typically separate smaller sizes. Final lengths after stack measurements. |
| Insulating board standoffs | 1 set | Match integrated controller/display mounting holes; use supplied kit hardware where applicable. |
| Rubber feet | 4 | Non-slip, sized for stand; check stability under touchscreen presses. |
| Cable strain reliefs and grommets | 1 set | Match one external low-voltage feed; no raw mains wiring inside the print. |
| Cable ties and adhesive mounts | 1 set | Keep leads away from antenna and panel edges. |
| Panel padding | 1 set | Thin non-conductive compliant pads; do not clamp active glass or cover ventilation. |

No weather sensors, weather mast, GPS, infrared receiver, remote handset, battery, UPS, speakers or custom PCB are required for v1. Touchscreen provides local setup/navigation; the iPhone is the primary remote. Automatic scheduled dimming needs no ambient-light sensor. Ambient-responsive dimming can be added later if wanted.

## Procurement and assembly checks

Confirm the exact 10.1-inch kit, board/chip revision, supported firmware package, actual flash capacity, power rating, included antenna/cables, mounting drawing and backlight interface. Select the kit appropriate for our own printed casing; do not assume an enclosed variant has identical dimensions. No price or stock guarantee is recorded.

Test the board's backlight driver for brightness range, dim/off and touch wake. DDC/CI is not used for this integrated panel. Confirm 1280 × 800 landscape rendering and matching touch rotation before freezing the bezel.

Assembly: external supply → board's documented power input; supplied board/display/touch interconnects → panel; service USB cable → development computer only when flashing/debugging. Power down before connecting ribbons and mount on insulating standoffs. Preserve access to BOOT/RESET and C6 service pads.

Bench qualification: cold boot, touch, brightness, Wi-Fi, peak memory, flash slot sizes and power draw. Then test signed OTA, rollback and power cuts, followed by 72-hour enclosed runtime and thermal checks. Exact printed dimensions follow measurements. The existing Pi 4 is optional development equipment and adds no required cost to this build.
