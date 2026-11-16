import importlib.util, json, os, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ota_release',ROOT/'scripts/ota-release.py');ota=importlib.util.module_from_spec(spec);spec.loader.exec_module(ota)
class ReleaseTests(unittest.TestCase):
 def test_workflow_fails_closed_without_runtime(self):
  workflow=(ROOT/'.github/workflows/ota-release.yml').read_text()
  build=workflow.split('- name: Build credential-free release firmware',1)[1].split('- name: Sign and verify release manifest',1)[0]
  self.assertIn('test -f firmware/main/credentials.c',build)
  self.assertIn('test -f firmware/main/update_service.c',build)
  self.assertIn('test ! -f scripts/generate_transport_config.py',build)
  self.assertLess(build.index('test ! -f scripts/generate_transport_config.py'),build.index('idf.py -C firmware build'))
  self.assertNotIn('OTA_SIGNING_PRIVATE_KEY',build)
  self.assertIn('needs: [validation, security, prerequisites]',workflow)
  copy_commands='\n'.join(line.strip() for line in build.splitlines() if line.strip().startswith(('mkdir -p build-host/release','cp firmware/dependencies.lock')))
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);(root/'firmware').mkdir();(root/'firmware/dependencies.lock').write_text('fixture-lock')
   subprocess.run(['bash','-ec',copy_commands],cwd=root,check=True)
   self.assertEqual((root/'build-host/release/dependencies.lock').read_text(),'fixture-lock')
  upload=next(line for line in workflow.splitlines() if 'gh release upload' in line)
  for asset in ['tempo.bin','tempo-manifest.json','tempo-manifest.sig','SHA256SUMS','dependencies.lock','release.json','notes.md']:
   self.assertIn('build-host/release/'+asset,upload)


 def test_draft_assets_reject_missing_extra_and_stale_content(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);expected=root/'expected';actual=root/'actual';expected.mkdir();actual.mkdir()
   for name in ota.ASSETS:
    (expected/name).write_bytes(name.encode());(actual/name).write_bytes(name.encode())
   ota.verify_assets(expected,actual)
   (actual/'stale.bin').write_bytes(b'old')
   with self.assertRaises(ValueError):ota.verify_assets(expected,actual)
   (actual/'stale.bin').unlink();(actual/'notes.md').write_bytes(b'wrong notes')
   with self.assertRaises(ValueError):ota.verify_assets(expected,actual)
   (actual/'notes.md').unlink()
   with self.assertRaises(ValueError):ota.verify_assets(expected,actual)
  workflow=(ROOT/'.github/workflows/ota-release.yml').read_text()
  self.assertLess(workflow.index('gh release delete-asset'),workflow.index('gh release upload'))
  self.assertLess(workflow.index('verify-assets --downloaded'),workflow.index('gh release edit'))
  self.assertNotIn('--clobber',workflow)
  self.assertIn("--pattern '*'",workflow)
  self.assertNotIn('--archive',workflow)
 def test_publication_requires_remote_tag_at_exact_commit(self):
  workflow=(ROOT/'.github/workflows/ota-release.yml').read_text()
  commands=workflow.split('          tag="v$VERSION"',1)[1].split('          if draft=',1)[0]
  commands='tag="v$VERSION"\n'+commands
  self.assertIn('--verify-tag --draft',workflow)
  self.assertIn("Release exists without its expected git tag",workflow)
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);remote=root/'remote';work=root/'work'
   def run(*args,cwd=None):return subprocess.check_output(args,cwd=cwd,text=True,stderr=subprocess.DEVNULL).strip()
   run('git','init','--bare',str(remote));run('git','clone',str(remote),str(work))
   run('git','config','user.name','Test',cwd=work);run('git','config','user.email','test@example.com',cwd=work)
   (work/'file').write_text('first');run('git','add','.',cwd=work);run('git','commit','-qm','first',cwd=work);run('git','push','origin','HEAD',cwd=work)
   sha=run('git','rev-parse','HEAD',cwd=work)
   # Stub only GitHub's ref creation API; execute real fetch and commit checks.
   shell='gh(){ if [ "$1" = release ]; then return 1; fi; test "$1" = api; git -C "$TEST_REMOTE" update-ref "refs/tags/v1.0.0" "$GITHUB_SHA"; };\n'+commands
   env={**os.environ,'TEST_REMOTE':str(remote),'GITHUB_REPOSITORY':'test/repo','GITHUB_SHA':sha,'VERSION':'1.0.0'}
   orphan='gh(){ return 0; };\n'+commands
   self.assertNotEqual(subprocess.run(['bash','-ec',orphan],cwd=work,env=env,capture_output=True).returncode,0)
   subprocess.run(['bash','-ec',shell],cwd=work,env=env,check=True,capture_output=True)
   self.assertEqual(run('git','rev-parse','refs/tags/v1.0.0',cwd=remote),sha)
   subprocess.run(['bash','-ec',shell],cwd=work,env=env,check=True,capture_output=True)
   fresh=root/'fresh';run('git','clone',str(remote),str(fresh))
   self.assertFalse((fresh/'.git/FETCH_HEAD').exists())
   subprocess.run(['bash','-ec',shell],cwd=fresh,env=env,check=True,capture_output=True)
   (work/'file').write_text('different');run('git','commit','-qam','second',cwd=work)
   env['GITHUB_SHA']=run('git','rev-parse','HEAD',cwd=work)
   self.assertNotEqual(subprocess.run(['bash','-ec',shell],cwd=work,env=env,capture_output=True).returncode,0)
 def test_runtime_activation_waits_for_all_prerequisites(self):
  workflow=(ROOT/'.github/workflows/ota-release.yml').read_text()
  self.assertIn("if: needs.prerequisites.outputs.runtime_ready == 'true'",workflow)
  self.assertIn("'firmware/**'",workflow)
  script=workflow.split('        id: runtime',1)[1].split('        run: |',1)[1].split('  publish:',1)[0]
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);out=root/'output';summary=root/'summary'
   env={**os.environ,'GITHUB_OUTPUT':str(out),'GITHUB_STEP_SUMMARY':str(summary)}
   def ready():
    out.write_text('');subprocess.run(['bash','-ec',script],cwd=root,env=env,check=True)
    return out.read_text().strip()
   self.assertEqual(ready(),'ready=false')
   (root/'firmware/main').mkdir(parents=True);(root/'firmware/main/credentials.c').touch()
   self.assertEqual(ready(),'ready=false')
   (root/'firmware/main/update_service.c').touch();self.assertEqual(ready(),'ready=true')
   (root/'scripts').mkdir();(root/'scripts/generate_transport_config.py').touch()
   self.assertEqual(ready(),'ready=false');self.assertIn('publication inactive',summary.read_text())
 def test_signed_package_descriptor_hash_and_tampering(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);key=root/'private.pem';public=root/'public.pem';image=root/'app.bin';out=root/'release'
   subprocess.run(['openssl','genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-256','-out',str(key)],check=True,capture_output=True)
   subprocess.run(['openssl','pkey','-in',str(key),'-pubout','-out',str(public)],check=True,capture_output=True)
   binary=bytearray(1024);binary[0]=0xe9;binary[32:36]=(0xabcd5432).to_bytes(4,'little');binary[48:53]=b'1.0.0';binary[80:95]=b'weather_station';image.write_bytes(binary)
   ota.package(out,image,'1.0.0',key,public)
   manifest=json.loads((out/'tempo-manifest.json').read_text());self.assertEqual(manifest['size'],1024);self.assertEqual(manifest['version'],'1.0.0')
   with self.assertRaises(ValueError):ota.package(out,image,'1.0.1',key,public)
   (out/'tempo-manifest.json').write_text('{}')
   checked=subprocess.run(['openssl','dgst','-sha256','-verify',str(public),'-signature',str(out/'tempo-manifest.sig'),str(out/'tempo-manifest.json')],capture_output=True)
   self.assertNotEqual(checked.returncode,0)
   binary.extend(b'-----BEGIN PRIVATE KEY-----\n'+b'A'*80+b'\n-----END PRIVATE KEY-----');image.write_bytes(binary)
   with self.assertRaises(ValueError):ota.package(out,image,'1.0.0',key,public)
   image.write_bytes(b'bad')
   with self.assertRaises(ValueError):ota.package(out,image,'1.0.0',key,public)
 def test_version_plan_notes_idempotence_and_old_commit_rejection(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);(root/'firmware').mkdir();(root/'.changeset').mkdir()
   (root/'firmware/CMakeLists.txt').write_text('project(weather_station VERSION 0.8.16)')
   (root/'.changeset/first.md').write_text('---\n"tempo": minor\n---\nFirst OTA release.')
   def git(*args):return subprocess.check_output(['git',*args],cwd=root,text=True,stderr=subprocess.DEVNULL).strip()
   git('init','-q');git('config','user.name','Test');git('config','user.email','test@example.com');git('add','.');git('commit','-qm','baseline')
   old_root=ota.ROOT;ota.ROOT=root
   try:
    first=git('rev-parse','HEAD');self.assertEqual(ota.plan(root/'out'),'0.9.0')
    git('tag','v0.9.0');self.assertEqual(ota.plan(root/'out'),'0.9.0');self.assertIn('First OTA release.',(root/'out/notes.md').read_text())
    (root/'.changeset/second.md').write_text('---\n"tempo": patch\n---\nSecond change.')
    git('add','.changeset');git('commit','-qm','second');self.assertEqual(ota.plan(root/'out'),'0.9.1');self.assertNotIn('First OTA release.',(root/'out/notes.md').read_text())
    before=(root/'out/release.json').read_bytes();notes=(root/'out/notes.md').read_bytes()
    git('tag','v0.9.1');self.assertEqual(ota.plan(root/'out'),'0.9.1')
    self.assertEqual((root/'out/release.json').read_bytes(),before);self.assertEqual((root/'out/notes.md').read_bytes(),notes)
    metadata=json.loads(before);self.assertEqual(metadata['tag'],'v0.9.1');self.assertEqual(metadata['previous'],'v0.9.0')
    git('tag','v9.0.0',first) # Highest semantic tag is older than another production tag.
    git('checkout','--detach',first)
    with self.assertRaises(subprocess.CalledProcessError):ota.plan(root/'out')
   finally:ota.ROOT=old_root
if __name__=='__main__':unittest.main()
