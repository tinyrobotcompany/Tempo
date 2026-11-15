#!/usr/bin/env python3
"""Check every shipped JS module appears in Node's report and clears 80%."""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]

def check(report, names):
    failures=[]
    for name in sorted(names):
        matches=re.findall(r'\b'+re.escape(name)+r'\s*\|\s*(\d+(?:\.\d+)?)\s*\|',report)
        if len(matches)!=1 or float(matches[0])<80:
            failures.append(name)
    if failures:
        raise ValueError('Missing or below 80% browser line coverage: '+', '.join(failures))

if __name__=='__main__':
    names={p.name for p in (ROOT/'firmware/main/web').glob('*.js')}
    names.update(p.name for p in (ROOT/'companion/location-helper').glob('*.js'))
    # location.js is byte-identical in the two surfaces (verified by existing tests).
    check((ROOT/'build-host/coverage/browser.txt').read_text(),names)
    print('All shipped browser modules meet the 80% executable-line coverage floor.')
