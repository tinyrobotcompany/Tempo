import pathlib
import subprocess
import tempfile
import unittest

class ReleaseTagCheckout(unittest.TestCase):
    def test_annotated_and_lightweight_tags_validate_the_tagged_tree(self):
        workflow = (pathlib.Path(__file__).resolve().parents[1] / '.github/workflows/release.yml').read_text()
        self.assertIn("tags: ['source-v*']", workflow)
        self.assertIn('check --tag "${GITHUB_REF_NAME#source-}"', workflow)
        self.assertNotIn("tags: ['v*']", workflow)
        commands = workflow.split('        run: |\n', 1)[1].split('          python3 scripts/release.py', 1)[0]
        commands = '\n'.join(line[10:] for line in commands.splitlines())
        with tempfile.TemporaryDirectory() as folder:
            def git(*args):
                return subprocess.check_output(['git', *args], cwd=folder, text=True, stderr=subprocess.DEVNULL).strip()
            git('init', '-b', 'main')
            git('config', 'user.name', 'Test')
            git('config', 'user.email', 'test@example.invalid')
            marker = pathlib.Path(folder) / 'version'
            marker.write_text('release')
            git('add', '.')
            git('commit', '-m', 'release')
            commit = git('rev-parse', 'HEAD')
            git('tag', '-a', 'source-v1', '-m', 'release')
            git('tag', 'source-v2')
            marker.write_text('later')
            git('commit', '-am', 'later main')
            git('update-ref', 'refs/remotes/origin/main', 'HEAD')
            for tag in ('source-v1', 'source-v2'):
                git('checkout', 'main')
                import os
                subprocess.run(['bash', '-e', '-c', commands], cwd=folder,
                    env={**os.environ, 'GITHUB_REF': 'refs/tags/' + tag,
                         'GITHUB_SHA': git('rev-parse', tag)}, check=True,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                self.assertEqual(git('rev-parse', 'HEAD'), commit)
                self.assertEqual(marker.read_text(), 'release')

if __name__ == '__main__':
    unittest.main()
