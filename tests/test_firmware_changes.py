import importlib.util, unittest, json, os, subprocess, sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('changes',Path(__file__).resolve().parents[1]/'.github/scripts/firmware-changes.py')
changes=importlib.util.module_from_spec(spec);spec.loader.exec_module(changes)
class FirmwareChangesTests(unittest.TestCase):
 def test_actual_build_inputs(self):
  for p in ['firmware/main/main.c','firmware/sdkconfig.defaults','firmware/CMakeLists.txt','firmware/main/web/app.js','firmware/partitions.csv','firmware/dependencies.lock','scripts/ota-release.py', 'scripts/generate_transport_config.py','companion/shortcut/Weather Station Location.shortcut']:
   self.assertTrue(changes.affects_firmware(p),p)
  for p in ['README.md','docs/BOM.md','tests/test_core.c','.github/workflows/ci.yaml','.changeset/fix.md','firmware/README.md']:
   self.assertFalse(changes.affects_firmware(p),p)
 def test_only_intentional_firmware_skip_can_pass(self):
  result={name:{'result':'success'} for name in ['changes','host-tests','changeset','firmware','infrastructure']}
  result['changes']['outputs']={'firmware':'true'};self.assertTrue(changes.gate(result))
  result['firmware']['result']='skipped';self.assertFalse(changes.gate(result))
  result['changes']['outputs']['firmware']='false';self.assertTrue(changes.gate(result))
  result['infrastructure']['result']='failure';self.assertFalse(changes.gate(result))
  result['infrastructure']['result']='success'
  result['host-tests']['result']='skipped';self.assertFalse(changes.gate(result))
  result['host-tests']['result']='success';result['changes']['result']='failure';self.assertFalse(changes.gate(result))
 def test_workflow_aggregate_entrypoint(self):
  root=Path(__file__).resolve().parents[1]
  workflow=(root/'.github/workflows/ci.yaml').read_text().split('  required:',1)[1]
  self.assertIn('if: always()',workflow)
  self.assertIn('CHECK_RESULTS: ${{ toJSON(needs) }}',workflow)
  self.assertIn('run: python3 .github/scripts/firmware-changes.py',workflow)
  for changed,firmware,expected in [('false','skipped',0),('false','failure',1),('true','skipped',1),('true','success',0)]:
   results={name:{'result':'success'} for name in ['changes','host-tests','changeset','infrastructure','firmware']}
   results['changes']['outputs']={'firmware':changed};results['firmware']['result']=firmware
   process=subprocess.run([sys.executable,str(root/'.github/scripts/firmware-changes.py')],env={**os.environ,'CHECK_RESULTS':json.dumps(results)},capture_output=True)
   self.assertEqual(process.returncode,expected)
if __name__=='__main__':unittest.main()
