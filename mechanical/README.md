# Tempo — mechanical

Code-first parametric CAD for the Tempo case, printed in PETG on an Elegoo
Centauri Carbon. Same toolchain as Waveform One: Python and
[build123d](https://build123d.readthedocs.io), with `uv` and pytest. Autodesk Fusion
(or any STEP viewer) is a viewer only. See [decision 0001](docs/decisions/0001-code-first-cad.md).

## Status

**Revision A1: slab with a coloured trim band and a desk stand** (concept A from
`docs/design/enclosure/enclosure-mockups.html`). Built and validated in code; nothing printed yet.

The display dimensions come from Waveshare's drawing
([reference/waveshare-10.1-dimensions.png](reference/waveshare-10.1-dimensions.png)),
not from the received unit. The port positions on the panel's right edge and the
display's mass are estimates. Work through
[reference/MEASUREMENTS.md](reference/MEASUREMENTS.md) and print the coupon before printing the large parts.

## Setup (once)

```sh
brew install uv
cd mechanical/cad
uv sync
```

## Build

```sh
cd mechanical/cad
uv run pytest                                    # geometry, fit, desk and stability checks
uv run python -m tempo_case.build                # uses the preset named in colourway.toml
uv run python -m tempo_case.build --preset sage  # try another preset once
```

The build stops with a clear message if a parameter is inconsistent, a part
enters the display or a keep-out, two parts collide, a part leaves the print
envelope, anything goes below the desk, or the case tips too easily. Outputs
(regenerated each run, not committed) go to `mechanical/exports/`:

- `step/tempo_case_revA1_assembly.step`: the whole case on the desk, in your colours (Z up)
- `previews/tempo_case_revA1_views.png`: four shaded views
- `3mf/` and `stl/`: every part on its own, already in print orientation
- `print_jobs/`: one ready-to-slice 3MF per plate, grouped by filament
- `print_plan.md`: which plate to print in which filament, plus the check results

## Colours

Colours are set in [`colourway.toml`](colourway.toml), never in the geometry code.
The case has three colour zones, each a separate single-filament print:

| Zone | Parts |
|---|---|
| `band` | `band_left`, `band_right`: the trim round the glass |
| `shell` | `shell`: the back (and the calibration coupon) |
| `stand` | `stand` |

Choose a preset with `use = "…"`, edit one, or add your own. A preset can also
give a single part its own colour (for example a two-tone band) under
`[presets.<name>.parts]`. The build assigns each part's colour to the STEP,
the preview and the 3MF display colour, and groups the print jobs by filament.
No multi-material printer is needed: change filament between plates.

## The design

```text
  side section (case upright)          desk pose: leans back 13°
  ┌─┐◄ lip (band colour, 1.6 mm)
  │ │ glass 3.2 ┐                             ╱│ panel + band
  │ │ metal back│ display kit, 12 mm         ╱ │
  │ │◄ band wall┘                          ╱ ┌─┘ shell
  │ ├──┐◄ band flange (clamped)           ╱ ╱│
  │ │  │ shell (body colour), 20 mm      ▔▔▔▔▔╲▁▁▁ stand foot
  └─┴──┘◄ stand screws here
```

- **Band (2 halves).** Wraps the glass edge: a 1.6 mm lip just ahead of the glass
  (the coloured outline you see from the front; it overlaps the black glass border
  by 1.5 mm), a 2.4 mm wall, and a rear flange. A window in the right-hand wall
  exposes the USB-C ports and buttons. The halves meet at the top and bottom
  centre. At 255.6 mm the band is too wide for one print; each half lies flange
  down on the bed and needs no supports.
- **Shell.** An open tray behind the panel, 244 mm wide so it fits the bed. Its rim
  presses the band's flange against the panel's metal back. Four
  **M2.5 × 10** screws go in from the back through counterbored columns into the
  panel's own corner holes. They engage 3 mm of the 4 mm threads, so the band is
  clamped with no fasteners of its own. Vents above and below the stand; four M3
  heat-set inserts for the stand. Prints back face down.
- **Stand.** A 150 mm wide hollow wedge on four **M3 × 10** screws, whose heads
  you reach through the hollow. The case rests on the band's rear-bottom edge and
  the stand's foot, which ends 120 mm behind it. Two seats under the foot take
  rubber feet. Prints on its end.

The build reports a stability figure: a horizontal push of about 6 N on the top
edge is needed to tip it backwards, using a 0.75 kg *estimate* for the display.
Weigh the display and update `PANEL_MASS`.

## Hardware

| Item | Qty | Where |
|---|---|---|
| M2.5 × 10 screws (cap or pan head, ≤ 5 mm head) | 4 | shell → panel corner holes |
| M3 heat-set inserts, ~5.7 mm long | 4 | shell back, for the stand |
| M3 × 10 screws | 4 | stand → shell |
| Stick-on rubber feet, ≤ 12 mm | 2 | under the stand's foot |
| Thin foam or silicone tape, ~0.5 mm | 1 strip | behind the band's lip, so the glass is never clamped |

## Assembly

1. Press the four heat-set inserts into the shell back from the outside.
2. Stick the thin pad tape behind each band half's lip.
3. Lay the display face down on a soft cloth. Fit both band halves round it; the
   window goes on the side with the ports.
4. Place the shell on the back, over the band's flange, and drive the four
   M2.5 × 10 screws through the shell's corner columns into the panel. Snug,
   not tight: the threads are only 4 mm deep.
5. Screw the stand on with the four M3 × 10 screws. Stick the rubber feet into their seats.
6. Plug USB-C in through the band's window.

The rubber feet raise the stand slightly; the tilt then reduces by about a
degree. If you'd rather not use feet, the seats are only 1 mm deep.

## Parameter trust levels

Every value in `cad/tempo_case/parameters.py` is tagged `VERIFIED`, `DATASHEET`,
`PROVISIONAL`, `MEASURED`, `CALIBRATED` or `POLICY`. Nothing is `VERIFIED` or
`MEASURED` yet. The insert and M3 hole sizes are from the Waveform One coupon
(same printer and PETG); the M2.5 hole is provisional until this project's coupon is printed.

## Layout

```text
mechanical/
├── colourway.toml          colour presets: the only place colours are set
├── cad/                    build123d source (pyproject.toml, uv.lock, tests/)
│   └── tempo_case/
│       ├── parameters.py   every dimension, with unit and trust status
│       ├── layout.py       derived datums, mounting points, desk pose
│       ├── panel.py        display stand-in and keep-outs
│       ├── enclosure/      band, shell, stand
│       ├── coupon.py       hole-size coupon
│       ├── colourway.py    reads colourway.toml
│       ├── stability.py    tipping forces in the desk pose
│       ├── validation.py   design rules, collisions, desk and print checks
│       ├── export.py       STEP, 3MF/STL, previews, print plan
│       └── build.py        entry point
├── docs/decisions/         design decisions
├── reference/              manufacturer drawing and measurement checklist
└── exports/                generated files only
```
