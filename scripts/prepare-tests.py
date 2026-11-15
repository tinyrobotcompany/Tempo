#!/usr/bin/env python3
"""Restore pinned host dependencies; published and IDF-cache ZIP layouts differ."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def restore(dep, blob, target):
    actual = digest(blob)
    if actual not in {dep['sha256'], dep.get('local_sha256', dep['sha256'])}:
        raise ValueError('Dependency checksum mismatch: ' + dep['name'])
    if not dep['archive']:
        shutil.copyfile(blob, target)
        return
    prefix = dep.get('archive_prefix', '') if actual == dep['sha256'] else ''
    with zipfile.ZipFile(blob) as archive:
        entries = []
        for member in archive.infolist():
            if prefix and not member.filename.startswith(prefix):
                raise ValueError('Unexpected archive layout')
            name = member.filename[len(prefix):]
            if not name or member.is_dir():
                continue
            destination = target / name
            if not destination.resolve().is_relative_to(target.resolve()):
                raise ValueError('Unsafe archive member')
            entries.append((member, destination))
        # Clear only this generated dependency to prevent stale source files.
        if target.exists():
            shutil.rmtree(target)
        for member, destination in entries:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(archive.read(member))


def main():
    root = Path(__file__).resolve().parents[1]
    cache = Path(os.environ.get('TEMPO_TEST_CACHE', root/'build-host/dependencies')).resolve()
    cache.mkdir(parents=True, exist_ok=True)
    for dep in json.loads((root/'tests/dependencies.json').read_text()):
        blob = cache/(dep['sha256']+'.download')
        if not blob.exists():
            local = Path.home()/dep['local']
            if local.is_file() and digest(local) == dep.get('local_sha256', dep['sha256']):
                shutil.copyfile(local, blob)
            else:
                temporary = blob.with_suffix('.partial')
                subprocess.run(['curl','--fail','--location','--silent','--show-error','--retry','3','--max-time','300','--proto','=https','--proto-redir','=https',dep['url'],'-o',str(temporary)],check=True)
                if digest(temporary) != dep['sha256']:
                    raise ValueError('Downloaded dependency checksum mismatch: '+dep['name'])
                temporary.replace(blob)
        restore(dep, blob, cache/dep['name'])
    (root/'build-host').mkdir(exist_ok=True)
    variables={'LVGL_SOURCE_DIR':cache/'lvgl','CJSON_SOURCE_DIR':cache/'cjson/cJSON','IDF_EMBED_SCRIPT':cache/'embed.cmake'}
    (root/'build-host/test-env.sh').write_text(''.join('export '+k+'='+shlex.quote(str(v))+'\n' for k,v in variables.items()))
    print('Pinned host dependencies ready.')


if __name__ == '__main__':
    main()
