"""Real OpenSSL certificates: never replace verification with a mocked success."""
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('identity', ROOT/'scripts/phone-identity.py')
identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(identity)


class IdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)/'device'
        identity.create(cls.directory, 'device-a7f3.tempo.tinyrobotcompany.io')
        identity.development_certificate(cls.directory)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def copy(self, root):
        target=Path(root)/'identity'
        shutil.copytree(self.directory,target)
        target.chmod(0o700)
        return target

    def test_real_matching_identity_and_chain(self):
        self.assertEqual(identity.validate(self.directory,self.directory/'ca.pem'),'device-a7f3.tempo.tinyrobotcompany.io')
        self.assertEqual((self.directory/'device.key').stat().st_mode & 0o777,0o600)
        self.assertNotEqual((self.directory/'device.key').read_bytes(),(self.directory/'ca.key').read_bytes())
        with self.assertRaises(ValueError):identity.create(self.directory,'other.example.com')
        with self.assertRaises(ValueError):identity.development_certificate(self.directory)

    def test_wrong_hostname_key_untrusted_and_expiring_certificate(self):
        with tempfile.TemporaryDirectory() as root:
            target=self.copy(root)
            (target/'identity.json').write_text('{"hostname":"other.tempo.tinyrobotcompany.io"}')
            with self.assertRaises(ValueError):identity.validate(target,target/'ca.pem')
            (target/'identity.json').write_bytes((self.directory/'identity.json').read_bytes())
            other=Path(root)/'other'
            identity.create(other,'other.tempo.tinyrobotcompany.io')
            identity.development_certificate(other)
            with self.assertRaises(ValueError):identity.validate(target,other/'ca.pem')
            (target/'device.key').write_bytes((other/'device.key').read_bytes())
            with self.assertRaises(ValueError):identity.validate(target,target/'ca.pem')
            (target/'device.key').write_bytes((self.directory/'device.key').read_bytes())
            identity.run('openssl','x509','-req','-in',str(target/'device.csr'),'-CA',str(target/'ca.pem'),
                         '-CAkey',str(target/'ca.key'),'-CAcreateserial','-days','1',
                         '-extfile',str(target/'extensions.cnf'),'-out',str(target/'fullchain.pem'))
            with self.assertRaises(ValueError):identity.validate(target,target/'ca.pem')

    def test_leaf_and_intermediate_bundle(self):
        with tempfile.TemporaryDirectory() as root:
            d=self.copy(root)
            # Reissue the test root with room for one intermediate CA.
            identity.run('openssl','req','-x509','-key',str(d/'ca.key'),'-days','7',
                         '-subj','/CN=Test root','-addext','basicConstraints=critical,CA:TRUE,pathlen:1','-out',str(d/'ca.pem'))
            issuer=Path(root)/'issuer';identity.create(issuer,'issuer.example.com')
            (issuer/'extensions').write_text('basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\n')
            def sign(csr,ca,key,extensions,out):
                identity.run('openssl','x509','-req','-in',str(csr),'-CA',str(ca),'-CAkey',str(key),
                             '-CAcreateserial','-days','3','-sha256','-extfile',str(extensions),'-out',str(out))
            sign(issuer/'device.csr',d/'ca.pem',d/'ca.key',issuer/'extensions',issuer/'ca.pem')
            sign(d/'device.csr',issuer/'ca.pem',issuer/'device.key',d/'extensions.cnf',d/'fullchain.pem')
            leaf=(d/'fullchain.pem').read_bytes()
            (d/'fullchain.pem').write_bytes(leaf+(issuer/'ca.pem').read_bytes())
            self.assertEqual(identity.validate(d,d/'ca.pem'),'device-a7f3.tempo.tinyrobotcompany.io')
            (d/'identity.json').write_text('{"hostname":"wrong.example.com"}')
            with self.assertRaises(ValueError):identity.validate(d,d/'ca.pem')
            (d/'identity.json').write_bytes((self.directory/'identity.json').read_bytes())
            (d/'device.key').write_bytes((issuer/'device.key').read_bytes())
            with self.assertRaises(ValueError):identity.validate(d,d/'ca.pem')
            (d/'device.key').write_bytes((self.directory/'device.key').read_bytes())
            (d/'fullchain.pem').write_bytes(leaf)
            with self.assertRaises(ValueError):identity.validate(d,d/'ca.pem')

    def test_bounds_permissions_and_names(self):
        for name in ['','localhost','192.168.1.2','*.example.com','a..example.com','-a.example.com','a-.example.com','UPPER.example.com','a'*64+'.com','a.com/other','a'*97]:
            with self.assertRaises(ValueError):identity.hostname(name)
        with tempfile.TemporaryDirectory() as root:
            target=self.copy(root)
            target.chmod(0o755)
            with self.assertRaises(ValueError):identity.private_directory(target)
            target.chmod(0o700)
            (target/'device.key').chmod(0o644)
            with self.assertRaises(ValueError):identity.validate(target,target/'ca.pem')
            (target/'device.key').chmod(0o600)
            (target/'fullchain.pem').write_text('x'*8192)
            with self.assertRaises(ValueError):identity.validate(target,target/'ca.pem')


if __name__=='__main__':unittest.main()
