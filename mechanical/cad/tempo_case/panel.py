"""Stand-in for the Waveshare 10.1-inch display kit, from the manufacturer drawing."""

from build123d import Color

from tempo_case import layout
from tempo_case import parameters as p
from tempo_case.geometry import Placeholder, box_between, rounded_prism

PANEL_NAME = "Display"
GLASS_COLOR = Color(0.05, 0.06, 0.08)


def glass_top_z() -> float:
    return layout.panel_centre_z() + p.GLASS_H.v / 2


def port_zone_z() -> tuple[float, float]:
    """(low, high) Z of the ports on the panel's right-hand edge."""
    top = glass_top_z()
    return top - p.PORT_ZONE_BOTTOM.v, top - p.PORT_ZONE_TOP.v


def build_panel() -> Placeholder:
    zc = layout.panel_centre_z()
    glass = rounded_prism(p.GLASS_W.v, p.GLASS_H.v, p.GLASS_R.v, 0.0, p.GLASS_T.v, zc)
    back = rounded_prism(p.BACK_W.v, p.BACK_H.v, p.BACK_R.v, p.GLASS_T.v, p.PANEL_DEPTH.v - p.GLASS_T.v, zc)
    body = glass + back

    edge = p.BACK_W.v / 2
    low, high = port_zone_z()
    ports = box_between((edge, p.GLASS_T.v, low), (edge + p.PORT_PLUG_KEEPOUT.v, p.PANEL_DEPTH.v, high))
    w, h = p.REAR_KEEPOUT_W.v, p.REAR_KEEPOUT_H.v
    rear = box_between((-w / 2, p.PANEL_DEPTH.v, zc - h / 2), (w / 2, p.PANEL_DEPTH.v + p.REAR_KEEPOUT_DEPTH.v, zc + h / 2))
    return Placeholder(PANEL_NAME, body, GLASS_COLOR, {"ports": ports, "rear_connectors": rear})


def active_area_z() -> tuple[float, float]:
    bottom = layout.panel_centre_z() - p.GLASS_H.v / 2 + p.ACTIVE_BOTTOM.v
    return bottom, bottom + p.ACTIVE_H.v


def active_area_x() -> tuple[float, float]:
    left = -p.GLASS_W.v / 2 + p.ACTIVE_LEFT.v
    return left, left + p.ACTIVE_W.v
