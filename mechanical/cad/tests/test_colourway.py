import tomllib

import pytest

from tempo_case import colourway
from tempo_case.colourway import ColourwayError, parse

PARTS = frozenset({"band_left", "band_right", "shell", "stand", "calibration_coupon"})


def preset(**overrides):
    zones = {
        "band": {"filament": "PETG Teal", "hex": "#3fd8c4"},
        "shell": {"filament": "PETG Charcoal", "hex": "#22272D"},
        "stand": {"filament": "PETG Teal", "hex": "#3FD8C4"},
    }
    zones.update(overrides)
    return {"use": "a", "presets": {"a": zones}}


def test_shipped_file_loads_every_preset():
    data = tomllib.loads(colourway.DEFAULT_PATH.read_text(encoding="utf-8"))
    assert data["use"] in data["presets"]
    for name in data["presets"]:
        loaded = colourway.load(colourway.DEFAULT_PATH, name, PARTS)
        assert loaded.preset == name
        assert set(loaded.zones) == set(colourway.ZONES)


def test_hex_is_normalised_and_converted_to_a_colour():
    cw = parse(preset())
    teal = cw.zones["band"]
    assert teal.hex == "#3FD8C4"
    r, g, b, _ = tuple(teal.color)
    assert (r, g, b) == pytest.approx((0x3F / 255, 0xD8 / 255, 0xC4 / 255))


def test_same_colour_in_two_zones_is_one_filament():
    cw = parse(preset())
    assert cw.zones["band"] == cw.zones["stand"]


def test_part_override_beats_its_zone():
    data = preset(parts={"band_right": {"filament": "PETG White", "hex": "#F2F2F0"}})
    cw = parse(data, part_names=PARTS)
    assert cw.filament_for("band_right", "band").name == "PETG White"
    assert cw.filament_for("band_left", "band").name == "PETG Teal"


def test_preset_argument_overrides_use():
    data = preset()
    data["presets"]["b"] = dict(data["presets"]["a"])
    assert parse(data, "b").preset == "b"


@pytest.mark.parametrize(
    "data, message",
    [
        ({"presets": {}}, "No \\[presets"),
        ({"use": "nope", "presets": preset()["presets"]}, "not found"),
        (preset(band={"filament": "x", "hex": "teal"}), "not #RRGGBB"),
        (preset(band={"filament": "", "hex": "#000000"}), "empty"),
        (preset(band={"hex": "#000000"}), "must be"),
        (preset(lid={"filament": "x", "hex": "#000000"}), "unknown zones"),
        (preset(parts={"lid": {"filament": "x", "hex": "#000000"}}), "unknown parts"),
    ],
)
def test_bad_files_are_rejected_with_a_reason(data, message):
    with pytest.raises(ColourwayError, match=message):
        parse(data, part_names=PARTS)


def test_missing_zone_is_rejected():
    data = preset()
    del data["presets"]["a"]["stand"]
    with pytest.raises(ColourwayError, match="missing zones"):
        parse(data)


def test_missing_file_is_reported(tmp_path):
    with pytest.raises(ColourwayError, match="not found"):
        colourway.load(tmp_path / "none.toml")
