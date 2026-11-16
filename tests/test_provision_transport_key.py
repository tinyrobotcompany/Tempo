import importlib.util, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('provision',ROOT/'scripts/provision-transport-key.py');provision=importlib.util.module_from_spec(spec);spec.loader.exec_module(provision)
class ProvisionTests(unittest.TestCase):
 def test_provision_local_pairing_and_key_validation(self):
  token='a'*32
  self.assertEqual(provision.destination('http://192.168.1.2/#token='+token),('http://192.168.1.2/api/credential',token))
  for bad in ['https://192.168.1.2/#token='+token,'https://github.com/#token='+token,'http://8.8.8.8/#token='+token,'http://user@192.168.1.2/#token='+token,'http://192.168.1.2/#token=x']:
   with self.assertRaises(ValueError):provision.destination(bad)
  with tempfile.TemporaryDirectory() as folder:
   env=Path(folder)/'.env';env.write_text('IDFM_PRIM_API_KEY=fixture')
   self.assertEqual(provision.key_from_env(env),'fixture')
   env.write_text('IDFM_PRIM_API_KEY=fixture\nIDFM_PRIM_API_KEY=duplicate')
   with self.assertRaises(ValueError):provision.key_from_env(env)
if __name__=='__main__':unittest.main()
