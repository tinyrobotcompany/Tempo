import importlib.util
import subprocess
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'.github/scripts'/f'{name}.py')
    result = importlib.util.module_from_spec(spec); spec.loader.exec_module(result)
    return result
changes = module('infrastructure-changes')
firmware = module('firmware-changes')

class InfrastructureChangesTests(unittest.TestCase):
    def test_inputs_and_unrelated_files(self):
        for path in ('infra/certificates/main.bicep', 'infra/certificates/deployment.json',
                     'scripts/infrastructure/delivery.py', '.github/workflows/infrastructure-deploy.yaml',
                     '.github/workflows/ci.yaml', 'tests/test_infrastructure_delivery.py'):
            self.assertTrue(changes.affects_infrastructure(path), path)
        for path in ('firmware/main/main.c', 'README.md', 'docs/OTA.md', 'services/certificates/app.py',
                     '.github/workflows/ota-release.yml'):
            self.assertFalse(changes.affects_infrastructure(path), path)

    def test_full_push_range_includes_deleted_and_renamed_infrastructure(self):
        with tempfile.TemporaryDirectory() as directory:
            def git(*args):
                return subprocess.check_output(['git', '-C', directory, *args]).decode().strip()
            git('init', '-q'); git('config', 'user.email', 'test@example.invalid'); git('config', 'user.name', 'Test')
            path = Path(directory)/'infra/certificates/old.bicep'; path.parent.mkdir(parents=True); path.write_text('original')
            git('add', '.'); git('commit', '-qm', 'base'); base = git('rev-parse', 'HEAD')
            path.rename(Path(directory)/'unrelated.txt'); git('add', '-A'); git('commit', '-qm', 'rename out')
            head = git('rev-parse', 'HEAD')
            actual = subprocess.check_output
            with patch.object(changes.subprocess, 'check_output', side_effect=lambda args: actual(args, cwd=directory)):
                paths = changes.changed_paths({'before': base}, head)
                self.assertIn('infra/certificates/old.bicep', paths)
                self.assertTrue(any(map(changes.affects_infrastructure, paths)))
                initial = changes.changed_paths({'before': '0'*40}, base)
                self.assertIn('infra/certificates/old.bicep', initial)
                paths = changes.changed_paths({'pull_request': {'base': {'sha': base}, 'head': {'sha': head}}}, head)
                self.assertIn('infra/certificates/old.bicep', paths)

    def test_required_gate_only_accepts_intentional_skip(self):
        results = {name: {'result': 'success'} for name in ('changes','host-tests','changeset','certificates','infrastructure-changes')}
        results['changes']['outputs'] = {'firmware': 'false'}
        results['firmware'] = {'result': 'skipped'}
        for flag, outcome, accepted in [('false','skipped',True),('true','success',True),('true','skipped',False),('true','failure',False),('false','failure',False),('', 'skipped',False)]:
            results['infrastructure-changes']['outputs'] = {'infrastructure': flag}
            results['infrastructure'] = {'result': outcome}
            self.assertEqual(firmware.gate(results), accepted, (flag,outcome))
        results['infrastructure-changes']['result'] = 'failure'
        self.assertFalse(firmware.gate(results))

if __name__ == '__main__': unittest.main()
