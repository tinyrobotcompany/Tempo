"""Exercise the actual shell validator embedded in the PR workflow."""
import os
from pathlib import Path
import subprocess
import unittest

class BranchNames(unittest.TestCase):
    def test_actual_workflow_validator(self):
        workflow = (Path(__file__).resolve().parents[1] / '.github/workflows/pr-checks.yaml').read_text()
        block = workflow.split('        run: |\n', 1)[1].split('\n  pr-title:', 1)[0]
        script = '\n'.join(line[10:] for line in block.splitlines())
        cases = {
            'feat/external-power-management': True,
            'feature/external-power-management': True,
            'fix/parser-validation': True,
            'ci/checks': True,
            'dependabot/github_actions/actions/setup-node-7.1.0': True,
            'dependabot/npm_and_yarn/package-1.2.3': True,
            'main': False, 'unknown/name': False, 'feat/': False,
            'feat/Uppercase': False, 'feat/two words': False,
            'feature//invalid': False, 'feat/a/b/c': False,
            'dependabot/': False,
        }
        for branch, expected in cases.items():
            with self.subTest(branch=branch):
                result = subprocess.run(['bash', '-e', '-c', script],
                    env={**os.environ, 'BRANCH': branch}, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, expected, result.stdout + result.stderr)

if __name__ == '__main__':
    unittest.main()
