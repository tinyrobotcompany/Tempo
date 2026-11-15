"""Write generated geometry to mechanical/exports/.

Generated files are never written next to source code. Filenames are stable so
a rebuild overwrites the previous output instead of accumulating copies.
"""

import re
import zipfile
from dataclasses import dataclass
from pathlib import Path
from xml.sax.saxutils import quoteattr

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from build123d import Compound, Location, Pos, Shape, export_step, export_stl
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

from tempo_case import parameters as p
from tempo_case.colourway import Colourway, Filament
from tempo_case.geometry import PrintedPart

EXPORT_ROOT = Path(__file__).resolve().parents[2] / "exports"
STEM = f"tempo_case_rev{p.PRINT_REVISION}"
MESH_TOLERANCE = 0.01  # mm; well below the 0.4 mm nozzle
ANGULAR_TOLERANCE = 0.1  # radians
WELD_DECIMALS = 5

# (title, elevation, azimuth) in matplotlib's camera terms; the desk pose has the viewer at -Y.
VIEWS = (("Front-left", 18, -125), ("Front", 4, -90), ("Right side", 0, 0), ("Rear-right", 20, 50))
LIGHT = np.array([-0.4, -0.7, 0.6]) / np.linalg.norm([-0.4, -0.7, 0.6])


def step_path(root: Path) -> Path:
    return root / "step" / f"{STEM}_assembly.step"


def preview_path(root: Path) -> Path:
    return root / "previews" / f"{STEM}_views.png"


def _leaves(shape: Shape) -> list[Shape]:
    children = list(getattr(shape, "children", ()))
    return [shape] if not children else [leaf for child in children for leaf in _leaves(child)]


def render_views(model: Compound, path: Path, title: str) -> None:
    polygons, colours = [], []
    for leaf in _leaves(model):
        if leaf.label.startswith("KeepOut_"):
            continue
        vertices, triangles = leaf.tessellate(0.5)
        points = np.array([tuple(v) for v in vertices])
        rgba = tuple(leaf.color) if leaf.color else (0.6, 0.6, 0.6, 1.0)
        for tri in triangles:
            polygon = points[list(tri)]
            normal = np.cross(polygon[1] - polygon[0], polygon[2] - polygon[0])
            length = np.linalg.norm(normal)
            shade = 0.5 + 0.5 * abs(normal @ LIGHT / length) if length else 1.0
            polygons.append(polygon)
            colours.append((*(np.clip(np.array(rgba[:3]) * shade, 0, 1)), 1.0))

    box = model.bounding_box()
    fig = plt.figure(figsize=(14, 11))
    for index, (name, elev, azim) in enumerate(VIEWS, start=1):
        ax = fig.add_subplot(2, 2, index, projection="3d")
        ax.add_collection3d(Poly3DCollection(polygons, facecolors=colours, edgecolors="none"))
        ax.set_xlim(box.min.X, box.max.X)
        ax.set_ylim(box.min.Y, box.max.Y)
        ax.set_zlim(box.min.Z, box.max.Z)
        ax.set_box_aspect((box.size.X, box.size.Y, box.size.Z))
        ax.set_proj_type("ortho")
        ax.view_init(elev=elev, azim=azim)
        ax.set_title(name)
        ax.set_axis_off()
    fig.suptitle(title)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


def export_model(model: Compound, root: Path, title: str) -> list[Path]:
    path = step_path(root)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not export_step(model, path):
        raise RuntimeError(f"STEP export failed: {path}")
    png = preview_path(root)
    png.parent.mkdir(parents=True, exist_ok=True)
    render_views(model, png, title)
    return [path, png]


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def part_stem(part: PrintedPart) -> str:
    return f"{part.name}_rev{p.PRINT_REVISION}_{p.PRINT_MATERIAL}"


def _mesh_xml(shape: Shape) -> str:
    """A closed triangle mesh with shared edge points welded, as slicers require."""
    vertices, triangles = shape.tessellate(MESH_TOLERANCE, ANGULAR_TOLERANCE)
    index_of, points, remap = {}, [], []
    for v in vertices:
        key = (round(v.X, WELD_DECIMALS), round(v.Y, WELD_DECIMALS), round(v.Z, WELD_DECIMALS))
        if key not in index_of:
            index_of[key] = len(points)
            points.append(key)
        remap.append(index_of[key])
    faces = []
    for a, b, c in triangles:
        a, b, c = remap[a], remap[b], remap[c]
        if a != b and b != c and a != c:
            faces.append(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>')
    vertex_xml = "".join(f'<vertex x="{x:.5f}" y="{y:.5f}" z="{z:.5f}"/>' for x, y, z in points)
    return f"<mesh><vertices>{vertex_xml}</vertices><triangles>{''.join(faces)}</triangles></mesh>"


def write_3mf(path: Path, objects: list[tuple[str, Filament, Shape]]) -> None:
    """A 3MF with one object per part, each tagged with its filament colour.

    Colours go in a 3MF <basematerials> group (display colour) and every object
    uses filament slot 1, since each print job is a single filament.
    """
    filaments = list(dict.fromkeys(f for _, f, _ in objects))
    bases = "".join(f"<base name={quoteattr(f.name)} displaycolor=\"{f.hex}FF\"/>" for f in filaments)
    resources, items, settings = [f'<basematerials id="1">{bases}</basematerials>'], [], []
    for index, (name, filament, shape) in enumerate(objects, start=2):
        resources.append(
            f'<object id="{index}" type="model" name={quoteattr(name)} pid="1" pindex="{filaments.index(filament)}">{_mesh_xml(shape)}</object>'
        )
        items.append(f'<item objectid="{index}"/>')
        settings.append(f'<object id="{index}"><metadata key="name" value={quoteattr(name)}/><metadata key="extruder" value="1"/></object>')
    model = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
        '<metadata name="Application">Tempo case CAD (build123d)</metadata>'
        f'<resources>{"".join(resources)}</resources><build>{"".join(items)}</build></model>'
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as package:
        package.writestr(
            "[Content_Types].xml",
            '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
            '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
            '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
            '<Default Extension="config" ContentType="text/xml"/></Types>',
        )
        package.writestr(
            "_rels/.rels",
            '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>'
            "</Relationships>",
        )
        package.writestr("3D/3dmodel.model", model)
        package.writestr("Metadata/model_settings.config", f'<?xml version="1.0" encoding="UTF-8"?>\n<config>{"".join(settings)}</config>')


def export_parts(parts: list[PrintedPart], colours: Colourway, root: Path) -> list[Path]:
    """3MF and STL per part, already in print orientation and sitting on the bed."""
    written = []
    for part in parts:
        stem = part_stem(part)
        mf, stl = root / "3mf" / f"{stem}.3mf", root / "stl" / f"{stem}.stl"
        stl.parent.mkdir(parents=True, exist_ok=True)
        write_3mf(mf, [(part.name, colours.filament_for(part.name, part.zone), part.on_bed())])
        if not export_stl(part.on_bed(), stl, tolerance=MESH_TOLERANCE):
            raise RuntimeError(f"STL export failed: {stl}")
        written += [mf, stl]
    return written


def pack_plates(parts: list[PrintedPart]) -> list[list[tuple[PrintedPart, Location]]]:
    """Lay parts out in rows on as few plates as needed, each plate centred on the bed origin."""
    gap, max_x, max_y = p.PLATE_GAP.v, p.MAX_PRINT_X.v, p.PLATE_DEPTH.v
    plates: list[list[tuple[PrintedPart, float, float]]] = [[]]
    x = y = row_depth = 0.0
    for part in parts:
        size = part.on_bed().bounding_box().size
        if size.X > max_x or size.Y > max_y:
            raise RuntimeError(f"{part.name} ({size.X:.0f} x {size.Y:.0f} mm) does not fit a plate")
        if plates[-1] and x + size.X > max_x:
            x, y, row_depth = 0.0, y + row_depth + gap, 0.0
        if plates[-1] and y + size.Y > max_y:
            plates.append([])
            x = y = row_depth = 0.0
        plates[-1].append((part, x, y))
        x += size.X + gap
        row_depth = max(row_depth, size.Y)

    result = []
    for plate in plates:
        placed = []
        for part, px, py in plate:
            box = part.on_bed().bounding_box()
            placed.append((part, Pos(px - box.min.X, py - box.min.Y, 0)))
        total = Compound(children=[shift * part.on_bed() for part, shift in placed]).bounding_box()
        centre = Pos(-total.center().X, -total.center().Y, 0)
        result.append([(part, centre * shift * part.bed_location()) for part, shift in placed])
    return result


@dataclass(frozen=True)
class PrintJob:
    path: Path
    filament: Filament
    parts: tuple[str, ...]
    volume_cm3: float


def export_print_jobs(parts: list[PrintedPart], colours: Colourway, root: Path) -> list[PrintJob]:
    """One 3MF per plate: the coupon first, then the parts grouped by filament."""
    coupon = [pp for pp in parts if pp.name == "calibration_coupon"]
    rest = [pp for pp in parts if pp.name != "calibration_coupon"]
    groups: dict[Filament, list[PrintedPart]] = {}
    for part in rest:
        groups.setdefault(colours.filament_for(part.name, part.zone), []).append(part)

    batches = [("calibration-coupon", colours.filament_for(pp.name, pp.zone), [pp]) for pp in coupon]
    for filament, members in groups.items():
        plates = pack_plates(members)
        for n, plate in enumerate(plates, start=1):
            suffix = f"-plate-{n}" if len(plates) > 1 else ""
            batches.append((f"{slug(filament.name)}{suffix}", filament, [pp for pp, _ in plate]))

    jobs = []
    for index, (name, filament, members) in enumerate(batches, start=1):
        path = root / "print_jobs" / f"{index}_{name}_rev{p.PRINT_REVISION}.3mf"
        objects = [(pp.name, filament, loc * pp.body) for plate in pack_plates(members) for pp, loc in plate]
        write_3mf(path, objects)
        jobs.append(PrintJob(path, filament, tuple(pp.name for pp in members), sum(pp.body.volume for pp in members) / 1000))
    return jobs


def write_print_plan(jobs: list[PrintJob], colours: Colourway, summary: list[str], root: Path) -> Path:
    """A Markdown checklist of plates and filaments for the chosen colourway."""
    lines = [
        f"# Tempo case print plan — colourway `{colours.preset}`",
        "",
        "Generated by `uv run python -m tempo_case.build`; do not edit.",
        "",
        "| # | File | Filament | Colour | Parts | Solid volume |",
        "|---|---|---|---|---|---|",
    ]
    for index, job in enumerate(jobs, start=1):
        lines.append(f"| {index} | `{job.path.name}` | {job.filament.name} | `{job.filament.hex}` | {', '.join(job.parts)} | {job.volume_cm3:.0f} cm³ |")
    lines += ["", "Solid volume is before infill; the slicer gives the real filament use.", "", "## Checks", ""]
    lines += [f"- {line}" for line in summary]
    path = root / "print_plan.md"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path
