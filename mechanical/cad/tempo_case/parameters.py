"""Central engineering parameters for the Tempo case (concept A: slab with trim band).

Every dimension used by geometry code lives here, with its unit and how much we
trust it. Geometry modules read values from this file rather than hard-coding them.
Colours are not parameters: they live in mechanical/colourway.toml.

Case coordinates (stable for the life of the project; the case stands upright):
    X = left (-) / right (+) as seen from the front, origin on the panel centre line
    Y = front (-) / rear (+), origin on the front face of the display glass
    Z = bottom (0) / top (+), origin on the bottom of the coloured band
The desk pose (layout.desk_location) tilts the case back by TILT_DEG so the
band's rear-bottom edge and the stand's foot both rest on the desk.
All lengths are millimetres; angles are degrees; masses are kilograms.
"""

from dataclasses import dataclass
from enum import Enum


class Status(str, Enum):
    """How much a parameter value can be trusted."""

    VERIFIED = "VERIFIED"  # confirmed by a physical fit test
    DATASHEET = "DATASHEET"  # taken from a manufacturer drawing, not yet measured
    PROVISIONAL = "PROVISIONAL"  # design intent or estimate; expected to change
    MEASURED = "MEASURED"  # measured on the real hardware
    CALIBRATED = "CALIBRATED"  # established by a printed calibration coupon
    POLICY = "POLICY"  # a deliberate design rule rather than a physical fact


@dataclass(frozen=True)
class Param:
    value: float
    unit: str
    status: Status
    note: str
    signed: bool = False  # may be zero or negative

    @property
    def v(self) -> float:
        return self.value


D, P, M, POL, CAL = Status.DATASHEET, Status.PROVISIONAL, Status.MEASURED, Status.POLICY, Status.CALIBRATED

PRINT_MATERIAL = "PETG"
PRINT_REVISION = "A1"

# --- Printer: Elegoo Centauri Carbon (as Waveform One) -------------------------

PRINTER_BUILD_X = Param(256.0, "mm", D, "Centauri Carbon nominal build volume.")
PRINTER_BUILD_Y = Param(256.0, "mm", D, "Centauri Carbon nominal build volume.")
PRINTER_BUILD_Z = Param(256.0, "mm", D, "Centauri Carbon nominal build volume.")
MAX_PRINT_X = Param(245.0, "mm", POL, "Engineering envelope: margin for brims and bed edges.")
MAX_PRINT_Y = Param(245.0, "mm", POL, "Engineering envelope: margin for brims and bed edges.")
MAX_PRINT_Z = Param(245.0, "mm", POL, "Engineering envelope: margin for brims and bed edges.")
PLATE_DEPTH = Param(225.0, "mm", POL, "Front-to-back room on a plate; the profile keeps ~23 mm for its purge line.")
PLATE_GAP = Param(8.0, "mm", POL, "Space between parts on a print-job plate.")
LAYER_HEIGHT = Param(0.2, "mm", POL, "0.20mm Standard profile.")
MAX_OVERHANG = Param(2.0, "mm", POL, "Longest unsupported horizontal ledge allowed in print orientation.")

# --- Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1 -----------------------------------
# From Waveshare's dimension drawing (reference/waveshare-10.1-dimensions.png,
# docs.waveshare.com, checked 2026-11-15). Measure the received unit before printing.

GLASS_W = Param(250.2, "mm", D, "Cover glass outer width.")
GLASS_H = Param(169.0, "mm", D, "Cover glass outer height.")
GLASS_R = Param(7.0, "mm", D, "Cover glass corner radius.")
GLASS_T = Param(3.2, "mm", D, "Cover glass thickness, ahead of the metal back.")
PANEL_DEPTH = Param(12.0, "mm", D, "Glass front to back plate rear face.")
BACK_W = Param(245.2, "mm", D, "Metal back width (2.5 mm inside the glass each side).")
BACK_H = Param(164.0, "mm", D, "Metal back height.")
BACK_R = Param(4.5, "mm", D, "Metal back corner radius.")
ACTIVE_W = Param(217.18, "mm", D, "Visible area width.")
ACTIVE_H = Param(135.96, "mm", D, "Visible area height.")
ACTIVE_LEFT = Param(16.51, "mm", D, "Glass left edge to visible area.")
ACTIVE_BOTTOM = Param(16.52, "mm", D, "Glass bottom edge to visible area.")
MOUNT_PITCH_X = Param(236.2, "mm", D, "Corner M2.5 holes in the back, centre to centre, across.")
MOUNT_PITCH_Z = Param(155.0, "mm", D, "Corner M2.5 holes in the back, centre to centre, up.")
MOUNT_THREAD_DEPTH = Param(4.0, "mm", D, "Waveshare: M2.5, depth 4 mm = 3 mm base plate + 1 mm back plate.")
PANEL_MASS = Param(0.75, "kg", P, "Display with board; estimate. Weigh it: it drives the stability check.")
# Side view of the drawing: the USB-C, button and card openings are on the
# right-hand edge (seen from the front), roughly 50-130 mm below the glass top.
PORT_ZONE_TOP = Param(45.0, "mm", P, "Glass top edge to the uppermost port; scaled off the drawing (±5 mm). Measure.")
PORT_ZONE_BOTTOM = Param(135.0, "mm", P, "Glass top edge to the lowest port; scaled off the drawing (±5 mm). Measure.")
PORT_PLUG_KEEPOUT = Param(35.0, "mm", P, "USB-C plug body plus cable bend beyond the panel edge.")
REAR_KEEPOUT_W = Param(200.0, "mm", P, "Behind the back plate: header, antenna lead and speaker area width.")
REAR_KEEPOUT_H = Param(120.0, "mm", P, "Same, height.")
REAR_KEEPOUT_DEPTH = Param(10.0, "mm", P, "Same, depth behind the back plate (antenna and cable bends).")

# --- Trim band (coloured, two halves) ---------------------------------------------
# Wraps the glass edge. Its rear flange is clamped between the panel's back and
# the shell by the four M2.5 corner screws, so the band needs no screws of its own.

BAND_WALL = Param(2.4, "mm", P, "Side wall thickness around the glass edge.")
BAND_CLEARANCE = Param(0.3, "mm", P, "Per side, between glass edge and band wall.")
LIP_THICKNESS = Param(1.6, "mm", P, "Front lip ahead of the glass: the coloured outline seen from the front.")
LIP_OVERLAP = Param(1.5, "mm", POL, "Lip reaches over the 16.5 mm black glass border; prints as a short ledge.")
LIP_GAP = Param(0.5, "mm", P, "Lip to glass: room for a thin compliant pad, so the glass is never clamped.")
FLANGE_THICKNESS = Param(2.0, "mm", P, "Rear flange clamped between back plate and shell.")
FLANGE_HOLE_MARGIN = Param(2.0, "mm", POL, "Flange material inward of each M2.5 screw hole.")
PORT_WINDOW_MARGIN = Param(1.5, "mm", P, "Port window beyond the port zone, each end.")

# --- Rear shell (body colour) ---------------------------------------------------------

SHELL_INSET = Param(0.6, "mm", P, "Shell outline inside the metal back outline, per side; keeps it within the bed.")
SHELL_WALL = Param(2.4, "mm", P, "Side wall thickness.")
SHELL_BACK = Param(2.4, "mm", P, "Back wall thickness.")
SHELL_DEPTH = Param(20.0, "mm", P, "Band flange to shell back; room for the rear keep-out and stand bosses.")
SCREW_COLUMN_D = Param(7.0, "mm", P, "Columns carrying the four M2.5 panel screws.")
SCREW_FLOOR = Param(5.0, "mm", P, "Solid column end pressing the flange; the screw head sits on it.")
M25_CLEARANCE_HOLE = Param(2.9, "mm", P, "M2.5 clearance; confirm with the coupon.")
M25_HEAD_HOLE = Param(5.4, "mm", P, "Counterbore for an M2.5 cap or pan head (≤ 5 mm).")
PANEL_SCREW_LENGTH = Param(10.0, "mm", POL, "M2.5 x 10 screws into the panel's back.")
MIN_THREAD_ENGAGEMENT = Param(2.5, "mm", POL, "At least five M2.5 threads.")
THREAD_DEPTH_MARGIN = Param(0.5, "mm", POL, "Screw tip stops this far short of the panel's 4 mm thread depth.")
VENT_SLOT_W = Param(2.5, "mm", P, "Narrow so slot ends bridge cleanly.")
VENT_SLOT_LEN = Param(18.0, "mm", P, "Vertical slot length.")
VENT_PITCH = Param(7.0, "mm", P, "Slot spacing.")
VENT_SPAN_X = Param(180.0, "mm", P, "Width of each row of slots.")
VENT_ROW_OFFSET = Param(62.0, "mm", P, "Each row's centre above/below the panel centre.")

# --- Stand (coloured) ---------------------------------------------------------------------
# A wide hollow wedge screwed to the shell back. Leaning back, the case rests on
# the band's rear-bottom edge and on the stand's foot.

TILT_DEG = Param(13.0, "deg", POL, "Backward tilt from vertical. At 15° the shell's bottom-rear edge would clear the desk by only 0.2 mm.")
MIN_DESK_CLEARANCE = Param(1.0, "mm", POL, "Parts other than the band and the stand stay at least this far above the desk.")
STAND_REACH = Param(120.0, "mm", P, "Desk distance from the band's rear-bottom edge (the front pivot) to the rear of the foot.")
STAND_FOOT_LENGTH = Param(30.0, "mm", P, "Foot length along the desk.")
STAND_WIDTH = Param(150.0, "mm", P, "Across X; also its print height.")
STAND_ATTACH_HEIGHT = Param(80.0, "mm", P, "Contact height on the shell back, centred on the panel.")
STAND_MEMBER = Param(7.0, "mm", P, "Wall thickness of the hollow wedge.")
STAND_SCREW_X = Param(50.0, "mm", P, "Stand screws either side of the centre line.")
STAND_SCREW_DZ = Param(20.0, "mm", P, "Stand screws above/below the panel centre.")
STAND_SCREW_LENGTH = Param(10.0, "mm", POL, "M3 x 10 through the stand's front wall into inserts in the shell.")
M3_INSERT_HOLE = Param(4.1, "mm", CAL, "Waveform One coupon 2026-10-10, same printer and PETG.")
M3_INSERT_DEPTH = Param(6.0, "mm", P, "Insert length (5.7 mm) plus a little.")
M3_CLEARANCE_HOLE = Param(3.2, "mm", CAL, "Waveform One coupon 2026-10-10.")
M3_HEAD_HOLE = Param(6.5, "mm", P, "Clearance round an M3 head, for the driver.")
INSERT_BOSS_D = Param(8.0, "mm", P, "Heat-set insert bosses inside the shell back.")
FOOT_RECESS_D = Param(12.0, "mm", P, "Seats for stick-on rubber feet under the stand.")
FOOT_RECESS_DEPTH = Param(1.0, "mm", P, "Seat depth.")
FOOT_RECESS_X = Param(55.0, "mm", P, "Seat centres either side of the centre line.")

# --- Stability (desk pose) --------------------------------------------------------------

PETG_DENSITY = Param(1.27, "g/cm3", D, "Typical PETG.")
PRINT_FILL_FACTOR = Param(0.6, "ratio", P, "Printed mass / solid mass for these thin-walled parts; estimate.")
MIN_TIP_FORCE = Param(4.0, "N", POL, "Horizontal push on the top edge that must not tip it backwards (a firm tap is ~1-2 N).")
GRAVITY = Param(9.81, "m/s2", D, "Standard gravity.")

# --- Calibration coupon -------------------------------------------------------------------

COUPON_INSERT_HOLES = (3.9, 4.0, 4.1, 4.2)  # M3 heat-set inserts
COUPON_M25_HOLES = (2.7, 2.8, 2.9, 3.0)  # smallest an M2.5 screw slides through
COUPON_PITCH = Param(12.0, "mm", P, "Hole spacing on the coupon.")
COUPON_THICKNESS = Param(8.0, "mm", P, "Deeper than an insert.")


def all_params() -> dict[str, Param]:
    """Every parameter defined in this module, keyed by name."""
    return {name: value for name, value in globals().items() if isinstance(value, Param)}
