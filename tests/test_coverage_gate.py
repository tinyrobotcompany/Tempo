import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('gate',Path(__file__).resolve().parents[1]/'scripts/check-coverage.py')
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
class CoverageGateTests(unittest.TestCase):
    def test_exact_threshold_passes_but_below_fails(self):
        rows,fail=gate.evaluate({'a':{'count':100,'covered':80}},{'a'})
        self.assertEqual(fail,[])
        self.assertEqual(gate.evaluate({'a':{'count':100,'covered':79}},{'a'})[1],['a'])
    def test_missing_or_empty_instrumentation_fails(self):
        for data in ({},{'a':{'count':0,'covered':0}}):
            with self.assertRaises(ValueError):gate.evaluate(data,{'a'})
if __name__=='__main__':unittest.main()
