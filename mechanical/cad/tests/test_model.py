import math
import zipfile

import pytest
from build123d import Vertex, import_step

from tempo_case import colourway, layout
from tempo_case import parameters as p
from tempo_case.build import build_all, coloured_model, main, part_names
from tempo_case.export import export_print_jobs, step_path, write_3mf
from tempo_case.geometry import y_cylinder
from tempo_case.panel import port_zone_z
from tempo_case.validation import parameter_errors, print_envelope_violations


@pytest.fixture(scope="module")
def built():
    return build_all()


@pytest.fixture(scope="module")
def parts(built):
    return {pp.name: pp for pp in built.printed}


def test_parameters_are_consistent():
    assert parameter_errors() == []


def test_every_parameter_has_a_trust_level_and_note():
    for name, param in p.all_params().items():
        assert isinstance(param.status, p.Status), name
        assert param.note.strip(), name


def test_expected_parts_exist(parts):
    assert set(parts) == part_names()
    assert {parts[n].zone for n in parts} <= set(colourway.ZONES)


def test_every_part_fits_the_print_envelope(built):
    for part in built.printed:
        assert print_envelope_violations(part) == [], part.name


def test_band_wraps_the_glass_and_shell_stays_within_the_bed():
    width, height, _ = layout.band_outer_size()
    assert width > p.GLASS_W.v and height > p.GLASS_H.v
    assert width > p.MAX_PRINT_X.v, "a one-piece band would not fit the bed; it is split in two"
    shell_w, _, _ = layout.shell_outer_size()
    assert shell_w <= p.MAX_PRINT_X.v


def test_band_halves_are_mirror_images(parts):
    left, right = parts["band_left"].body.bounding_box(), parts["band_right"].body.bounding_box()
    assert left.max.X == pytest.approx(0, abs=1e-6) and right.min.X == pytest.approx(0, abs=1e-6)
    assert left.min.X == pytest.approx(-right.max.X, abs=1e-6)


def test_lip_leaves_the_visible_area_clear(parts):
    lip_z = layout.band_front_y() + p.LIP_THICKNESS.v / 2
    # A probe through the lip plane over the visible area's corner must not hit the band.
    x = -p.GLASS_W.v / 2 + p.ACTIVE_LEFT.v + 1
    z = layout.panel_centre_z() - p.GLASS_H.v / 2 + p.ACTIVE_BOTTOM.v + 1
    probe = y_cylinder(x, lip_z - 0.1, lip_z + 0.1, z, 0.5)
    assert (parts["band_left"].body & probe).volume == pytest.approx(0, abs=1e-6)


def test_port_window_is_open_over_the_port_zone(parts):
    low, high = port_zone_z()
    x = layout.band_outer_size()[0] / 2 - p.BAND_WALL.v / 2
    for z in (low, (low + high) / 2, high):
        probe = y_cylinder(x, p.GLASS_T.v + 0.5, p.PANEL_DEPTH.v - 0.5, z, 0.5)
        assert (parts["band_right"].body & probe).volume == pytest.approx(0, abs=1e-6)


def test_panel_screws_pass_through_band_and_shell(parts):
    for x, z in layout.mount_points():
        bore = y_cylinder(x, p.PANEL_DEPTH.v, layout.shell_back_y(), z, p.M25_CLEARANCE_HOLE.v - 0.2)
        band = parts["band_left" if x < 0 else "band_right"].body
        assert (band & bore).volume == pytest.approx(0, abs=1e-6)
        assert (parts["shell"].body & bore).volume == pytest.approx(0, abs=1e-6)


def test_flange_is_clamped_under_each_screw(parts):
    for x, z in layout.mount_points():
        ring = y_cylinder(x, p.PANEL_DEPTH.v, layout.flange_rear_y(), z, p.M25_CLEARANCE_HOLE.v + 2)
        band = parts["band_left" if x < 0 else "band_right"].body
        assert (band & ring).volume > 0.9 * (ring.volume - y_cylinder(x, 0, p.FLANGE_THICKNESS.v, z, p.M25_CLEARANCE_HOLE.v).volume)


def test_screw_engagement_respects_the_panel_thread_depth():
    assert p.MIN_THREAD_ENGAGEMENT.v <= layout.panel_screw_engagement() <= p.MOUNT_THREAD_DEPTH.v - p.THREAD_DEPTH_MARGIN.v


def test_desk_pose_rests_on_band_and_stand_only(built):
    desk = layout.desk_location()
    lowest = {pp.name: (desk * pp.body).bounding_box().min.Z for pp in built.enclosure}
    assert lowest["band_left"] == pytest.approx(0, abs=0.05)
    assert lowest["stand"] == pytest.approx(0, abs=0.05)
    assert lowest["shell"] >= p.MIN_DESK_CLEARANCE.v
    assert (desk * built.panel.body).bounding_box().min.Z > 0


def test_screen_leans_back_by_the_tilt():
    desk = layout.desk_location()
    zc, half = layout.panel_centre_z(), p.GLASS_H.v / 2
    top = (desk * Vertex(0, 0, zc + half)).center()
    bottom = (desk * Vertex(0, 0, zc - half)).center()
    assert top.Y > bottom.Y, "the top leans away from the viewer"
    assert math.degrees(math.atan2(top.Y - bottom.Y, top.Z - bottom.Z)) == pytest.approx(p.TILT_DEG.v, abs=1e-6)


def test_validation_rejects_a_screw_that_bottoms_out(monkeypatch):
    monkeypatch.setattr(p, "PANEL_SCREW_LENGTH", p.Param(12.0, "mm", p.Status.POLICY, "too long"))
    assert any("threads are 4 mm deep" in e for e in parameter_errors())


def test_stability_meets_the_push_limit(built):
    s = built.stability
    assert 0 < s.centre_y < s.rear_pivot_y
    assert s.tip_back_force_n >= p.MIN_TIP_FORCE.v
    assert s.mass_kg > p.PANEL_MASS.v


def test_exports_carry_configured_colours(tmp_path, built):
    data = {
        "use": "x",
        "presets": {
            "x": {
                "band": {"filament": "PETG Red", "hex": "#CC2222"},
                "shell": {"filament": "PETG Black", "hex": "#111111"},
                "stand": {"filament": "PETG Red", "hex": "#CC2222"},
                "parts": {"band_right": {"filament": "PETG White", "hex": "#F0F0F0"}},
            }
        },
    }
    colours = colourway.parse(data, part_names=part_names())
    model = coloured_model(built, colours)
    by_label = {child.label: child for child in model.children}
    r, g, b, _ = tuple(by_label["band_left"].color)
    assert (r, g, b) == pytest.approx((0xCC / 255, 0x22 / 255, 0x22 / 255), abs=1e-3)

    jobs = export_print_jobs(built.printed, colours, tmp_path)
    listed = [name for job in jobs for name in job.parts]
    assert sorted(listed) == sorted(part_names()), "every part is printed exactly once"
    by_part = {name: job.filament.name for job in jobs for name in job.parts}
    assert by_part["band_right"] == "PETG White" and by_part["band_left"] == "PETG Red"
    for job in jobs:
        with zipfile.ZipFile(job.path) as package:
            xml = package.read("3D/3dmodel.model").decode()
        assert f'displaycolor="{job.filament.hex}FF"' in xml


def test_3mf_is_a_valid_package(tmp_path, parts):
    path = tmp_path / "coupon.3mf"
    filament = colourway.Filament("PETG Grey", "#808080")
    write_3mf(path, [("calibration_coupon", filament, parts["calibration_coupon"].on_bed())])
    with zipfile.ZipFile(path) as package:
        assert {"[Content_Types].xml", "_rels/.rels", "3D/3dmodel.model"} <= set(package.namelist())
        assert "<triangle " in package.read("3D/3dmodel.model").decode()


def test_full_build_writes_everything_and_reimports(tmp_path):
    main(["--out", str(tmp_path), "--preset", "sage"])
    assert step_path(tmp_path).exists()
    assert (tmp_path / "print_plan.md").read_text(encoding="utf-8").startswith("# Tempo case print plan — colourway `sage`")
    assert len(list((tmp_path / "stl").glob("*.stl"))) == len(part_names())
    reimported = import_step(step_path(tmp_path))
    assert len(reimported.solids()) >= len(part_names())  # parts plus the display
