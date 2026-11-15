"""Small geometry helpers and the part/placeholder records shared by every module."""

from dataclasses import dataclass, field

from build123d import Align, Box, Color, Compound, Cylinder, Location, Part, Plane, Pos, RectangleRounded, Rot, extrude

Vec3 = tuple[float, float, float]

MIN_ALIGN = (Align.MIN, Align.MIN, Align.MIN)
KEEPOUT_COLOR = Color(1.0, 0.45, 0.1, 0.35)
# How far a cutting tool extends past the faces it cuts, so no zero-thickness skins remain.
CUT_OVERSHOOT = 1.0


def box(min_corner: Vec3, size: Vec3) -> Part:
    """Axis-aligned box given its minimum corner and its X/Y/Z extents."""
    return Pos(*min_corner) * Box(*size, align=MIN_ALIGN)


def box_between(lo: Vec3, hi: Vec3) -> Part:
    return box(lo, (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))


def front_plane(y: float, centre_z: float, centre_x: float = 0.0) -> Plane:
    """A plane facing the viewer (-Y) with local x = world X and local y = world Z."""
    return Plane(origin=(centre_x, y, centre_z), x_dir=(1, 0, 0), z_dir=(0, -1, 0))


def rounded_prism(width: float, height: float, radius: float, y0: float, length: float, centre_z: float) -> Part:
    """Rounded rectangle in the XZ plane, centred on X=0, extruded rearward (+Y) from y0."""
    sketch = front_plane(y0, centre_z) * RectangleRounded(width, height, radius)
    return extrude(sketch, amount=length, dir=(0, 1, 0))


def y_cylinder(x: float, y0: float, y1: float, z: float, diameter: float) -> Part:
    """Cylinder whose axis runs along Y from y0 to y1."""
    return Pos(x, (y0 + y1) / 2, z) * Rot(90, 0, 0) * Cylinder(diameter / 2, y1 - y0)


def side_plane(x0: float) -> Plane:
    """A plane at X = x0 whose local x is world Y and local y is world Z; extrudes towards +X."""
    return Plane(origin=(x0, 0, 0), x_dir=(0, 1, 0), z_dir=(1, 0, 0))


@dataclass
class Placeholder:
    """An electronics stand-in: its solid body plus spaces that must stay clear."""

    name: str
    body: Part
    color: Color
    keepouts: dict[str, Part] = field(default_factory=dict)

    def compound(self) -> Compound:
        self.body.label = f"{self.name}Body"
        self.body.color = self.color
        children = [self.body]
        for name, keepout in self.keepouts.items():
            keepout.label = f"KeepOut_{name}"
            keepout.color = KEEPOUT_COLOR
            children.append(keepout)
        return Compound(label=self.name, children=children)


@dataclass
class PrintedPart:
    """A part to be printed.

    `body` is in assembly position (case coordinates). `zone` names its colour
    zone in colourway.toml. `print_pose` rotates it into print orientation;
    `on_bed()` then sits it on Z=0, centred on the bed origin.
    """

    name: str
    zone: str
    body: Part
    print_pose: Location
    orientation_note: str

    def bed_location(self) -> Location:
        box_ = (self.print_pose * self.body).bounding_box()
        return Pos(-box_.center().X, -box_.center().Y, -box_.min.Z) * self.print_pose

    def on_bed(self) -> Part:
        return self.bed_location() * self.body


# Rotations from assembly position to print position.
REAR_FACE_DOWN = Location(Rot(-90, 0, 0))  # rear (max Y) face onto the bed
SIDE_DOWN = Location(Rot(0, -90, 0))  # left (min X) face onto the bed
AS_MODELLED = Location()
