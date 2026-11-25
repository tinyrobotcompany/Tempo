#!/usr/bin/env python3
"""Fail-closed production configuration and Secure Boot image verification.

Never generates keys, provisions eFuses, flashes hardware or publishes a release.
The key is supplied by the protected release environment, separately from the
P-256 OTA manifest key. The configured public-key fingerprint is factory policy.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {
    'CONFIG_SECURE_BOOT': 'y',
    'CONFIG_SECURE_BOOT_V2_ENABLED': 'y',
    'CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME': 'y',
    'CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES': 'y',
    'CONFIG_SECURE_FLASH_ENC_ENABLED': 'y',
    'CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE': 'y',
    'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE': 'y',
    'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK': 'y',
    'CONFIG_TEMPO_PRODUCTION_IDENTITY': 'y',
    'CONFIG_TEMPO_PRODUCTION_RELEASE': 'y',
    'CONFIG_NVS_ENCRYPTION': 'y',
    'CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC': 'y',
    'CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC': 'n',
    'CONFIG_NVS_SEC_KEY_PROTECT_NONE': 'n',
    'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME': '"partitions.production.csv"',
    'CONFIG_PARTITION_TABLE_FILENAME': '"partitions.production.csv"',
    'CONFIG_PARTITION_TABLE_OFFSET': '0x10000',
    'CONFIG_EFUSE_VIRTUAL': 'n',
    'CONFIG_SECURE_BOOT_INSECURE': 'n',
    'CONFIG_SECURE_BOOT_V2_FORCE_ENABLE_ECDSA': 'n',
    'CONFIG_MBEDTLS_KEY_EXCHANGE_RSA': 'n',
    'CONFIG_MBEDTLS_SSL_RENEGOTIATION': 'n',
    'CONFIG_MBEDTLS_CIPHER_MODE_CBC': 'n',
    'CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_RSA': 'y',
    'CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA': 'y',
    'CONFIG_MBEDTLS_GCM_C': 'y',
}


def policy(environment):
    url = environment.get('TEMPO_ENROLLMENT_URL', '')
    parsed = urlsplit(url)
    host = parsed.hostname or ''
    if (any(c.isspace() or c in ('"', '\\') for c in url)
            or parsed.scheme != 'https' or parsed.username or parsed.password or parsed.port not in (None, 443)
            or parsed.path != '/v1/device' or parsed.query or parsed.fragment
            or not re.fullmatch(r'(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z]{2,63}', host)
            or host.endswith(('.localhost', '.local', '.invalid', '.test', '.example'))
            or any(host == name or host.endswith('.' + name) for name in ('example.com', 'example.net', 'example.org'))):
        raise ValueError('A deployed public HTTPS enrollment endpoint is required')
    slot = environment.get('TEMPO_IDENTITY_HMAC_KEY_ID', '')
    version = environment.get('TEMPO_SECURE_VERSION', '')
    fingerprint = environment.get('SECURE_BOOT_PUBLIC_KEY_SHA256', '')
    if not re.fullmatch('[0-5]', slot):
        raise ValueError('Factory HMAC slot must be explicitly set to 0..5')
    if not re.fullmatch(r'0|[1-9]|1[0-6]', version):
        raise ValueError('Reviewed hardware security version must be explicitly set to 0..16')
    if not re.fullmatch('[0-9a-f]{64}', fingerprint):
        raise ValueError('Factory Secure Boot public-key SHA-256 fingerprint is required')
    return dict(REQUIRED, CONFIG_TEMPO_ENROLLMENT_URL='"' + url + '"',
                CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=slot,
                CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=slot,
                CONFIG_BOOTLOADER_APP_SECURE_VERSION=version), fingerprint


def public_key(key, fingerprint):
    public = subprocess.run(['openssl', 'pkey', '-in', str(key), '-pubout', '-outform', 'DER'],
                            check=True, capture_output=True).stdout
    # RSA parsing rejects EC keys; inspect public material only, never private text.
    text = subprocess.run(['openssl', 'rsa', '-pubin', '-inform', 'DER', '-text', '-noout'],
                          input=public, check=True, capture_output=True).stdout
    if b'(3072 bit)' not in text or hashlib.sha256(public).hexdigest() != fingerprint:
        raise ValueError('Secure Boot signing key does not match the approved RSA-3072 factory identity')
    return public


def configure(base, output, key, environment):
    required, fingerprint = policy(environment)
    public_key(key, fingerprint)
    name = str(key.resolve())
    if any(c in name for c in ('"', '\n', '\r', '\\')):
        raise ValueError('Invalid signing-key path')
    required['CONFIG_SECURE_BOOT_SIGNING_KEY'] = '"' + name + '"'
    lines = [line for line in base.read_text().splitlines()
             if not any(line.startswith(k + '=') or line == '# ' + k + ' is not set' for k in required)]
    lines.extend(k + '=' + v if v != 'n' else '# ' + k + ' is not set' for k, v in required.items())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n')


def verify(config, image, key, environment):
    required, fingerprint = policy(environment)
    public = public_key(key, fingerprint)
    actual = {}
    for line in config.read_text().splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            k, v = line.split('=', 1)
            actual[k] = v
    for name, value in required.items():
        if actual.get(name, 'n') != value:
            raise ValueError('Resolved production configuration mismatch: ' + name)
    blob = image.read_bytes()
    if len(blob) < 4096 or len(blob) > 0x800000 or blob[0] != 0xe9:
        raise ValueError('Invalid or oversized signed application')
    if int.from_bytes(blob[32:36], 'little') != 0xabcd5432 or int.from_bytes(blob[36:40], 'little') != int(required['CONFIG_BOOTLOADER_APP_SECURE_VERSION']):
        raise ValueError('Application security version differs from factory policy')
    # Espressif validates the actual signature block, not just its presence.
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        pem = Path(directory) / 'public.pem'
        result = subprocess.run(['openssl', 'pkey', '-pubin', '-inform', 'DER', '-pubout'],
                                input=public, capture_output=True, check=True)
        pem.write_bytes(result.stdout)
        subprocess.run([sys.executable, '-m', 'espsecure', 'verify-signature', '--version', '2',
                        '--keyfile', str(pem), str(image)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['configure', 'verify'])
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--key', required=True, type=Path)
    parser.add_argument('--image', type=Path)
    args = parser.parse_args()
    if args.action == 'configure':
        configure(ROOT / 'firmware/sdkconfig', args.config, args.key, os.environ)
    else:
        if not args.image: parser.error('verify requires --image')
        verify(args.config, args.image, args.key, os.environ)
