import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('browsergate',Path(__file__).resolve().parents[1]/'scripts/check-browser-coverage.py')
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
class GateTests(unittest.TestCase):
    def test_each_module_must_be_present_and_above_floor(self):
        gate.check('app.js | 80.00 | 50.00 |',{'app.js'})
        for report in ('','app.js | 79.99 | 90.00 |'):
            with self.assertRaises(ValueError):gate.check(report,{'app.js'})
        with self.assertRaises(ValueError):gate.check('app.js | 100.00 |',{'app.js','new.js'})
if __name__=='__main__':unittest.main()
