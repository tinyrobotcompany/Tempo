import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('release', Path(__file__).resolve().parents[1] / 'scripts/release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'firmware').mkdir()
        (self.root / '.changeset').mkdir()
        (self.root / 'firmware/CMakeLists.txt').write_text('project(weather_station VERSION 0.8.16)\n')

    def note(self, name='change', bump='patch', body='Fix a user-visible issue.'):
        (self.root / '.changeset' / (name + '.md')).write_text(f'---\n"tempo": {bump}\n---\n\n{body}\n')

    def test_preparation_consumes_notes_and_matches_tag(self):
        self.note()
        (self.root / 'README.md').write_text('Current firmware: **0.8.16**. firmware-0.8.16-blue')
        self.assertEqual(release.prepare(self.root, '2026-11-15'), '0.8.17')
        release.check(self.root, 'v0.8.17')
        self.assertIn('firmware-0.8.17-blue', (self.root / 'README.md').read_text())
        self.assertIn('Fix a user-visible issue.', (self.root / 'docs/releases/0.8.17.md').read_text())
        self.assertEqual(release.pending(self.root), [])
        with self.assertRaises(ValueError):
            release.prepare(self.root)

    def test_highest_bump_wins(self):
        self.note('fix')
        self.note('feature', 'minor')
        self.note('breaking', 'major')
        self.assertEqual(release.prepare(self.root), '1.0.0')

    def test_minor_resets_patch(self):
        self.note(bump='minor')
        self.assertEqual(release.prepare(self.root), '0.9.0')

    def test_invalid_note_changes_nothing(self):
        self.note(bump='invalid')
        with self.assertRaises(ValueError):
            release.prepare(self.root)
        self.assertEqual(release.current(self.root)[2], '0.8.16')
        self.assertTrue((self.root / '.changeset/change.md').exists())

    def test_pending_notes_and_wrong_tag_block_release(self):
        self.note()
        with self.assertRaises(ValueError):
            release.check(self.root, 'v0.8.16')
        release.prepare(self.root)
        with self.assertRaises(ValueError):
            release.check(self.root, 'v0.8.18')
        (self.root / 'docs/releases/0.8.17.md').unlink()
        with self.assertRaises(ValueError):
            release.check(self.root, 'v0.8.17')

    def test_existing_notes_cannot_be_overwritten(self):
        self.note()
        dest = self.root / 'docs/releases/0.8.17.md'
        dest.parent.mkdir(parents=True)
        dest.write_text('Existing release')
        with self.assertRaises(ValueError):
            release.prepare(self.root)
        self.assertEqual(dest.read_text(), 'Existing release')
        self.assertEqual(release.current(self.root)[2], '0.8.16')

if __name__ == '__main__':
    unittest.main()
