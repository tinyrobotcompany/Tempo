# Measurements to take before printing

Values currently come from Waveshare's drawing (`waveshare-10.1-dimensions.png`,
docs.waveshare.com, checked 2026-11-15). Measure the received kit with
callipers, update `cad/tempo_case/parameters.py`, change each value's status to
`MEASURED`, and rebuild.

| Parameter | Drawing value | Measure |
|---|---|---|
| `GLASS_W` × `GLASS_H` | 250.2 × 169.0 mm | Cover glass outline |
| `GLASS_T`, `PANEL_DEPTH` | 3.2, 12.0 mm | Glass thickness; glass front to back-plate rear face |
| `BACK_W` × `BACK_H` | 245.2 × 164.0 mm | Metal back outline |
| `MOUNT_PITCH_X` × `MOUNT_PITCH_Z` | 236.2 × 155.0 mm | Corner M2.5 hole centres; confirm thread depth (4 mm) |
| `PORT_ZONE_TOP`, `PORT_ZONE_BOTTOM` | ~45, ~135 mm (estimate) | Glass top edge to the top of the highest and the bottom of the lowest opening on the right-hand edge (USB-C, buttons, card slot) |
| `PANEL_MASS` | 0.75 kg (estimate) | Weigh the kit with antenna attached |
| `REAR_KEEPOUT_*` | 200 × 120 × 10 mm (estimate) | Anything standing proud of the back (header plugs, antenna lead) |

Also check:

- Which edge the ports are on. The model assumes the right-hand edge seen from the front.
- Where the supplied Wi-Fi antenna will sit inside the shell. Keep it away from the screws.
- The power input: confirm the board is powered over USB-C through the port window, not a separate connector.

## Print the coupon first

`print_jobs/1_calibration-coupon_revA1.3mf`. Front row: M3 heat-set insert
holes 3.9 → 4.2 mm. Back row: M2.5 clearance holes 2.7 → 3.0 mm. The smallest
holes are at the chamfered corner. Set `M3_INSERT_HOLE` and `M25_CLEARANCE_HOLE`
from the results.
