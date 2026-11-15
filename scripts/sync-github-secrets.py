#!/usr/bin/env python3
"""Send explicitly named CI secrets to Tempo through authenticated gh; never print values."""
import argparse, subprocess
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--env',type=Path,default=Path('.env'))
args=parser.parse_args()
values={}
for raw in args.env.read_text().splitlines():
    line=raw.strip()
    if not line or line.startswith('#') or '=' not in line:continue
    name,value=line.removeprefix('export ').split('=',1)
    name=name.strip()
    if name in ('OPENAI_API_KEY','REPO_ADMIN_TOKEN'):
        if name in values:raise SystemExit('Duplicate CI secret name in environment file.')
        values[name]=value.strip().strip('"\'')
if not values.get('OPENAI_API_KEY'):raise SystemExit('OPENAI_API_KEY is missing or empty.')
for name,value in values.items():
    if not value:continue
    subprocess.run(['gh','secret','set',name,'--repo','tinyrobotcompany/Tempo'],input=value,text=True,check=True)
    print(name+' configured; value withheld.')
