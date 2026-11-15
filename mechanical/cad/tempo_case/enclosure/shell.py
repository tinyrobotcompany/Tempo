"""Rear shell in the body colour: an open tray behind the panel.

Its rim presses the band's flange against the panel's metal back. Four columns
carry the M2.5 screws into the panel's corner holes; each screw is put in from
the back through a counterbore and its head bears on the column's solid end.
Two rows of vertical vent slots run through the back, above and below the
stand. Four heat-set inserts in the back take the stand screws.

Prints back face down: the walls, columns and bosses rise from the bed.
"""

from build123d import Part

from tempo_case import layout
from tempo_case import parameters as p
from tempo_case.geometry import CUT_OVERSHOOT, REAR_FACE_DOWN, PrintedPart, box_between, rounded_prism, y_cylinder

ZONE = "shell"


def _vent_slots() -> Part | None:
    zc, half = layout.panel_centre_z(), p.VENT_SLOT_LEN.v / 2
    back = layout.shell_back_y()
    count = int(p.VENT_SPAN_X.v // p.VENT_PITCH.v) + 1
    start = -(count - 1) * p.VENT_PITCH.v / 2
    slots = None
    for row in (-1, 1):
        centre = zc + row * p.VENT_ROW_OFFSET.v
        for i in range(count):
            x = start + i * p.VENT_PITCH.v
            slot = box_between(
                (x - p.VENT_SLOT_W.v / 2, back - p.SHELL_BACK.v - CUT_OVERSHOOT, centre - half),
                (x + p.VENT_SLOT_W.v / 2, back + CUT_OVERSHOOT, centre + half),
            )
            slots = slot if slots is None else slots + slot
    return slots


def build_shell() -> PrintedPart:
    zc = layout.panel_centre_z()
    width, height, radius = layout.shell_outer_size()
    rim, back = layout.flange_rear_y(), layout.shell_back_y()
    outer = rounded_prism(width, height, radius, rim, back - rim, zc)
    wall = p.SHELL_WALL.v
    cavity = rounded_prism(width - 2 * wall, height - 2 * wall, max(radius - wall, 0.5), rim - CUT_OVERSHOOT, back - p.SHELL_BACK.v - rim + CUT_OVERSHOOT, zc)
    shell = outer - cavity

    # Screw columns, trimmed to the outline where they meet the corner walls.
    columns = None
    for x, z in layout.mount_points():
        column = y_cylinder(x, rim, back - p.SHELL_BACK.v + CUT_OVERSHOOT, z, p.SCREW_COLUMN_D.v)
        columns = column if columns is None else columns + column
    shell += columns & outer

    # Insert bosses for the stand, standing inward from the back wall.
    boss_front = back - p.M3_INSERT_DEPTH.v - 1.0
    for x, z in layout.stand_screw_points():
        shell += y_cylinder(x, boss_front, back - p.SHELL_BACK.v + CUT_OVERSHOOT, z, p.INSERT_BOSS_D.v)

    for x, z in layout.mount_points():
        shell -= y_cylinder(x, rim - CUT_OVERSHOOT, back + CUT_OVERSHOOT, z, p.M25_CLEARANCE_HOLE.v)
        shell -= y_cylinder(x, rim + p.SCREW_FLOOR.v, back + CUT_OVERSHOOT, z, p.M25_HEAD_HOLE.v)
    for x, z in layout.stand_screw_points():
        shell -= y_cylinder(x, back - p.M3_INSERT_DEPTH.v, back + CUT_OVERSHOOT, z, p.M3_INSERT_HOLE.v)

    shell -= _vent_slots()
    return PrintedPart("shell", ZONE, shell, REAR_FACE_DOWN, "Back face down; walls and columns rise from the bed. No supports.")
