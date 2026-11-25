import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('production', ROOT/'scripts/production-release.py')
production=importlib.util.module_from_spec(spec);spec.loader.exec_module(production)


class ProductionReleaseTests(unittest.TestCase):
    def policy(self):
        return {'TEMPO_ENROLLMENT_URL':'https://enrollment.tempo.tinyrobotcompany.io/v1/device',
                'TEMPO_IDENTITY_HMAC_KEY_ID':'2','TEMPO_SECURE_VERSION':'0',
                'SECURE_BOOT_PUBLIC_KEY_SHA256':'a'*64}

    def test_missing_and_unsafe_factory_configuration_fails_closed(self):
        env=self.policy()
        for key in env:
            with self.subTest(key=key), self.assertRaises(ValueError):
                production.policy({k:v for k,v in env.items() if k!=key})
        for url in ('http://api.company.io/v1/device','https://192.168.1.34/v1/device',
                    'https://localhost/v1/device','https://api.example.com/v1/device',
                    'https://user:secret@api.company.io/v1/device','https://api.company.io/v1/device?x',
                    'https://api.company.io/v1/device\n'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                production.policy(dict(env,TEMPO_ENROLLMENT_URL=url))
        for slot in ('-1','6','', '2\n'):
            with self.assertRaises(ValueError): production.policy(dict(env,TEMPO_IDENTITY_HMAC_KEY_ID=slot))
        for version in ('-1','17','0.1',''):
            with self.assertRaises(ValueError): production.policy(dict(env,TEMPO_SECURE_VERSION=version))

    def test_real_signing_key_and_resolved_configuration_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);key=root/'key.pem';config=root/'sdkconfig';image=root/'app.bin'
            subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072','-out',str(key)],capture_output=True,check=True)
            public=subprocess.run(['openssl','pkey','-in',str(key),'-pubout','-outform','DER'],capture_output=True,check=True).stdout
            env=dict(self.policy(),SECURE_BOOT_PUBLIC_KEY_SHA256=hashlib.sha256(public).hexdigest())
            production.configure(ROOT/'firmware/sdkconfig',config,key,env)
            self.assertNotIn('PRIVATE KEY',config.read_text())
            with self.assertRaises(ValueError): production.public_key(key,'0'*64)
            blob=bytearray(8192);blob[0]=0xe9;blob[32:36]=(0xabcd5432).to_bytes(4,'little');image.write_bytes(blob)
            # Unsigned fixture must reach Espressif verification, never be accepted by marker alone.
            real_run=production.subprocess.run
            def run(args,**kwargs):
                if args[1:3]==['-m','espsecure']:
                    raise subprocess.CalledProcessError(1,args)
                return real_run(args,**kwargs)
            with patch.object(production.subprocess,'run',side_effect=run):
                with self.assertRaises(subprocess.CalledProcessError): production.verify(config,image,key,env)
            config.write_text(config.read_text().replace('CONFIG_SECURE_BOOT=y','# CONFIG_SECURE_BOOT is not set'))
            with self.assertRaisesRegex(ValueError,'configuration mismatch'): production.verify(config,image,key,env)

    def test_pipeline_verifies_signed_image_before_manifest_packaging(self):
        workflow=(ROOT/'.github/workflows/ota-release.yml').read_text()
        self.assertLess(workflow.index('production-release.py verify'),workflow.index('package --production'))
        self.assertIn('SECURE_BOOT_SIGNING_PRIVATE_KEY',workflow)
        self.assertIn('SDKCONFIG="$config"',workflow)
        self.assertIn('unset SECURE_BOOT_SIGNING_PRIVATE_KEY',workflow)
        header=(ROOT/'firmware/components/station_core/include/station_update.h').read_text()
        self.assertIn('ab-8mb-protected-v1',header)
        self.assertIn('ab-8mb-https-v1',header)

    def test_encrypted_customer_storage_requires_existing_locked_factory_key(self):
        required,_=production.policy(self.policy())
        self.assertEqual(required['CONFIG_NVS_ENCRYPTION'],'y')
        self.assertEqual(required['CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC'],'y')
        self.assertEqual(required['CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID'],required['CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID'])
        main=(ROOT/'firmware/main/main.c').read_text()
        self.assertLess(main.index('ESP_ERROR_CHECK(station_identity_store_init())'),main.index('ESP_ERROR_CHECK(nvs_flash_init())'))
        self.assertNotIn('nvs_flash_erase',main)
        rows=[line.split(',') for line in (ROOT/'firmware/partitions.production.csv').read_text().splitlines() if line and not line.startswith('#')]
        previous=0x11000
        for row in rows:
            offset,size=int(row[3].strip(),0),int(row[4].strip(),0)
            self.assertGreaterEqual(offset,previous)
            previous=offset+size
        self.assertLessEqual(previous,32*1024*1024)


if __name__=='__main__': unittest.main()
