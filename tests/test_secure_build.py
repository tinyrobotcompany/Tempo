"""Secure Boot v2 production profile, signing proof and release wiring, with disposable keys only."""
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sb = load('secure_build', 'scripts/secure-build.py')
ota = load('ota_release', 'scripts/ota-release.py')
LOCK = ROOT / 'tests/esptool-requirements.txt'


def espsecure_venv():
    """Hash-locked espsecure for host tests; release builds use the pinned ESP-IDF one."""
    cache = Path(os.environ.get('TEMPO_TEST_CACHE', ROOT / 'build-host/dependencies'))
    venv = cache / ('espsecure-' + hashlib.sha256(LOCK.read_bytes()).hexdigest()[:16])
    python = venv / 'bin/python'
    if not (venv / 'ready').exists():
        shutil.rmtree(venv, ignore_errors=True)
        subprocess.run(['python3', '-m', 'venv', str(venv)], check=True)
        options = ['-m', 'pip', 'install', '-q', '--disable-pip-version-check', '--no-cache-dir',
                   '--require-hashes', '--no-deps', '--only-binary=:all:']
        lines = LOCK.read_text().splitlines()
        start = next(i for i, line in enumerate(lines) if line.startswith('setuptools=='))
        backend = venv / 'build-backend.txt'
        backend.write_text('\n'.join(lines[start:start + 2]) + '\n')
        # esptool is published only as an sdist; its hashed build backend is installed first.
        subprocess.run([str(python), *options, '-r', str(backend)], check=True)
        subprocess.run([str(python), *options, '--no-build-isolation', '--no-binary=esptool', '-r', str(LOCK)], check=True)
        (venv / 'ready').touch()
    return venv / 'bin'


def profile_config(**changes):
    """The generated-sdkconfig view of sdkconfig.production plus build-time inputs."""
    values = sb.read_config(ROOT / 'firmware/sdkconfig.production')
    values.update({'CONFIG_SECURE_SIGNED_ON_BOOT': 'y', 'CONFIG_SECURE_SIGNED_ON_UPDATE': 'y',
                   'CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID': '2'})
    for key, value in changes.items():
        if value is None:
            values.pop(key, None)
        else:
            values[key] = value
    return ''.join(f'{key}={value}\n' for key, value in values.items())


def app_image(secure_version=1, version=b'1.0.0'):
    blob = bytearray(0x3000)
    blob[0] = 0xe9
    blob[32:36] = (0xabcd5432).to_bytes(4, 'little')
    blob[36:40] = secure_version.to_bytes(4, 'little')
    blob[48:48 + len(version)] = version
    blob[80:95] = b'weather_station'
    blob[0x1000:0x1100] = os.urandom(0x100)
    return bytes(blob)


class ProductionProfileTests(unittest.TestCase):
    def check(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'sdkconfig'
            path.write_text(text)
            return sb.check_config(path)

    def rejected(self, message, **changes):
        with self.assertRaisesRegex(ValueError, message):
            self.check(profile_config(**changes))

    def test_profile_passes_and_every_guard_is_required(self):
        self.assertEqual(self.check(profile_config()), 1)
        for key in sb.REQUIRED:
            self.rejected(key, **{key: None})
        self.rejected('ENROLLMENT_URL', CONFIG_TEMPO_ENROLLMENT_URL='""')
        self.rejected('ENROLLMENT_URL', CONFIG_TEMPO_ENROLLMENT_URL='"http://localhost/v1/device"')
        self.rejected('HMAC', CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID='-1')
        self.rejected('HMAC', CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=None)
        self.rejected('SECURE_VERSION', CONFIG_BOOTLOADER_APP_SECURE_VERSION=None)
        self.rejected('SECURE_VERSION', CONFIG_BOOTLOADER_APP_SECURE_VERSION='0')
        self.rejected('SECURE_VERSION', CONFIG_BOOTLOADER_APP_SECURE_VERSION='17')
        self.rejected('mode', CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=None)
        self.rejected('mode', CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE='y')
        for key in sb.FORBIDDEN:
            self.rejected(key, **{key: 'y'})

    def test_profile_commits_documented_choices(self):
        profile = (ROOT / 'firmware/sdkconfig.production').read_text()
        for line in ('CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y', 'CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=y',
                     '# CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES is not set', 'CONFIG_BOOTLOADER_APP_SECURE_VERSION=1',
                     'CONFIG_TEMPO_ENROLLMENT_URL="https://tempo-westeurope-api.politemushroom-8174e848.westeurope.azurecontainerapps.io/v1/device"'):
            self.assertIn(line + '\n', profile)
        self.assertFalse(any(line.startswith('CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID') for line in profile.splitlines()))
        self.assertNotIn('SECURE_BOOT_SIGNING_KEY', profile)
        for name in sb.PROFILE:
            self.assertTrue((ROOT / 'firmware' / name).is_file())
        releasing = (ROOT / 'docs/RELEASING.md').read_text()
        for key in [*sb.REQUIRED, *sb.FORBIDDEN, 'CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID', 'CONFIG_TEMPO_ENROLLMENT_URL']:
            self.assertIn(key, releasing)
        self.assertIn('python3 scripts/secure-build.py build --signing-key', releasing)

    def test_compile_layers_profile_and_stops_before_build_when_config_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            build, calls = Path(directory) / 'build', []
            def fake_run(command, check):
                calls.append(Path(command[1]).name if command[-1] == directory else command[-1])
                if command[-1] == 'reconfigure':
                    defaults = next(c for c in command if c.startswith('SDKCONFIG_DEFAULTS='))
                    slot = Path(defaults.rsplit(';', 1)[1]).read_text().split('=')[1].strip()
                    (build / 'sdkconfig').write_text(profile_config(CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=slot))
            with patch.dict(os.environ, {'IDF_PATH': directory}), patch.object(sb.subprocess, 'run', fake_run):
                with self.assertRaisesRegex(ValueError, 'slot 0-5'):
                    sb.compile_firmware(build, -1)
                self.assertEqual(sb.compile_firmware(build, 3), 1)
                self.assertEqual(calls, ['check-idf-security.py', 'reconfigure', 'build'])
                calls.clear()
                with patch.object(sb, 'REQUIRED', {**sb.REQUIRED, 'CONFIG_TEMPO_TEST_ONLY': 'y'}):
                    with self.assertRaisesRegex(ValueError, 'CONFIG_TEMPO_TEST_ONLY'):
                        sb.compile_firmware(build, 3)
                self.assertNotIn('build', calls)
            self.assertEqual((build / 'tempo-identity.defaults').read_text(), 'CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=3\n')

    def test_secure_layout_keeps_app_slots_and_encrypts_nvs_keys(self):
        def table(name):
            rows = [line.split(',') for line in (ROOT / 'firmware' / name).read_text().splitlines()
                    if line.strip() and not line.startswith('#')]
            return {r[0].strip(): (r[1].strip(), r[2].strip(), int(r[3], 16), int(r[4], 16), r[5].strip()) for r in rows}
        secure, plain = table(sb.PARTITIONS), table('partitions.csv')
        self.assertEqual(plain['nvs'][2], 0x9000)  # Unlocked development layout is unchanged.
        for name in ('ota_0', 'ota_1', 'phone_tls'):
            self.assertEqual(secure[name], plain[name])
        self.assertEqual(secure['nvs_keys'][:2], ('data', 'nvs_keys'))
        self.assertEqual(secure['nvs_keys'][4], 'encrypted')
        spans = sorted((offset, offset + size) for _, _, offset, size, _ in secure.values())
        self.assertGreaterEqual(spans[0][0], 0x10000 + 0x1000)  # After the moved partition table.
        self.assertTrue(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])))
        self.assertLessEqual(spans[-1][1], 32 * 1024 * 1024)
        for name in ('ota_0', 'ota_1'):
            self.assertEqual(secure[name][2] % 0x10000 + secure[name][3] % 0x10000, 0)

    def test_layout_identifier_tracks_hardware_security(self):
        header = ROOT / 'firmware/components/station_core/include/station_update.h'
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'layout.c'
            source.write_text('#include <stdio.h>\n#include "station_update.h"\nint main(void){puts(TEMPO_UPDATE_LAYOUT);}\n')
            def layout(*flags):
                binary = Path(directory) / 'layout'
                subprocess.run(['cc', *flags, '-I', str(header.parent), str(source), '-o', str(binary)], check=True)
                return subprocess.check_output([str(binary)], text=True).strip()
            self.assertEqual(layout(), 'ab-8mb-https-v1')
            self.assertEqual(layout('-DCONFIG_SECURE_BOOT_V2_ENABLED=1'), 'ab-8mb-https-v1')
            locked = layout('-DCONFIG_SECURE_BOOT_V2_ENABLED=1', '-DCONFIG_SECURE_FLASH_ENC_ENABLED=1')
        self.assertEqual(locked, sb.LAYOUT)
        self.assertEqual(ota.LAYOUT, sb.LAYOUT)


class SigningProofTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bin = espsecure_venv()
        cls.tool = [str(cls.bin / 'python'), '-m', 'espsecure']

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.key = self.root / 'disposable-signing-key.pem'
        sb.must(self.tool, 'generate-signing-key', '--version', '2', '--scheme', 'rsa3072', self.key)
        self.addCleanup(self.directory.cleanup)

    def test_signed_image_verifies_and_tampered_or_wrong_key_images_fail(self):
        unsigned, signed = self.root / 'app.bin', self.root / 'signed.bin'
        unsigned.write_bytes(app_image())
        sb.sign_and_prove(self.tool, unsigned, self.key, signed)
        blob = signed.read_bytes()
        sb.signature_block(blob)
        self.assertEqual(sb.secure_version(blob), 1)
        with self.assertRaisesRegex(ValueError, 'Secure Boot v2 signature block|not a Secure Boot'):
            sb.signature_block(unsigned.read_bytes())
        tampered = bytearray(blob)
        tampered[0x1010] ^= 1
        with self.assertRaisesRegex(ValueError, 'does not cover'):
            sb.signature_block(bytes(tampered))
        corrupt = bytearray(blob)
        corrupt[-4096 + 100] ^= 1
        with self.assertRaisesRegex(ValueError, 'Corrupt'):
            sb.signature_block(bytes(corrupt))
        self.assertNotIn(self.key.read_bytes(), blob)

    def test_proof_fails_closed_when_verifier_accepts_bad_images(self):
        unsigned = self.root / 'app.bin'
        unsigned.write_bytes(app_image())
        permissive = self.root / 'permissive.py'
        permissive.write_text('import subprocess,sys\n'
                              'if sys.argv[1]=="verify-signature":sys.exit(0)\n'
                              f'sys.exit(subprocess.run([{str(self.bin / "python")!r},"-m","espsecure",*sys.argv[1:]]).returncode)\n')
        with self.assertRaisesRegex(ValueError, 'Tampered image was accepted'):
            sb.sign_and_prove([str(self.bin / 'python'), str(permissive)], unsigned, self.key, self.root / 'out.bin')
        with self.assertRaisesRegex(ValueError, 'sign-data failed'):
            sb.sign_and_prove([*self.tool], unsigned, self.root / 'missing.pem', self.root / 'out.bin')

    def test_sign_build_outputs_usb_flash_set_and_feeds_ota_package(self):
        build, output, repo = self.root / 'build', self.root / 'release', self.root / 'repo'
        for name in ('bootloader', 'partition_table'):
            (build / name).mkdir(parents=True)
        repo.mkdir()
        (build / 'sdkconfig').write_text(profile_config())
        (build / sb.APP).write_bytes(app_image())
        (build / 'bootloader/bootloader.bin').write_bytes(b'\xe9' + bytes(0x2fff))
        (build / 'partition_table/partition-table.bin').write_bytes(b'table')
        (build / 'ota_data_initial.bin').write_bytes(b'\xff' * 0x2000)
        inside = repo / 'key.pem'
        shutil.copyfile(self.key, inside)
        with patch.object(sb, 'ROOT', repo), patch.dict(os.environ, {'PATH': f'{self.bin}{os.pathsep}{os.environ["PATH"]}'}):
            with self.assertRaisesRegex(ValueError, 'outside the repository'):
                sb.sign_build(build, inside, output)
            self.assertFalse(output.exists())
            image = sb.sign_build(build, self.key, output)
            self.assertEqual(sb.main(['sign', '--signing-key', str(inside), '--build-dir', str(build), '--output', str(output)]), 1)
            with patch.object(sb, 'sign_and_prove', side_effect=ValueError('signing failed')):
                with self.assertRaisesRegex(ValueError, 'signing failed'):
                    sb.sign_build(build, self.key, output)
            self.assertFalse((output / sb.APP).exists())  # No stale signed image survives a failure.
            image = sb.sign_build(build, self.key, output)
            (build / sb.APP).write_bytes(app_image(secure_version=0))
            with self.assertRaisesRegex(ValueError, 'secure version'):
                sb.sign_build(build, self.key, output)
        self.assertEqual(sorted(p.name for p in output.iterdir()),
                         ['SHA256SUMS', 'bootloader.bin', 'ota_data_initial.bin', 'partition-table.bin', sb.APP])
        sb.signature_block((output / 'bootloader.bin').read_bytes())
        self.assertNotIn(b'PRIVATE KEY', b''.join(p.read_bytes() for p in output.iterdir()))
        ota_key, ota_public = self.root / 'ota.pem', self.root / 'ota-public.pem'
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', str(ota_key)], check=True, capture_output=True)
        subprocess.run(['openssl', 'pkey', '-in', str(ota_key), '-pubout', '-out', str(ota_public)], check=True, capture_output=True)
        ota.package(self.root / 'ota', image, '1.0.0', ota_key, ota_public)
        with self.assertRaisesRegex(ValueError, 'Secure Boot v2 signature block|not a Secure Boot'):
            ota.package(self.root / 'ota', build / sb.APP, '1.0.0', ota_key, ota_public)


class ReleaseWorkflowTests(unittest.TestCase):
    def test_app_is_signed_and_proven_before_manifest_signing(self):
        workflow = (ROOT / '.github/workflows/ota-release.yml').read_text()
        def step(name):
            return workflow.split('- name: ' + name, 1)[1].split('\n      - name: ', 1)[0]
        build = step('Build credential-free release firmware')
        sign = step('Secure Boot sign and verify application')
        manifest = step('Sign and verify release manifest')
        self.assertIn('scripts/secure-build.py compile --build-dir firmware/build --hmac-key-id', build)
        self.assertIn('vars.TEMPO_IDENTITY_HMAC_KEY_ID', build)
        self.assertNotIn('secrets.', build)
        self.assertIn('SECURE_BOOT_SIGNING_KEY: ${{ secrets.SECURE_BOOT_SIGNING_KEY }}', sign)
        self.assertIn('scripts/secure-build.py sign', sign)
        self.assertIn("trap 'rm -f \"$signing_key\"' EXIT", sign)
        self.assertNotIn('OTA_SIGNING_PRIVATE_KEY', sign)
        self.assertNotIn('SECURE_BOOT_SIGNING_KEY', manifest)
        self.assertIn('--image build-host/secure-release/weather_station.bin', manifest)
        self.assertLess(workflow.index('- name: Build credential-free release firmware'), workflow.index('- name: Secure Boot sign and verify application'))
        self.assertLess(workflow.index('- name: Secure Boot sign and verify application'), workflow.index('- name: Sign and verify release manifest'))
        self.assertEqual(workflow.count('secrets.SECURE_BOOT_SIGNING_KEY'), 1)
        self.assertNotIn('idf.py -C firmware build', workflow)


if __name__ == '__main__':
    unittest.main()
