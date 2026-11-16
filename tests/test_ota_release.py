import importlib.util, json, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ota_release',ROOT/'scripts/ota-release.py');ota=importlib.util.module_from_spec(spec);spec.loader.exec_module(ota)
spec=importlib.util.spec_from_file_location('provision',ROOT/'scripts/provision-transport-key.py');provision=importlib.util.module_from_spec(spec);spec.loader.exec_module(provision)
class ReleaseTests(unittest.TestCase):
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
 def test_provision_local_pairing_and_key_validation(self):
  token='a'*32
  self.assertEqual(provision.destination('http://192.168.1.2/#token='+token),('http://192.168.1.2/api/credential',token))
  for bad in ['https://github.com/#token='+token,'http://8.8.8.8/#token='+token,'http://user@192.168.1.2/#token='+token,'http://192.168.1.2/#token=x']:
   with self.assertRaises(ValueError):provision.destination(bad)
  with tempfile.TemporaryDirectory() as folder:
   env=Path(folder)/'.env';env.write_text('IDFM_PRIM_API_KEY=fixture')
   self.assertEqual(provision.key_from_env(env),'fixture')
   env.write_text('IDFM_PRIM_API_KEY=fixture\nIDFM_PRIM_API_KEY=duplicate')
   with self.assertRaises(ValueError):provision.key_from_env(env)
if __name__=='__main__':unittest.main()
