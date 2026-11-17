#!/usr/bin/env python3
"""Plan and package immutable OTA releases from reviewed commits; never read .env."""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
from importlib.util import spec_from_file_location, module_from_spec
ROOT = Path(__file__).resolve().parents[1]
spec = spec_from_file_location('release', ROOT/'scripts/release.py')
release = module_from_spec(spec); spec.loader.exec_module(release)
TARGET = 'waveshare-p4-10.1-rev3-32mb'
LAYOUT = 'ab-8mb-v1'
def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()
def plan(destination):
    head = git('rev-parse', 'HEAD')
    tags = [t for t in git('tag','--list','v*').splitlines() if re.fullmatch(r'v\d+\.\d+\.\d+', t)]
    latest_tag = max(tags, key=lambda t: release.version_tuple(t[1:])) if tags else None
    for tag in tags:
        subprocess.run(['git','merge-base','--is-ancestor',tag,'HEAD'],cwd=ROOT,check=True)
    head_tags = [tag for tag in tags if git('rev-parse',tag+'^{commit}') == head]
    retry_tag = max(head_tags,key=lambda tag:release.version_tuple(tag[1:])) if head_tags else None
    if retry_tag is not None and retry_tag != latest_tag:
        raise ValueError('Tagged source has an older version than an existing production release')
    predecessor_tags = [tag for tag in tags if tag != retry_tag]
    notes_base_tag = max(predecessor_tags,key=lambda tag:release.version_tuple(tag[1:])) if predecessor_tags else None
    # A retry uses the same predecessor range as the original allocation.
    notes = release.pending(ROOT)
    if notes_base_tag is not None:
        changed = set(git('diff','--name-only',notes_base_tag,'HEAD','--','.changeset').splitlines())
        notes = [note for note in notes if str(note[0].relative_to(ROOT)) in changed]
    if retry_tag is not None:
        allocated_tag = retry_tag
        version = allocated_tag[1:]
    else:
        base = max(release.version_tuple(release.current(ROOT)[2]), release.version_tuple(latest_tag[1:]) if latest_tag else (0,0,0))
        bump = max((note[1] for note in notes),key=['patch','minor','major'].index,default='patch')
        a,b,c=base
        version = f'{a+1}.0.0' if bump=='major' else f'{a}.{b+1}.0' if bump=='minor' else f'{a}.{b}.{c+1}'
        allocated_tag = 'v'+version
    release.version_tuple(version)
    body=f'# Tempo {version}\n\nSource: `{head}`\n\n'+('\n\n'.join(n[2] for n in notes) or 'Validated firmware and dependency updates.')
    body+='\n\n## Installation and compatibility\n\nWaveshare ESP32-P4 10.1-inch, revision 3, 32 MB flash. Existing A/B 8 MB partition layout only. Initial OTA bootstrap requires USB. Credentials and settings remain on the device.\n\nAutomated tests/build/security gates passed before publication. Physical hardware acceptance is recorded separately; CI does not claim a hardware test.\n'
    destination.mkdir(parents=True,exist_ok=True)
    # previous is the predecessor used for release notes, never this release's own tag.
    (destination/'release.json').write_text(json.dumps({'version':version,'tag':allocated_tag,'commit':head,'previous':notes_base_tag},indent=2)+'\n')
    (destination/'notes.md').write_text(body)
    return version

def package(destination, image, version, key, public_key):
    release.version_tuple(version)
    blob=image.read_bytes()
    if not 1024<=len(blob)<=0x800000 or blob[0]!=0xe9:
        raise ValueError('Invalid ESP application image or OTA slot overflow')
    # ESP-IDF app descriptor is first segment data: magic + secure version + reserved,
    # then 32-byte version and 32-byte project name. Reject packaging another build.
    if int.from_bytes(blob[32:36],'little')!=0xabcd5432 or blob[48:80].split(b'\0')[0].decode()!=version or blob[80:112].split(b'\0')[0]!=b'weather_station':
        raise ValueError('Application descriptor/version does not match release')
    if re.search(rb'-----BEGIN (?:EC |RSA )?PRIVATE KEY-----[\r\n]+[A-Za-z0-9+/=\r\n]{64,}-----END (?:EC |RSA )?PRIVATE KEY-----',blob):
        raise ValueError('Private key material in release image')
    for forbidden in (b'tempo-ci-placeholder-not-a-credential',b'STATION_PRIM_API_KEY'):
        if forbidden in blob:raise ValueError('Forbidden credential/build marker in release image')
    manifest={'schema':1,'source_commit':git('rev-parse','HEAD'),'dependency_lock_sha256':hashlib.sha256((ROOT/'firmware/dependencies.lock').read_bytes()).hexdigest(),'version':version,'target':TARGET,'layout':LAYOUT,'size':len(blob),
              'sha256':hashlib.sha256(blob).hexdigest(),
              'url':f'https://github.com/tinyrobotcompany/Tempo/releases/download/v{version}/tempo.bin'}
    destination.mkdir(parents=True,exist_ok=True)
    (destination/'tempo.bin').write_bytes(blob)
    path=destination/'tempo-manifest.json';path.write_text(json.dumps(manifest,sort_keys=True,separators=(',',':'))+'\n')
    signature=destination/'tempo-manifest.sig'
    subprocess.run(['openssl','dgst','-sha256','-sign',str(key),'-out',str(signature),str(path)],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['openssl','dgst','-sha256','-verify',str(public_key),'-signature',str(signature),str(path)],check=True,stdout=subprocess.DEVNULL)
    (destination/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256((destination/n).read_bytes()).hexdigest()}  {n}\n' for n in ['tempo.bin','tempo-manifest.json','tempo-manifest.sig']))

ASSETS = ('tempo.bin','tempo-manifest.json','tempo-manifest.sig','SHA256SUMS','dependencies.lock','release.json','notes.md')
def verify_assets(expected, downloaded):
    if set(p.name for p in downloaded.iterdir()) != set(ASSETS):
        raise ValueError('Draft asset set does not match release')
    for name in ASSETS:
        actual=downloaded/name
        if actual.is_symlink() or not actual.is_file() or hashlib.sha256(actual.read_bytes()).digest()!=hashlib.sha256((expected/name).read_bytes()).digest():
            raise ValueError('Draft asset differs from validated local artifact: '+name)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['plan','package','verify-assets']);p.add_argument('--output',type=Path,default=ROOT/'build-host/release');p.add_argument('--downloaded',type=Path);p.add_argument('--image',type=Path);p.add_argument('--version');p.add_argument('--key',type=Path);a=p.parse_args()
    if a.action=='plan':print(plan(a.output))
    elif a.action=='verify-assets':
        if not a.downloaded:p.error('verify-assets requires --downloaded')
        verify_assets(a.output,a.downloaded)
    else:
        if not all([a.image,a.version,a.key]):p.error('package requires --image, --version and --key')
        package(a.output,a.image,a.version,a.key,ROOT/'firmware/main/ota_public.pem')
