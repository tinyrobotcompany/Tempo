#!/usr/bin/env python3
"""Refresh representative IANA time-zone cities from local public-domain zone.tab."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
import re,json
rows=[]
def deg(v):
    sign=-1 if v[0]=='-' else 1
    t=v[1:]; dd=len(t)-4 if len(t)>5 else len(t)-2
    return sign*(int(t[:dd])+int(t[dd:dd+2])/60+(int(t[dd+2:])/3600 if t[dd+2:] else 0))
for line in Path('/usr/share/zoneinfo/zone.tab').read_text().splitlines():
    if not line or line.startswith('#'):continue
    country,coords,zone,*_=line.split('\t'); lat,lon=re.findall('[+-][0-9]+',coords)
    name=zone.rsplit('/',1)[-1].replace('_',' ')+' · '+country
    # ASCII font coverage; country helps distinguish names.
    name=name.replace(' · ',' / ')
    rows.append((name,zone,f'{deg(lat):.6f},{deg(lon):.6f}'))
rows.sort()
(root/'firmware/components/station_core/cities.inc').write_text('/* IANA tzdb zone.tab, public domain; bundled from macOS zoneinfo.\n * Representative time-zone cities, not a street-level location database. */\nconst station_city_t station_cities[] = {\n'+''.join('    {'+', '.join(json.dumps(x) for x in row)+'},\n' for row in rows)+'};\nconst unsigned station_city_count=sizeof(station_cities)/sizeof(station_cities[0]);\n')
