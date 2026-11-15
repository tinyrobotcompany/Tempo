"""Entry point: validate, build the Tempo case, colour it from colourway.toml and export.

Run from mechanical/cad:
    uv run python -m tempo_case.build                     # preset named by `use`
    uv run python -m tempo_case.build --preset sage       # another preset, once
    uv run python -m tempo_case.build --colourway my.toml # another colour file
"""

import argparse
import logging
from dataclasses import dataclass
from pathlib import Path

from build123d import Color, Compound

from tempo_case import colourway, layout
from tempo_case import parameters as p
from tempo_case.coupon import build_coupon
from tempo_case.enclosure import band, shell, stand
from tempo_case.export import EXPORT_ROOT, export_model, export_parts, export_print_jobs, write_print_plan
from tempo_case.geometry import Placeholder, PrintedPart, box_between
from tempo_case.panel import active_area_x, active_area_z, build_panel
from tempo_case.stability import Stability, analyse
from tempo_case.validation import require_printable, require_valid_assembly, require_valid_parameters

log = logging.getLogger("tempo_case")
ROOT_NAME = "TempoCase"
SCREEN_COLOR = Color(0.04, 0.07, 0.11)


@dataclass
class Built:
    panel: Placeholder
    enclosure: list[PrintedPart]
    printed: list[PrintedPart]  # enclosure plus the coupon
    stability: Stability


def build_enclosure() -> list[PrintedPart]:
    return band.build_halves() + [shell.build_shell(), stand.build_stand()]


def build_all() -> Built:
    require_valid_parameters()
    panel = build_panel()
    enclosure = build_enclosure()
    result = analyse(panel.body, [pp.body for pp in enclosure])
    require_valid_assembly(panel, enclosure, result)
    printed = enclosure + [build_coupon()]
    require_printable(printed)
    return Built(panel, enclosure, printed, result)


def part_names() -> frozenset[str]:
    return frozenset({"band_left", "band_right", "shell", "stand", "calibration_coupon"})


def coloured_model(built: Built, colours: colourway.Colourway) -> Compound:
    """The assembly in its desk pose, each part in its configured colour."""
    desk = layout.desk_location()
    panel = desk * built.panel.body
    panel.label, panel.color = "Display", built.panel.color
    x0, x1 = active_area_x()
    z0, z1 = active_area_z()
    screen = desk * box_between((x0, -0.05, z0), (x1, 0.0, z1))
    screen.label, screen.color = "DisplayActiveArea", SCREEN_COLOR
    children = [panel, screen]
    for pp in built.enclosure:
        placed = desk * pp.body
        placed.label = pp.name
        placed.color = colours.filament_for(pp.name, pp.zone).color
        children.append(placed)
    return Compound(label=ROOT_NAME, children=children)


def desk_clearance(built: Built) -> float:
    shell_part = next(pp for pp in built.enclosure if pp.name == "shell")
    return (layout.desk_location() * shell_part.body).bounding_box().min.Z


def summary_lines(built: Built) -> list[str]:
    s = built.stability
    w, h, _ = layout.band_outer_size()
    return [
        f"Case {w:.1f} W x {h:.1f} H mm at the band; shell back {layout.shell_back_y() - layout.band_front_y():.1f} mm behind the front; tilt {p.TILT_DEG.v:g}°",
        f"Stand foot ends {p.STAND_REACH.v:.0f} mm behind the front pivot (band's rear-bottom edge)",
        f"Shell clears the desk by {desk_clearance(built):.1f} mm",
        f"Estimated mass {s.mass_kg:.2f} kg (display {p.PANEL_MASS.v:g} kg is an estimate: weigh it)",
        f"Tips backwards under a {s.tip_back_force_n:.1f} N push on the top edge (limit {p.MIN_TIP_FORCE.v:g} N); forwards under a {s.tip_forward_force_n:.1f} N pull",
        f"M2.5 x {p.PANEL_SCREW_LENGTH.v:g} panel screws engage {layout.panel_screw_engagement():.1f} mm of the panel's {p.MOUNT_THREAD_DEPTH.v:g} mm threads",
        f"M3 x {p.STAND_SCREW_LENGTH.v:g} stand screws engage {layout.stand_screw_engagement():.1f} mm into heat-set inserts",
    ]


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--colourway", type=Path, default=colourway.DEFAULT_PATH, help="colour file (default: mechanical/colourway.toml)")
    parser.add_argument("--preset", help="preset to use instead of the file's `use` line")
    parser.add_argument("--out", type=Path, default=EXPORT_ROOT, help="export folder (default: mechanical/exports)")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    logging.getLogger("build123d").setLevel(logging.WARNING)

    colours = colourway.load(args.colourway, args.preset, part_names())
    built = build_all()
    summary = summary_lines(built)
    for line in summary:
        log.info(line)

    title = f"Tempo case rev {p.PRINT_REVISION} — colourway '{colours.preset}'"
    paths = export_model(coloured_model(built, colours), args.out, title)
    paths += export_parts(built.printed, colours, args.out)
    jobs = export_print_jobs(built.printed, colours, args.out)
    paths += [job.path for job in jobs]
    paths.append(write_print_plan(jobs, colours, summary, args.out))
    for path in paths:
        log.info("Wrote %s", path)


if __name__ == "__main__":
    main()
