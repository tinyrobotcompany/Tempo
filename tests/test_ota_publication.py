"""Execute the production publication shell with only GitHub's remote API stubbed."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FAKE_GH = r'''#!/usr/bin/env python3
import os, shutil, subprocess, sys
from pathlib import Path
a=sys.argv[1:];root=Path(os.environ['TEST_RELEASE']);state=root/'state';assets=root/'assets'
if a[0]=='api':
    fields=dict(x.split('=',1) for x in a if x.startswith(('ref=','sha=')))
    subprocess.run(['git','-C',os.environ['TEST_REMOTE'],'update-ref',fields['ref'],fields['sha']],check=True)
    sys.exit(0)
assert a[0]=='release'
cmd=a[1]
if cmd=='view':
    if not state.exists():sys.exit(1)
    if '--json' in a:
        field=a[a.index('--json')+1]
        if field=='isDraft':print('true' if state.read_text()=='draft' else 'false')
        elif field=='assets':
            for p in assets.iterdir():print(p.name)
        else:raise AssertionError(field)
elif cmd=='create':
    assert '--verify-tag' in a and not state.exists()
    assets.mkdir(parents=True);state.write_text('draft')
elif cmd=='delete-asset':
    assert state.read_text()=='draft';(assets/a[3]).unlink()
elif cmd=='upload':
    assert state.read_text()=='draft'
    for name in a[3:]:shutil.copyfile(name,assets/Path(name).name)
elif cmd=='download':
    dest=Path(a[a.index('--dir')+1])
    for path in assets.iterdir():shutil.copyfile(path,dest/path.name)
    if os.environ.get('CORRUPT'):(dest/'tempo.bin').write_bytes(b'wrong')
elif cmd=='edit':
    assert '--draft=false' in a;state.write_text('published')
else:raise AssertionError(cmd)
'''

class PublicationTests(unittest.TestCase):
    def test_first_publish_retry_and_corruption_fail_closed(self):
        workflow=(ROOT/'.github/workflows/ota-release.yml').read_text()
        commands=workflow.split('      - name: Publish complete release',1)[1].split('        run: |',1)[1]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);remote=root/'remote';work=root/'work';bin_dir=root/'bin';bin_dir.mkdir()
            def git(*args,cwd=None):
                return subprocess.check_output(['git',*args],cwd=cwd,text=True,stderr=subprocess.DEVNULL).strip()
            git('init','--bare',str(remote));git('clone',str(remote),str(work))
            git('config','user.name','Test',cwd=work);git('config','user.email','test@example.com',cwd=work)
            (work/'scripts').mkdir()
            for name in ('ota-release.py','release.py'):shutil.copyfile(ROOT/'scripts'/name,work/'scripts'/name)
            git('add','.',cwd=work);git('commit','-qm','release source',cwd=work);git('push','origin','HEAD',cwd=work)
            release=work/'build-host/release';release.mkdir(parents=True)
            names={'tempo.bin','tempo-manifest.json','tempo-manifest.sig','SHA256SUMS','dependencies.lock','release.json','notes.md'}
            for name in names:(release/name).write_text(name)
            fake=bin_dir/'gh';fake.write_text(FAKE_GH);fake.chmod(0o755)
            storage=root/'release-api'
            env={**os.environ,'PATH':str(bin_dir)+os.pathsep+os.environ['PATH'],'TEST_REMOTE':str(remote),'TEST_RELEASE':str(storage),'GITHUB_SHA':git('rev-parse','HEAD',cwd=work),'GITHUB_REPOSITORY':'test/repo','VERSION':'1.0.0'}
            def publish():return subprocess.run(['bash','-eo','pipefail','-c',commands],cwd=work,env=env,capture_output=True,text=True)
            first=publish();self.assertEqual(first.returncode,0,first.stderr)
            self.assertEqual((storage/'state').read_text(),'published')
            self.assertEqual({p.name for p in (storage/'assets').iterdir()},names)
            self.assertEqual(publish().returncode,0) # Published retry does not replace assets.
            (storage/'state').write_text('draft');(storage/'assets/stale.bin').write_text('stale')
            env['CORRUPT']='1';self.assertNotEqual(publish().returncode,0)
            self.assertEqual((storage/'state').read_text(),'draft')
            del env['CORRUPT'];retry=publish();self.assertEqual(retry.returncode,0,retry.stderr)
            self.assertEqual((storage/'state').read_text(),'published')
            self.assertEqual({p.name for p in (storage/'assets').iterdir()},names)

if __name__=='__main__':unittest.main()
