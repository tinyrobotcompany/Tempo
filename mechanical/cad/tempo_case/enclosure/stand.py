"""The coloured stand: a wide hollow wedge screwed to the shell back.

Side profile (case coordinates, Y rearward, Z up): a quadrilateral from the
shell back down to a foot lying flat on the desk, hollowed to STAND_MEMBER-thick
walls. Its front wall takes four M3 screws into inserts in the shell; their
heads are reached through the hollow. Two seats under the foot take stick-on
rubber feet.

Prints on its side (left end down): every wall is vertical on the bed.
"""

from build123d import Cylinder, Kind, Part, Polygon, Pos, extrude, offset

from tempo_case import layout
from tempo_case import parameters as p
from tempo_case.geometry import CUT_OVERSHOOT, SIDE_DOWN, PrintedPart, side_plane, y_cylinder

ZONE = "stand"


def profile_points() -> list[tuple[float, float]]:
    """(y, z) corners: top of the attachment, rear of the foot, front of the foot, bottom of the attachment."""
    zc, back = layout.panel_centre_z(), layout.shell_back_y()
    half = p.STAND_ATTACH_HEIGHT.v / 2
    rear = layout.desk_point(p.STAND_REACH.v)
    front = layout.desk_point(p.STAND_REACH.v - p.STAND_FOOT_LENGTH.v)
    return [(back, zc + half), rear, front, (back, zc - half)]


def _prism(points: list[tuple[float, float]], inset: float = 0.0, extra: float = 0.0) -> Part:
    width = p.STAND_WIDTH.v + 2 * extra
    sketch = side_plane(-width / 2) * Polygon(*points, align=None)
    if inset:
        sketch = offset(sketch, -inset, kind=Kind.INTERSECTION)
    return extrude(sketch, amount=width, dir=(1, 0, 0))


def build_stand() -> PrintedPart:
    points = profile_points()
    stand = _prism(points) - _prism(points, inset=p.STAND_MEMBER.v, extra=CUT_OVERSHOOT)

    back = layout.shell_back_y()
    for x, z in layout.stand_screw_points():
        stand -= y_cylinder(x, back - CUT_OVERSHOOT, back + p.STAND_MEMBER.v + CUT_OVERSHOOT, z, p.M3_CLEARANCE_HOLE.v)

    # Rubber-foot seats, cut square to the desk.
    desk = layout.desk_location()
    middle = p.STAND_REACH.v - p.STAND_FOOT_LENGTH.v / 2
    for x in (-p.FOOT_RECESS_X.v, p.FOOT_RECESS_X.v):
        seat = Pos(x, middle, 0) * Cylinder(p.FOOT_RECESS_D.v / 2, 2 * p.FOOT_RECESS_DEPTH.v)
        stand -= desk.inverse() * seat
    return PrintedPart("stand", ZONE, stand, SIDE_DOWN, "On its left end; walls vertical. No supports.")
