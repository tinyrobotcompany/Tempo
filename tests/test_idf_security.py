import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('idf_security',ROOT/'scripts/check-idf-security.py')
security=importlib.util.module_from_spec(spec);spec.loader.exec_module(security)


class SDKSecurityTests(unittest.TestCase):
    def test_build_and_release_share_fixed_pin_and_preflight(self):
        for path in ('.github/workflows/ci.yaml','.github/workflows/ota-release.yml'):
            text=(ROOT/path).read_text()
            self.assertIn('ref: '+security.COMMIT,text)
            self.assertIn('scripts/check-idf-security.py',text)
            build='scripts/secure-build.py compile' if path.endswith('ota-release.yml') else 'idf.py -C firmware'
            self.assertLess(text.index('scripts/check-idf-security.py'),text.index(build))
        deps=json.loads((ROOT/'tests/dependencies.json').read_text())
        self.assertIn(security.COMMIT,next(x['url'] for x in deps if x['name']=='embed.cmake'))

    def test_old_sdk_and_tampered_parser_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk=Path(directory);parser=sdk/security.PARSER;parser.parent.mkdir(parents=True);parser.write_bytes(b'fixture')
            with patch.object(security.subprocess,'check_output',return_value='fff9895c82d744c7237be8847347bdd1b07c6643\n'):
                with self.assertRaisesRegex(ValueError,'previous 6.1'): security.check(sdk)
            with patch.object(security.subprocess,'check_output',return_value=security.COMMIT+'\n'):
                with self.assertRaisesRegex(ValueError,'differs'): security.check(sdk)
                with patch.object(security,'PARSER_SHA256',hashlib.sha256(b'fixture').hexdigest()): security.check(sdk)


if __name__=='__main__': unittest.main()
