"""Regression coverage for content-based release notes and dependency PR names."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CHECK = ROOT / '.github/scripts/changeset-check.sh'
OLD, NEW = 'a' * 40, 'b' * 40
WORKFLOW = f'''name: Test
on: push
permissions:
  contents: read
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@{OLD} # v6.1.0
        with:
          persist-credentials: false
      - run: echo tested
'''


class ChangesetPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.git('init', '-q')
        self.git('config', 'user.email', 'test@example.com')
        self.git('config', 'user.name', 'test')
        self.git('branch', '-m', 'main')
        self.write('.github/workflows/ci.yaml', WORKFLOW)
        self.write('firmware/code.c', 'int value = 1;\n')
        self.write('.changeset/existing.md', 'Existing release note\n')
        self.commit()
        self.git('checkout', '-qb', 'dependabot/github_actions/github-actions-123abc')

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.repo, stderr=subprocess.STDOUT)

    def write(self, name, text):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def commit(self):
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture')

    def check(self, actor='simonholmes001'):
        self.commit()
        env = {**os.environ, 'GITHUB_EVENT_NAME': 'pull_request',
               'GITHUB_ACTOR': actor, 'GITHUB_BASE_REF': 'main',
               'GITHUB_HEAD_REF': 'dependabot/github_actions/github-actions-123abc',
               'GITHUB_REF_NAME': '123/merge'}
        return subprocess.run(['bash', str(CHECK)], cwd=self.repo, env=env,
                              capture_output=True, text=True)

    def assert_result(self, expected, actor='simonholmes001'):
        result = self.check(actor)
        self.assertEqual(result.returncode == 0, expected, result.stdout + result.stderr)

    def test_action_pin_update_after_human_edit_passes(self):
        self.write('.github/workflows/ci.yaml', WORKFLOW.replace(OLD, NEW).replace('v6.1.0', 'v7.0.1'))
        self.assert_result(True)

    def test_bot_pin_update_passes(self):
        self.write('.github/workflows/ci.yaml', WORKFLOW.replace(OLD, NEW))
        self.assert_result(True, 'dependabot[bot]')

    def test_grouped_updates_across_workflows_pass(self):
        self.write('.github/workflows/security.yml', WORKFLOW)
        self.commit()
        self.git('branch', '-f', 'main', 'HEAD')
        for name in ['ci.yaml', 'security.yml']:
            self.write('.github/workflows/' + name, WORKFLOW.replace(OLD, NEW))
        self.assert_result(True)

    def test_application_change_on_bot_branch_requires_note(self):
        self.write('firmware/code.c', 'int value = 2;\n')
        self.assert_result(False)

    def test_bot_actor_does_not_bypass_application_note(self):
        self.write('firmware/code.c', 'int value = 2;\n')
        self.assert_result(False, 'dependabot[bot]')

    def test_mixed_action_and_application_changes_require_note(self):
        self.write('.github/workflows/ci.yaml', WORKFLOW.replace(OLD, NEW))
        self.write('firmware/code.c', 'int value = 2;\n')
        self.assert_result(False)

    def test_workflow_permissions_inputs_and_commands_require_notes(self):
        for before, after in [('contents: read', 'contents: write'),
                              ('persist-credentials: false', 'persist-credentials: true'),
                              ('echo tested', 'echo changed'),
                              ('actions/checkout', 'someone/checkout')]:
            with self.subTest(change=after):
                self.write('.github/workflows/ci.yaml', WORKFLOW.replace(OLD, NEW).replace(before, after))
                self.assert_result(False)

    def test_mutable_action_tag_requires_note(self):
        self.write('.github/workflows/ci.yaml', WORKFLOW.replace(OLD, 'v7'))
        self.assert_result(False)

    def test_deleted_application_file_requires_note(self):
        (self.repo / 'firmware/code.c').unlink()
        self.assert_result(False)

    def test_deleted_note_does_not_satisfy_application_change(self):
        (self.repo / '.changeset/existing.md').unlink()
        self.write('firmware/code.c', 'int value = 2;\n')
        self.assert_result(False)

    def test_new_workflow_requires_note(self):
        self.write('.github/workflows/new.yaml', WORKFLOW.replace(OLD, NEW))
        self.assert_result(False)

    def test_changed_block_scalar_is_not_an_action_update(self):
        for header in ['      - run: |', '      - run: >-']:
            with self.subTest(header=header):
                baseline = WORKFLOW + f'{header}\n          - uses: actions/checkout@{OLD}\n'
                self.write('.github/workflows/ci.yaml', baseline)
                self.commit()
                self.git('branch', '-f', 'main', 'HEAD')
                self.write('.github/workflows/ci.yaml', baseline.replace(OLD, NEW))
                self.assert_result(False)

    def test_substantive_change_with_note_passes(self):
        self.write('firmware/code.c', 'int value = 2;\n')
        self.write('.changeset/new.md', '---\n"tempo": patch\n---\nFix value.\n')
        self.assert_result(True)

    def test_multiline_quoted_script_is_not_an_action_update(self):
        baseline = WORKFLOW + f'      - run: "echo start\n          - uses: actions/checkout@{OLD}\n          end"\n'
        self.write('.github/workflows/ci.yaml', baseline)
        self.commit()
        self.git('branch', '-f', 'main', 'HEAD')
        self.write('.github/workflows/ci.yaml', baseline.replace(OLD, NEW))
        self.assert_result(False)

    def test_workflow_mode_change_requires_note(self):
        path = self.repo / '.github/workflows/ci.yaml'
        path.write_text(WORKFLOW.replace(OLD, NEW))
        path.chmod(0o755)
        self.assert_result(False)


class CodeqlPolicyTests(unittest.TestCase):
    def test_mixed_codeql_versions_fail_the_real_workflow_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'scripts').mkdir()
            (root / 'workflows').mkdir()
            shutil.copy(ROOT / '.github/scripts/codeql-version.test.mjs', root / 'scripts')
            (root / 'workflows/security.yaml').write_text(
                f'      - uses: github/codeql-action/init@{OLD}\n'
                f'      - uses: github/codeql-action/analyze@{NEW}\n')
            result = subprocess.run(['node', '--test', str(root / 'scripts/codeql-version.test.mjs')],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('update all CodeQL steps together', result.stdout)


class BranchPolicyTests(unittest.TestCase):
    def test_real_branch_check_accepts_dependency_groups_and_rejects_invalid_names(self):
        workflow = (ROOT / '.github/workflows/pr-checks.yaml').read_text()
        block = re.search(r'        run: \|\n((?:          .*\n|\n)+)', workflow)[1]
        script = '\n'.join(line[10:] for line in block.splitlines())
        cases = {'feature/ci-maintenance': True,
                 'dependabot/github_actions/actions/checkout-7.0.1': True,
                 'dependabot/github_actions/github-actions-123abc': True,
                 'dependabot/github_actions/codeql-security-123abc': True,
                 'dependabot/': False, 'dependabot//bad': False,
                 'bad branch': False, 'main': False}
        for branch, expected in cases.items():
            with self.subTest(branch=branch):
                result = subprocess.run(['bash', '-c', script],
                                        env={**os.environ, 'BRANCH': branch}, capture_output=True)
                self.assertEqual(result.returncode == 0, expected, result.stdout)


if __name__ == '__main__':
    unittest.main()
