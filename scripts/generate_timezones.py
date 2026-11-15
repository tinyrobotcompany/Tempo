#!/usr/bin/env python3
"""Generate current/future POSIX rules from installed IANA TZif footers (public-domain data).
Run explicitly after OS tzdata updates; never part of an ordinary firmware build.
"""
from pathlib import Path
import json
import zoneinfo
root = Path(__file__).resolve().parents[1]
entries = {}
for name in sorted(zoneinfo.available_timezones()):
    for base in zoneinfo.TZPATH:
        path = Path(base) / name
        if not path.is_file():
            continue
        data = path.read_bytes()
        if data[:4] != b'TZif' or data[4:5] not in (b'2', b'3', b'4'):
            break
        rule = data.rsplit(b'\n', 2)[-2].decode('ascii')
        if rule and len(rule) < 96:
            entries[name] = rule
        break
out = ['/* Generated from installed IANA TZif footers; see scripts/generate_timezones.py. */',
       '#include "station_core.h"', '#include <string.h>',
       'const char *station_timezone_rule(const char *name) {',
       '    static const struct { const char *name, *rule; } zones[] = {']
for name, rule in entries.items():
    out.append(f'        {{{json.dumps(name)}, {json.dumps(rule)}}},')
out += ['    };', '    if (!name) return NULL;',
        '    for (unsigned i=0; i<sizeof(zones)/sizeof(zones[0]); ++i)',
        '        if (strcmp(name,zones[i].name)==0) return zones[i].rule;',
        '    return NULL;', '}']
(root/'firmware/components/station_core/timezones.c').write_text('\n'.join(out)+'\n')
print(f'Generated {len(entries)} time zones')
