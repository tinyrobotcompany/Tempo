"""How hard a push it takes to tip the case over, in its desk pose.

Masses: the display from PANEL_MASS at its bounding-box centre; printed parts
from their solid volume x PETG density x fill factor. The push is horizontal,
at the top-centre of the band's front edge; it tips the case backwards about
the rear of the stand's foot (or forwards about the band's rear-bottom edge, if pulled).
"""

from dataclasses import dataclass

from build123d import Part, Vector, Vertex

from tempo_case import layout
from tempo_case import parameters as p


@dataclass(frozen=True)
class Stability:
    mass_kg: float
    centre_y: float  # desk Y of the centre of mass (0 = front pivot, the band's rear-bottom edge)
    centre_z: float
    push_height: float
    rear_pivot_y: float
    tip_back_force_n: float  # push on the screen needed to tip it backwards
    tip_forward_force_n: float  # pull on the top needed to tip it forwards


def analyse(panel_body: Part, printed: list[Part]) -> Stability:
    desk = layout.desk_location()
    weighted, total = Vector(0, 0, 0), 0.0
    panel = desk * panel_body
    centre = panel.bounding_box().center()
    weighted += centre * p.PANEL_MASS.v
    total += p.PANEL_MASS.v
    for body in printed:
        placed = desk * body
        mass = placed.volume / 1000 * p.PETG_DENSITY.v * p.PRINT_FILL_FACTOR.v / 1000
        weighted += placed.center() * mass
        total += mass
    com = weighted / total

    _, height, _ = layout.band_outer_size()
    top = (desk * Vertex(0, layout.band_front_y(), height)).center()
    weight = total * p.GRAVITY.v
    rear_pivot = p.STAND_REACH.v
    return Stability(
        mass_kg=total,
        centre_y=com.Y,
        centre_z=com.Z,
        push_height=top.Z,
        rear_pivot_y=rear_pivot,
        tip_back_force_n=weight * (rear_pivot - com.Y) / top.Z,
        tip_forward_force_n=weight * com.Y / top.Z,
    )
