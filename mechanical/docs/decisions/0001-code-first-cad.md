# 0001 — build123d, concept A and colours in a config file

Date: 2026-11-15
Status: Proposed (awaiting owner review on `feature/enclosure-design`)

## Context

Tempo needs a printed case for the Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1 kit
([BOM](../../../docs/BOM.md)). Three concepts were mocked up in
`docs/design/enclosure/enclosure-mockups.html`. The owner chose concept A
(slab, coloured trim band, desk stand). They asked for the same CAD tools as
Waveform One and for colours to be customisable from a config file.

## Decision

- **Toolchain:** build123d (Python, OpenCascade) under `mechanical/cad/`, run
  with `uv`, tested with pytest. This matches Waveform One decision 0001: rebuilds,
  checks and exports run headless and reproducibly; Fusion is a STEP viewer.
- **Front:** no printed bezel. The panel's black glass border (16.5 mm) is the
  front face; a thin band lip in the accent colour outlines it.
- **Split parts:** the 250.2 mm glass forces a 255.6 mm band, wider than the
  245 mm print envelope. The band is printed in two halves, clamped by the four
  M2.5 corner screws. The shell is kept at 244 mm, inside the metal back
  outline, so it prints in one piece.
- **Colours:** `mechanical/colourway.toml` maps three zones (band, shell, stand)
  and optional single parts to a named filament and hex colour. Each part is
  one colour, so any single-filament printer can make every colourway; print
  jobs are grouped by filament.
- **Desk pose:** leaning back, the case rests on the band's rear-bottom edge and
  the stand's foot. The tilt is 13°, not the 15° in the mock-up, because at 15°
  the shell's bottom-rear edge would clear the desk by only 0.2 mm.

## Consequences

- The design depends on the manufacturer drawing until the received unit is
  measured (`reference/MEASUREMENTS.md`). The port zone and display mass are estimates.
- The stability check (≥ 4 N push to tip backwards) is only as good as `PANEL_MASS`.
- Hand edits in Fusion are lost on rebuild; port them back to code.
