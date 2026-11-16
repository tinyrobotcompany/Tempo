import importlib.util, json, subprocess, tempfile, unittest
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
  self.assertIn('needs: [validation, security]',workflow)
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
    git('tag','v0.9.1');git('checkout','--detach',first)
    with self.assertRaises(subprocess.CalledProcessError):ota.plan(root/'out')
   finally:ota.ROOT=old_root
if __name__=='__main__':unittest.main()
