"""Checks that must pass before any geometry is exported."""

from itertools import combinations

from build123d import Part

from tempo_case import layout
from tempo_case import parameters as p
from tempo_case.geometry import Placeholder, PrintedPart
from tempo_case.stability import Stability

OVERLAP_TOLERANCE_MM3 = 1e-3
PRINT_TOLERANCE_MM = 1e-3
DESK_TOLERANCE_MM = 0.05


class ValidationError(Exception):
    pass


def _raise(title: str, errors: list[str]) -> None:
    if errors:
        raise ValidationError(f"{title}:\n  " + "\n  ".join(errors))


def parameter_errors() -> list[str]:
    errors = []
    for name, param in p.all_params().items():
        if param.unit == "mm" and not param.signed and param.value <= 0:
            errors.append(f"{name} must be positive, got {param.value}")
    for axis in "XYZ":
        limit, build = getattr(p, f"MAX_PRINT_{axis}").v, getattr(p, f"PRINTER_BUILD_{axis}").v
        if limit > build:
            errors.append(f"MAX_PRINT_{axis} ({limit}) exceeds PRINTER_BUILD_{axis} ({build})")
    if errors:
        return errors

    engagement = layout.panel_screw_engagement()
    deepest = p.MOUNT_THREAD_DEPTH.v - p.THREAD_DEPTH_MARGIN.v
    if engagement > deepest:
        errors.append(f"M2.5 x {p.PANEL_SCREW_LENGTH.v:g} screws reach {engagement:.1f} mm into the panel; its threads are {p.MOUNT_THREAD_DEPTH.v:g} mm deep (keep ≤ {deepest:.1f})")
    if engagement < p.MIN_THREAD_ENGAGEMENT.v:
        errors.append(f"M2.5 screws engage only {engagement:.1f} mm; at least {p.MIN_THREAD_ENGAGEMENT.v:g} mm is needed")
    stand_engagement = layout.stand_screw_engagement()
    if not p.MIN_THREAD_ENGAGEMENT.v <= stand_engagement <= p.M3_INSERT_DEPTH.v - p.THREAD_DEPTH_MARGIN.v:
        errors.append(f"M3 stand screws engage {stand_engagement:.1f} mm; keep between {p.MIN_THREAD_ENGAGEMENT.v:g} and {p.M3_INSERT_DEPTH.v - p.THREAD_DEPTH_MARGIN.v:.1f}")
    if p.LIP_OVERLAP.v + p.BAND_CLEARANCE.v > p.MAX_OVERHANG.v + 1e-9:
        errors.append(f"The lip overhangs {p.LIP_OVERLAP.v + p.BAND_CLEARANCE.v:.1f} mm when printed; the limit is {p.MAX_OVERHANG.v:g} mm")
    if p.LIP_OVERLAP.v >= p.ACTIVE_LEFT.v or p.LIP_OVERLAP.v >= p.ACTIVE_BOTTOM.v:
        errors.append("The lip would cover the visible screen area")

    shell_w, shell_h, _ = layout.shell_outer_size()
    inset = layout.flange_inset()
    flange_inner = (p.BACK_W.v / 2 - inset, p.BACK_H.v / 2 - inset)
    rim_inner = (shell_w / 2 - p.SHELL_WALL.v, shell_h / 2 - p.SHELL_WALL.v)
    if rim_inner[0] < flange_inner[0] or rim_inner[1] < flange_inner[1]:
        errors.append("The shell's rim falls partly inside the band flange's opening, so it would not clamp the flange all round")
    hole_r = p.SCREW_COLUMN_D.v / 2
    for axis, pitch, edge in (("X", p.MOUNT_PITCH_X.v, shell_w), ("Z", p.MOUNT_PITCH_Z.v, shell_h)):
        if pitch / 2 + p.M25_CLEARANCE_HOLE.v / 2 + 0.8 > edge / 2:
            errors.append(f"Panel screw holes are too close to the shell's outer edge along {axis}")
    if hole_r <= p.M25_HEAD_HOLE.v / 2:
        errors.append("SCREW_COLUMN_D must exceed the counterbore")
    if p.STAND_SCREW_DZ.v + p.M3_HEAD_HOLE.v / 2 + p.STAND_MEMBER.v > p.STAND_ATTACH_HEIGHT.v / 2:
        errors.append("Stand screws are too close to the top or bottom of the stand's front wall to reach their heads")
    return errors


def require_valid_parameters() -> None:
    _raise("Invalid parameters", parameter_errors())


def _overlap(a: Part, b: Part) -> float:
    if not a.bounding_box().overlaps(b.bounding_box()):
        return 0.0
    common = a & b
    return common.volume if common else 0.0


def collision_errors(panel: Placeholder, parts: list[PrintedPart]) -> list[str]:
    """Printed parts must not enter the display or its keep-outs, nor each other."""
    errors = []
    for part in parts:
        shapes = {"body": panel.body} | {f"keep-out {k}": v for k, v in panel.keepouts.items()}
        for what, shape in shapes.items():
            volume = _overlap(part.body, shape)
            if volume > OVERLAP_TOLERANCE_MM3:
                errors.append(f"{part.name} enters the display {what} by {volume:.1f} mm³")
    for a, b in combinations(parts, 2):
        volume = _overlap(a.body, b.body)
        if volume > OVERLAP_TOLERANCE_MM3:
            errors.append(f"{a.name} overlaps {b.name} by {volume:.1f} mm³")
    return errors


def stand_access_errors(parts: list[PrintedPart]) -> list[str]:
    """A driver must reach each stand screw head through the stand's hollow."""
    from tempo_case.geometry import y_cylinder

    stand = next(pp for pp in parts if pp.name == "stand").body
    face = layout.shell_back_y() + p.STAND_MEMBER.v
    errors = []
    for x, z in layout.stand_screw_points():
        reach = y_cylinder(x, face + 0.05, face + 30.0, z, p.M3_HEAD_HOLE.v)
        if _overlap(stand, reach) > OVERLAP_TOLERANCE_MM3:
            errors.append(f"Stand screw at x={x:.0f}, z={z:.0f} cannot be reached through the hollow")
    return errors


def desk_errors(panel: Placeholder, parts: list[PrintedPart]) -> list[str]:
    """In the desk pose nothing goes below the desk, and both the band and the stand touch it."""
    desk = layout.desk_location()
    errors = []
    lowest = {pp.name: (desk * pp.body).bounding_box().min.Z for pp in parts if pp.name != "calibration_coupon"}
    lowest[panel.name] = (desk * panel.body).bounding_box().min.Z
    resting = ("band_left", "band_right", "stand")
    for name, z in lowest.items():
        if z < -DESK_TOLERANCE_MM:
            errors.append(f"{name} goes {-z:.2f} mm below the desk")
        elif name not in resting and z < p.MIN_DESK_CLEARANCE.v:
            errors.append(f"{name} clears the desk by only {z:.2f} mm (minimum {p.MIN_DESK_CLEARANCE.v:g} mm)")
    for name in resting:
        if abs(lowest[name]) > DESK_TOLERANCE_MM:
            errors.append(f"{name} should rest on the desk but its lowest point is {lowest[name]:.2f} mm above it")
    return errors


def stability_errors(result: Stability) -> list[str]:
    errors = []
    if not 0 < result.centre_y < result.rear_pivot_y:
        errors.append(f"Centre of mass (desk Y {result.centre_y:.0f} mm) is outside the footprint 0..{result.rear_pivot_y:.0f} mm")
    if result.tip_back_force_n < p.MIN_TIP_FORCE.v:
        errors.append(f"A {result.tip_back_force_n:.1f} N push on the top tips it backwards; at least {p.MIN_TIP_FORCE.v:g} N is required")
    return errors


def require_valid_assembly(panel: Placeholder, parts: list[PrintedPart], result: Stability) -> None:
    names = [pp.name for pp in parts]
    errors = [f"Duplicate part name {n}" for n in set(names) if names.count(n) > 1]
    errors += collision_errors(panel, parts)
    errors += stand_access_errors(parts)
    errors += desk_errors(panel, parts)
    errors += stability_errors(result)
    _raise("Invalid assembly", errors)


def print_envelope_violations(part: PrintedPart) -> list[str]:
    size = part.on_bed().bounding_box().size
    limits = (p.MAX_PRINT_X.v, p.MAX_PRINT_Y.v, p.MAX_PRINT_Z.v)
    return [
        f"{axis} extent {extent:.1f} mm exceeds {limit:.1f} mm print limit"
        for axis, extent, limit in zip("XYZ", (size.X, size.Y, size.Z), limits)
        if extent > limit + PRINT_TOLERANCE_MM
    ]


def require_printable(parts: list[PrintedPart]) -> None:
    _raise("Parts exceed the print envelope", [f"{pp.name}: {v}" for pp in parts for v in print_envelope_violations(pp)])
