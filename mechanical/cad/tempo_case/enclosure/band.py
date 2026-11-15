"""The coloured trim band round the glass, printed as left and right halves.

Cross-section, front to back: a thin lip just ahead of the glass (the coloured
outline seen from the front), a wall round the glass and metal back, and a
rear flange. The flange is clamped between the panel's back and the shell by
the four M2.5 corner screws, so the band needs no fasteners of its own.

The halves print rear face (flange) down: the wall rises from the flange and
the lip is a short ledge at the top, so no supports are needed.
"""

from build123d import Part

from tempo_case import layout
from tempo_case import parameters as p
from tempo_case.geometry import CUT_OVERSHOOT, REAR_FACE_DOWN, PrintedPart, box_between, rounded_prism, y_cylinder
from tempo_case.panel import port_zone_z

ZONE = "band"


def build_full_band() -> Part:
    zc = layout.panel_centre_z()
    width, height, radius = layout.band_outer_size()
    front, rear = layout.band_front_y(), layout.flange_rear_y()
    band = rounded_prism(width, height, radius, front, rear - front, zc)

    grow = p.BAND_CLEARANCE.v
    band -= rounded_prism(p.GLASS_W.v + 2 * grow, p.GLASS_H.v + 2 * grow, p.GLASS_R.v + grow, -p.LIP_GAP.v, p.PANEL_DEPTH.v + p.LIP_GAP.v, zc)

    lip = p.LIP_OVERLAP.v
    band -= rounded_prism(p.GLASS_W.v - 2 * lip, p.GLASS_H.v - 2 * lip, p.GLASS_R.v - lip, front - CUT_OVERSHOOT, p.LIP_THICKNESS.v + 2 * CUT_OVERSHOOT, zc)

    inset = layout.flange_inset()
    band -= rounded_prism(p.BACK_W.v - 2 * inset, p.BACK_H.v - 2 * inset, max(p.BACK_R.v - inset, 1.0), p.PANEL_DEPTH.v - CUT_OVERSHOOT, p.FLANGE_THICKNESS.v + 2 * CUT_OVERSHOOT, zc)

    for x, z in layout.mount_points():
        band -= y_cylinder(x, p.PANEL_DEPTH.v - CUT_OVERSHOOT, rear + CUT_OVERSHOOT, z, p.M25_CLEARANCE_HOLE.v)

    # Window in the right-hand wall for the USB-C plugs and buttons on the panel's edge.
    low, high = port_zone_z()
    margin = p.PORT_WINDOW_MARGIN.v
    band -= box_between(
        (p.BACK_W.v / 2, p.GLASS_T.v - margin / 3, low - margin),
        (width / 2 + CUT_OVERSHOOT, p.PANEL_DEPTH.v, high + margin),
    )
    return band


def build_halves() -> list[PrintedPart]:
    band = build_full_band()
    width, height, _ = layout.band_outer_size()
    big = width + height
    left = band & box_between((-big, -big, -big), (0, big, big))
    right = band & box_between((0, -big, -big), (big, big, big))
    note = "Rear (flange) face down; the lip is a short ledge at the top. No supports."
    return [
        PrintedPart("band_left", ZONE, left, REAR_FACE_DOWN, note),
        PrintedPart("band_right", ZONE, right, REAR_FACE_DOWN, note),
    ]
