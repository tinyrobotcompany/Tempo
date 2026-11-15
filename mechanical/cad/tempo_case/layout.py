"""Derived positions shared by the parts: datums, mounting points and the desk pose."""

import math

from build123d import Location, Pos, Rot

from tempo_case import parameters as p


def panel_centre_z() -> float:
    """Height of the glass centre above the band's bottom."""
    return p.BAND_WALL.v + p.BAND_CLEARANCE.v + p.GLASS_H.v / 2


def band_outer_size() -> tuple[float, float, float]:
    """Band outer width, height and corner radius."""
    grow = p.BAND_CLEARANCE.v + p.BAND_WALL.v
    return p.GLASS_W.v + 2 * grow, p.GLASS_H.v + 2 * grow, p.GLASS_R.v + grow


def band_front_y() -> float:
    """Front face of the band's lip (the most forward point of the case)."""
    return -(p.LIP_GAP.v + p.LIP_THICKNESS.v)


def flange_rear_y() -> float:
    """Rear face of the band's flange, where the shell's rim lands."""
    return p.PANEL_DEPTH.v + p.FLANGE_THICKNESS.v


def shell_back_y() -> float:
    """Outer face of the shell back, where the stand attaches."""
    return flange_rear_y() + p.SHELL_DEPTH.v


def shell_outer_size() -> tuple[float, float, float]:
    inset = p.SHELL_INSET.v
    return p.BACK_W.v - 2 * inset, p.BACK_H.v - 2 * inset, p.BACK_R.v - inset


def flange_inset() -> float:
    """Distance from the metal back's edge to the flange's inner edge: covers the screw holes plus a margin."""
    hole_from_edge = (p.BACK_W.v - p.MOUNT_PITCH_X.v) / 2
    return hole_from_edge + p.M25_CLEARANCE_HOLE.v / 2 + p.FLANGE_HOLE_MARGIN.v


def mount_points() -> list[tuple[float, float]]:
    """(x, z) of the four M2.5 corner holes in the panel's back."""
    zc, dx, dz = panel_centre_z(), p.MOUNT_PITCH_X.v / 2, p.MOUNT_PITCH_Z.v / 2
    return [(sx * dx, zc + sz * dz) for sx in (-1, 1) for sz in (-1, 1)]


def stand_screw_points() -> list[tuple[float, float]]:
    zc, dx, dz = panel_centre_z(), p.STAND_SCREW_X.v, p.STAND_SCREW_DZ.v
    return [(sx * dx, zc + sz * dz) for sx in (-1, 1) for sz in (-1, 1)]


def panel_screw_engagement() -> float:
    """Thread length of each M2.5 screw inside the panel's back."""
    return p.PANEL_SCREW_LENGTH.v - p.SCREW_FLOOR.v - p.FLANGE_THICKNESS.v


def stand_screw_engagement() -> float:
    return p.STAND_SCREW_LENGTH.v - p.STAND_MEMBER.v


def pivot_y() -> float:
    """Case Y of the band's rear-bottom edge: the front line of contact with the desk."""
    return flange_rear_y()


def desk_location() -> Location:
    """Maps case coordinates to desk coordinates.

    Leaning back by TILT_DEG (top towards +Y), the case rests on the band's
    rear-bottom edge, which goes to the desk origin. The desk is world Z = 0;
    world +Y points away from the viewer.
    """
    return Rot(-p.TILT_DEG.v, 0, 0) * Pos(0, -pivot_y(), 0)


def desk_point(distance: float) -> tuple[float, float]:
    """(y, z) in case coordinates of the desk line `distance` mm behind the front pivot."""
    t = math.radians(p.TILT_DEG.v)
    return pivot_y() + distance * math.cos(t), distance * math.sin(t)
