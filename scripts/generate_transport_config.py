#!/usr/bin/env python3
"""Generate a build-local transport credential header; never print the key."""
import argparse
import json
import os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',required=True);args=p.parse_args()
root=Path(__file__).resolve().parents[1];source=root/'.env';key='';count=0
if source.exists():
 for line in source.read_text().splitlines():
  line=line.strip()
  if not line or line.startswith('#') or '=' not in line:continue
  name,value=line.split('=',1)
  if name.strip().removeprefix('export ')=='IDFM_PRIM_API_KEY':count+=1;key=value.strip().strip('\"\'')
if count>1 or len(key)>512 or any(ord(c)<33 or ord(c)>126 for c in key):raise SystemExit('Invalid transport API key configuration (value withheld).')
text='#pragma once\n#define STATION_PRIM_API_KEY '+json.dumps(key)+'\n'
out=Path(args.output);out.parent.mkdir(parents=True,exist_ok=True)
if not out.exists() or out.read_text()!=text:
 fd=os.open(out,os.O_CREAT|os.O_TRUNC|os.O_WRONLY,0o600)
 with os.fdopen(fd,'w') as f:f.write(text)
out.chmod(0o600)
