import os, subprocess, sys, tempfile, unittest
from pathlib import Path
class ConfigTests(unittest.TestCase):
 def test_ci_fixture_keeps_transport_enabled_without_secrets(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);(root/'scripts').mkdir();script=root/'scripts/generate_transport_config.py'
   script.write_text((Path(__file__).resolve().parents[1]/'scripts/generate_transport_config.py').read_text())
   out=root/'build/key.h';cmd=[sys.executable,str(script),'--output',str(out)]
   env={**os.environ,'TEMPO_CI_BUILD':'1'}
   subprocess.run(cmd,env=env,check=True);self.assertIn('tempo-ci-placeholder-not-a-credential',out.read_text())
   (root/'.env').write_text('IDFM_PRIM_API_KEY=test-only')
   result=subprocess.run(cmd,env=env,capture_output=True,text=True)
   self.assertNotEqual(result.returncode,0);self.assertNotIn('test-only',result.stderr)
if __name__=='__main__':unittest.main()
