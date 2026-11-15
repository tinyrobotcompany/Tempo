"""Hole-size coupon: print it first, then confirm M3_INSERT_HOLE and M25_CLEARANCE_HOLE.

Front row: M3 heat-set insert holes. Back row: M2.5 screw clearance holes.
Smallest holes at the chamfered corner.
"""

from build123d import Axis, Cylinder, Pos, chamfer

from tempo_case import parameters as p
from tempo_case.geometry import AS_MODELLED, CUT_OVERSHOOT, PrintedPart, box

ZONE = "shell"


def build_coupon() -> PrintedPart:
    pitch, thickness = p.COUPON_PITCH.v, p.COUPON_THICKNESS.v
    count = max(len(p.COUPON_INSERT_HOLES), len(p.COUPON_M25_HOLES))
    coupon = box((0, 0, 0), (pitch * (count + 1), 3 * pitch, thickness))
    for row, holes in ((1, p.COUPON_INSERT_HOLES), (2, p.COUPON_M25_HOLES)):
        for i, diameter in enumerate(holes, start=1):
            coupon -= Pos(i * pitch, row * pitch, thickness / 2) * Cylinder(diameter / 2, thickness + 2 * CUT_OVERSHOOT)
    marker = min(coupon.edges().filter_by(Axis.Z), key=lambda e: (e.center().X, e.center().Y))
    coupon = chamfer(marker, pitch / 2)
    return PrintedPart("calibration_coupon", ZONE, coupon, AS_MODELLED, "Flat; no supports.")
