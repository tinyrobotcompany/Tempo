#!/usr/bin/env python3
"""Verify large transport snapshots are linked into external BSS, not internal RAM."""
import json
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
build = root / 'firmware/build'
entry = json.loads((build / 'compile_commands.json').read_text())[0]
compiler = Path(shlex.split(entry['command'])[0])
objdump = compiler.with_name(compiler.name.removesuffix('gcc').removesuffix('g++') + 'objdump')
result = subprocess.run([str(objdump), '-t', str(build / 'weather_station.elf')],
                        check=True, capture_output=True, text=True)
required = {'search_result', 'line_result', 'station_search', 'line_map', 'summary_work', 'summary_pending', 'summary_targets'}
found = set()
size = 0
for line in result.stdout.splitlines():
    fields = line.split()
    if fields and fields[-1] in required:
        name = fields[-1]
        if not any('ext_ram' in field and 'bss' in field for field in fields):
            raise SystemExit(f'FAIL: {name} is not in external BSS: {line}')
        found.add(name)
        size += int(fields[-2], 16)
if found != required:
    raise SystemExit(f'FAIL: missing symbols: {sorted(required - found)}')
print(f'PASS: all required transport snapshots in external BSS; {size} bytes kept out of internal RAM.')
