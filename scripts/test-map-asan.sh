#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 - <<'PYCODE'
from pathlib import Path
import shlex,subprocess
base=Path('build-host/ui').resolve()
flags=(base/'CMakeFiles/test_map_tiles.dir/flags.make').read_text()
args=[]
for key in ('C_DEFINES','C_INCLUDES','C_FLAGS'):
    line=next(l for l in flags.splitlines() if l.startswith(key+' ='))
    args+=shlex.split(line.split('=',1)[1])
subprocess.run(['cc','-g','-fsanitize=address,undefined']+args+['tests/test_map_tiles.c',str(base/'lvgl/lib/liblvgl.a'),'-lm','-o','build-host/test-map-asan'],check=True)
subprocess.run(['build-host/test-map-asan'],check=True)
PYCODE
