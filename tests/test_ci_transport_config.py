import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class CredentialBuildTests(unittest.TestCase):
 def test_firmware_never_reads_env_or_compiles_key(self):
  cmake=(ROOT/'firmware/main/CMakeLists.txt').read_text()
  self.assertNotIn('.env',cmake);self.assertNotIn('generate_transport_config',cmake)
  source=(ROOT/'firmware/main/transport_service.c').read_text()
  self.assertNotIn('STATION_PRIM_API_KEY',source);self.assertIn('station_credential_get',source)
  self.assertFalse((ROOT/'scripts/generate_transport_config.py').exists())
if __name__=='__main__':unittest.main()
