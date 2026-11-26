import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('watcher', Path(__file__).parents[1] / 'scripts/watch-device.py')
watcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(watcher)

class WatcherTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.table = self.root / 'firmware/build/partition_table/partition-table.bin'
        self.table.parent.mkdir(parents=True)
        self.table.write_bytes(b'original')
        self.source = self.root / 'firmware/main/main.c'
        self.source.parent.mkdir()
        self.source.write_text('initial')

    def test_vendor_patch_changes_are_watched(self):
        source=self.root/'firmware/vendor/adapter/src/bridge.c'
        source.parent.mkdir(parents=True); source.write_text('before')
        before=watcher.fingerprint(self.root)
        source.write_text('after')
        self.assertNotEqual(before,watcher.fingerprint(self.root))

    def test_phone_assets_and_installer_are_watched(self):
        for name in ('firmware/main/web/app.js', 'firmware/main/web/index.html',
                     'firmware/main/web/style.css'):
            with self.subTest(name=name):
                source=self.root/name
                source.parent.mkdir(parents=True,exist_ok=True)
                before=watcher.fingerprint(self.root)
                source.write_text('before')
                self.assertNotEqual(before,watcher.fingerprint(self.root))
                before=watcher.fingerprint(self.root)
                source.write_text('after')
                self.assertNotEqual(before,watcher.fingerprint(self.root))

    def test_failed_build_never_flashes(self):
        calls=[]
        def run(args, **kwargs):
            calls.append(args); return SimpleNamespace(returncode=1)
        self.assertFalse(watcher.build_and_flash('port', run, self.root))
        self.assertEqual(len(calls), 1)

    def test_changed_partitions_never_flash(self):
        calls=[]
        def run(args, **kwargs):
            calls.append(args); self.table.write_bytes(b'changed'); return SimpleNamespace(returncode=0)
        self.assertFalse(watcher.build_and_flash('port', run, self.root))
        self.assertEqual(len(calls), 1)

    def test_partition_rejection_survives_retry(self):
        def run(args, **kwargs):
            self.table.write_bytes(b'changed'); return SimpleNamespace(returncode=0)
        self.assertFalse(watcher.build_and_flash('port', run, self.root))
        self.assertFalse(watcher.build_and_flash('port', run, self.root))

    def test_changed_source_never_flashes_stale_build(self):
        calls=[]
        def run(args, **kwargs):
            calls.append(args); self.source.write_text('new'); return SimpleNamespace(returncode=0)
        self.assertFalse(watcher.build_and_flash('port', run, self.root))
        self.assertEqual(len(calls), 1)

    def test_success_only_flashes_application(self):
        calls=[]
        def run(args, **kwargs):
            calls.append(args); return SimpleNamespace(returncode=0)
        self.assertTrue(watcher.build_and_flash('port', run, self.root))
        self.assertEqual(calls[1][-3:], ['-p', 'port', 'app-flash'])

if __name__ == '__main__':
    unittest.main()
