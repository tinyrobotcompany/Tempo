"""Read the case colours from mechanical/colourway.toml.

Colours never live in the geometry code: each printed part names a zone, and the
chosen preset maps zones (and optionally single parts) to a filament.
"""

import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from build123d import Color

DEFAULT_PATH = Path(__file__).resolve().parents[2] / "colourway.toml"
ZONES = ("band", "shell", "stand")
HEX = re.compile(r"^#[0-9A-Fa-f]{6}$")


class ColourwayError(ValueError):
    pass


@dataclass(frozen=True)
class Filament:
    name: str
    hex: str  # "#RRGGBB", upper case

    @property
    def color(self) -> Color:
        r, g, b = (int(self.hex[i : i + 2], 16) / 255 for i in (1, 3, 5))
        return Color(r, g, b)


@dataclass(frozen=True)
class Colourway:
    preset: str
    zones: dict[str, Filament]
    parts: dict[str, Filament] = field(default_factory=dict)

    def filament_for(self, part: str, zone: str) -> Filament:
        return self.parts.get(part) or self.zones[zone]


def _filament(where: str, raw: object) -> Filament:
    if not isinstance(raw, dict) or set(raw) != {"filament", "hex"}:
        raise ColourwayError(f"{where} must be {{ filament = \"…\", hex = \"#RRGGBB\" }}")
    name, hex_ = raw["filament"], raw["hex"]
    if not isinstance(name, str) or not name.strip():
        raise ColourwayError(f"{where}: filament name is empty")
    if not isinstance(hex_, str) or not HEX.match(hex_):
        raise ColourwayError(f"{where}: hex {hex_!r} is not #RRGGBB")
    return Filament(name.strip(), hex_.upper())


def parse(data: dict, preset: str | None = None, part_names: frozenset[str] | None = None) -> Colourway:
    presets = data.get("presets")
    if not isinstance(presets, dict) or not presets:
        raise ColourwayError("No [presets.<name>] tables found")
    chosen = preset or data.get("use")
    if chosen not in presets:
        raise ColourwayError(f"Preset {chosen!r} not found; choose one of: {', '.join(sorted(presets))}")
    table = presets[chosen]
    unknown = set(table) - set(ZONES) - {"parts"}
    if unknown:
        raise ColourwayError(f"Preset {chosen!r} has unknown zones {sorted(unknown)}; zones are {', '.join(ZONES)}")
    missing = [z for z in ZONES if z not in table]
    if missing:
        raise ColourwayError(f"Preset {chosen!r} is missing zones {missing}")
    zones = {z: _filament(f"presets.{chosen}.{z}", table[z]) for z in ZONES}
    parts_raw = table.get("parts", {})
    if not isinstance(parts_raw, dict):
        raise ColourwayError(f"presets.{chosen}.parts must be a table")
    if part_names is not None:
        stray = set(parts_raw) - part_names
        if stray:
            raise ColourwayError(f"presets.{chosen}.parts names unknown parts {sorted(stray)}; parts are {', '.join(sorted(part_names))}")
    parts = {name: _filament(f"presets.{chosen}.parts.{name}", raw) for name, raw in parts_raw.items()}
    return Colourway(chosen, zones, parts)


def load(path: Path = DEFAULT_PATH, preset: str | None = None, part_names: frozenset[str] | None = None) -> Colourway:
    try:
        data = tomllib.loads(Path(path).read_text(encoding="utf-8"))
    except FileNotFoundError as err:
        raise ColourwayError(f"Colourway file not found: {path}") from err
    except tomllib.TOMLDecodeError as err:
        raise ColourwayError(f"{path}: {err}") from err
    return parse(data, preset, part_names)
