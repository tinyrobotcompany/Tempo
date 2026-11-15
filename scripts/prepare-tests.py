#!/usr/bin/env python3
"""Restore SHA256-pinned host dependencies without firmware credentials or ESP-IDF installation."""
import hashlib, json, os, shlex, shutil, subprocess, zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cache=Path(os.environ.get('TEMPO_TEST_CACHE', root/'build-host/dependencies')).resolve()
cache.mkdir(parents=True,exist_ok=True)
for dep in json.loads((root/'tests/dependencies.json').read_text()):
    blob=cache/(dep['sha256']+'.download')
    if not blob.exists():
        local=Path.home()/dep['local']
        if local.is_file(): shutil.copyfile(local,blob)
        else:
            temporary=blob.with_suffix('.partial')
            subprocess.run(['curl','--fail','--location','--silent','--show-error','--retry','3','--max-time','300','--proto','=https','--proto-redir','=https',dep['url'],'-o',str(temporary)],check=True)
            temporary.replace(blob)
    if hashlib.sha256(blob.read_bytes()).hexdigest()!=dep['sha256']:
        raise SystemExit('Dependency checksum mismatch: '+dep['name'])
    target=cache/dep['name']
    if dep['archive']:
        # Restore archive contents each time: altered extracted sources cannot mask a failing test.
        with zipfile.ZipFile(blob) as archive:
            for member in archive.infolist():
                if not (target/member.filename).resolve().is_relative_to(target.resolve()):
                    raise SystemExit('Unsafe archive member')
            archive.extractall(target)
    else: shutil.copyfile(blob,target)
(root/'build-host').mkdir(exist_ok=True)
variables={'LVGL_SOURCE_DIR':cache/'lvgl','CJSON_SOURCE_DIR':cache/'cjson/cJSON','IDF_EMBED_SCRIPT':cache/'embed.cmake'}
(root/'build-host/test-env.sh').write_text(''.join('export '+k+'='+shlex.quote(str(v))+'\n' for k,v in variables.items()))
print('Pinned host dependencies ready.')
