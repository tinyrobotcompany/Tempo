#!/usr/bin/env python3
"""Build and Secure Boot v2-sign Tempo firmware for locked boards.

Never flashes, burns eFuses or publishes. The private signing key is read from the
path the operator passes and is never copied into the repository or build-host/.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / 'firmware'
PROFILE = ('sdkconfig.defaults', 'sdkconfig.rev3', 'sdkconfig.production')
LAYOUT = 'ab-8mb-secure-v1'
PARTITIONS = 'partitions-secure.csv'
APP = 'weather_station.bin'
PUBLIC_KEY = 'secure_boot_public_key.pem'
SECTOR = 0x1000
# Every option a locked-board build must have. Agent/factory lockdown depends on
# this list; docs/RELEASING.md mirrors it.
REQUIRED = {
    'CONFIG_SECURE_BOOT': 'y',
    'CONFIG_SECURE_BOOT_V2_ENABLED': 'y',
    'CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME': 'y',
    'CONFIG_SECURE_SIGNED_ON_BOOT': 'y',
    'CONFIG_SECURE_SIGNED_ON_UPDATE': 'y',
    'CONFIG_SECURE_FLASH_ENC_ENABLED': 'y',
    'CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT': 'y',
    # The factory script enables Secure Boot and flash encryption in eFuse first;
    # the bootloader must refuse to boot rather than lock a board by itself.
    # Espressif gates SECURE_BOOT_REQUIRE_ALREADY_ENABLED behind SECURE_BOOT_INSECURE,
    # so that menu is allowed only with every other option in it off (INSECURE_GATED).
    'CONFIG_SECURE_BOOT_INSECURE': 'y',
    'CONFIG_SECURE_BOOT_REQUIRE_ALREADY_ENABLED': 'y',
    'CONFIG_SECURE_FLASH_REQUIRE_ALREADY_ENABLED': 'y',
    'CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC': 'y',
    'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE': 'y',
    'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK': 'y',
    'CONFIG_NVS_ENCRYPTION': 'y',
    'CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC': 'y',
    'CONFIG_TEMPO_PRODUCTION_IDENTITY': 'y',
    'CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID': '5',  # Factory HMAC_UP key in eFuse BLOCK_KEY5.
    'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME': '"' + PARTITIONS + '"',
    'CONFIG_PARTITION_TABLE_OFFSET': '0x10000',
}
# Options that weaken the lockdown or put the signing key into the build. INSECURE_GATED
# is every other option ESP-IDF 6.1 exposes behind SECURE_BOOT_INSECURE or Development mode.
INSECURE_GATED = ('CONFIG_SECURE_BOOT_ALLOW_JTAG', 'CONFIG_SECURE_BOOT_ALLOW_ROM_BASIC',
                  'CONFIG_SECURE_BOOT_ALLOW_SHORT_APP_PARTITION', 'CONFIG_SECURE_BOOT_ALLOW_UNUSED_DIGEST_SLOTS',
                  'CONFIG_SECURE_BOOT_V2_FORCE_ENABLE_ECDSA', 'CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP',
                  'CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_DEC', 'CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_CACHE',
                  'CONFIG_SECURE_FLASH_SKIP_WRITE_PROTECTION_CACHE')
FORBIDDEN = ('CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES', 'CONFIG_BOOTLOADER_EFUSE_SECURE_VERSION_EMULATE',
             'CONFIG_EFUSE_VIRTUAL', 'CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE',
             'CONFIG_BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON', 'CONFIG_BOOTLOADER_SKIP_VALIDATE_ALWAYS',
             *INSECURE_GATED)
ENROLLMENT = re.compile(r'"https://[a-z0-9.-]+\.[a-z]{2,}/v1/device"')


def read_config(path):
    values = {}
    for line in Path(path).read_text().splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            key, value = line.split('=', 1)
            values[key] = value
    return values


def check_config(path):
    """Return the secure version, or raise ValueError listing every missing guard."""
    values = read_config(path)
    errors = [f'{key} must be {want}' for key, want in REQUIRED.items() if values.get(key) != want]
    errors += [f'{key} must not be set' for key in FORBIDDEN if values.get(key) == 'y']
    if not ENROLLMENT.fullmatch(values.get('CONFIG_TEMPO_ENROLLMENT_URL', '')):
        errors.append('CONFIG_TEMPO_ENROLLMENT_URL must be the HTTPS .../v1/device endpoint')
    secure = values.get('CONFIG_BOOTLOADER_APP_SECURE_VERSION', '')
    width = values.get('CONFIG_BOOTLOADER_APP_SEC_VER_SIZE_EFUSE_FIELD', '')
    if not (secure.isdigit() and width.isdigit() and 1 <= int(secure) <= int(width)):
        errors.append('CONFIG_BOOTLOADER_APP_SECURE_VERSION must be set between 1 and the eFuse field size')
    if errors:
        raise ValueError('Production configuration rejected:\n  ' + '\n  '.join(errors))
    return int(secure)


def signature_block(blob):
    """Check the appended RSA-3072 Secure Boot v2 signature sector covers this image."""
    if len(blob) < 2 * SECTOR or len(blob) % SECTOR:
        raise ValueError('Image is not a Secure Boot v2 signed binary')
    sector = blob[-SECTOR:]
    if sector[:4] != b'\xe7\x02\x00\x00':
        raise ValueError('Missing RSA Secure Boot v2 signature block')
    if zlib.crc32(sector[:1196]) != int.from_bytes(sector[1196:1200], 'little'):
        raise ValueError('Corrupt Secure Boot v2 signature block')
    if sector[4:36] != hashlib.sha256(blob[:-SECTOR]).digest():
        raise ValueError('Secure Boot v2 signature does not cover this image')


def secure_version(blob):
    if len(blob) < 112 or blob[0] != 0xe9 or int.from_bytes(blob[32:36], 'little') != 0xabcd5432:
        raise ValueError('Invalid ESP application descriptor')
    return int.from_bytes(blob[36:40], 'little')


def espsecure():
    for name in ('espsecure', 'espsecure.py'):
        found = shutil.which(name)
        if found:
            return [found]
    raise ValueError('espsecure not found; activate the pinned ESP-IDF environment first')


def outside_repository(key):
    key = Path(key).resolve(strict=True)
    if key.is_relative_to(ROOT.resolve()):
        raise ValueError('Keep the signing key outside the repository and build-host/')
    return key


def run(tool, *args):
    return subprocess.run([*tool, *map(str, args)], capture_output=True, text=True)


def must(tool, *args):
    result = run(tool, *args)
    if result.returncode:
        raise ValueError(f'espsecure {args[0]} failed:\n{result.stdout}{result.stderr}')


def sign_and_prove(tool, unsigned, key, signed):
    """Sign, then prove good/tampered/wrong-key behaviour before anything is published."""
    with tempfile.TemporaryDirectory(dir=signed.parent) as scratch:
        scratch = Path(scratch)
        public = scratch / 'public.pem'
        must(tool, 'sign-data', '--version', '2', '--keyfile', key, '--output', signed, unsigned)
        must(tool, 'extract-public-key', '--version', '2', '--keyfile', key, public)
        must(tool, 'verify-signature', '--version', '2', '--keyfile', public, signed)
        signature_block(signed.read_bytes())
        tampered = scratch / 'tampered.bin'
        blob = bytearray(signed.read_bytes())
        blob[len(blob) // 3] ^= 0x01
        tampered.write_bytes(blob)
        if run(tool, 'verify-signature', '--version', '2', '--keyfile', public, tampered).returncode == 0:
            raise ValueError('Tampered image was accepted; refusing to continue')
        wrong_key, wrong = scratch / 'disposable.pem', scratch / 'wrong-key.bin'
        must(tool, 'generate-signing-key', '--version', '2', '--scheme', 'rsa3072', wrong_key)
        must(tool, 'sign-data', '--version', '2', '--keyfile', wrong_key, '--output', wrong, unsigned)
        if run(tool, 'verify-signature', '--version', '2', '--keyfile', public, wrong).returncode == 0:
            raise ValueError('Wrong-key image was accepted; refusing to continue')


def compile_firmware(build):
    sdk = os.environ['IDF_PATH']
    subprocess.run([sys.executable, str(ROOT / 'scripts/check-idf-security.py'), sdk], check=True)
    build = build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    defaults = ';'.join(str(FIRMWARE / name) for name in PROFILE)
    idf = [sys.executable, str(Path(sdk) / 'tools/idf.py'), '-C', str(FIRMWARE),
           '-B', str(build), '-D', 'SDKCONFIG=' + str(build / 'sdkconfig'), '-D', 'SDKCONFIG_DEFAULTS=' + defaults]
    subprocess.run([*idf, 'reconfigure'], check=True)
    check_config(build / 'sdkconfig')  # Fail before compiling anything.
    subprocess.run([*idf, 'build'], check=True)
    return check_config(build / 'sdkconfig')


def flash_args(build):
    """esptool @file for the signed set: IDF's offsets plus the bootloader it omits under Secure Boot."""
    lines = (build / 'flash_args').read_text().splitlines()
    files = dict(reversed(line.split(maxsplit=1)) for line in lines[1:] if line.strip())
    offsets = {'bootloader.bin': read_config(build / 'sdkconfig')['CONFIG_BOOTLOADER_OFFSET_IN_FLASH'],
               'partition-table.bin': files['partition_table/partition-table.bin'],
               'ota_data_initial.bin': files['ota_data_initial.bin'], APP: files[APP]}
    return lines[0] + '\n' + ''.join(f'{offset} {name}\n' for name, offset in offsets.items())


def sign_build(build, key, output):
    key = outside_repository(key)
    version = check_config(build / 'sdkconfig')
    app = (build / APP).read_bytes()
    if secure_version(app) != version:
        raise ValueError('Application secure version does not match the production profile')
    output.mkdir(parents=True, exist_ok=True)
    names = ('bootloader.bin', 'partition-table.bin', 'ota_data_initial.bin', APP, PUBLIC_KEY, 'flash_args', 'SHA256SUMS')
    for name in names:  # A failed run must not leave an earlier signed set behind.
        (output / name).unlink(missing_ok=True)
    tool = espsecure()
    sign_and_prove(tool, build / APP, key, output / APP)
    sign_and_prove(tool, build / 'bootloader/bootloader.bin', key, output / 'bootloader.bin')
    for name in ('partition_table/partition-table.bin', 'ota_data_initial.bin'):
        shutil.copyfile(build / name, output / Path(name).name)
    must(tool, 'extract-public-key', '--version', '2', '--keyfile', key, output / PUBLIC_KEY)
    (output / 'flash_args').write_text(flash_args(build))
    (output / 'SHA256SUMS').write_text(''.join(
        f'{hashlib.sha256((output / n).read_bytes()).hexdigest()}  {n}\n'
        for n in names[:-1]))
    return output / APP


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    check = sub.add_parser('check-config', help='Validate a generated sdkconfig')
    check.add_argument('sdkconfig', type=Path)
    for name in ('compile', 'build'):
        command = sub.add_parser(name, help='Configure and compile the production profile'
                                 if name == 'compile' else 'Compile, then sign for a one-time USB flash')
        command.add_argument('--build-dir', type=Path, default=ROOT / 'build-host/secure-build')
    sign = sub.add_parser('sign', help='Sign and verify an already compiled production build')
    for command in (sub.choices['build'], sign):
        command.add_argument('--signing-key', type=Path, required=True)
        command.add_argument('--output', type=Path, default=ROOT / 'build-host/secure-release')
    sign.add_argument('--build-dir', type=Path, default=ROOT / 'build-host/secure-build')
    args = parser.parse_args(argv)
    try:
        if args.action == 'check-config':
            print(f'PASS: production profile, secure version {check_config(args.sdkconfig)}')
            return 0
        if args.action == 'build':
            outside_repository(args.signing_key)  # Refuse before a long compile.
        if args.action in ('compile', 'build'):
            compile_firmware(args.build_dir)
        if args.action in ('build', 'sign'):
            image = sign_build(args.build_dir.resolve(), args.signing_key, args.output.resolve())
            print(f'PASS: {image} is Secure Boot v2 signed; tampered and wrong-key images were rejected.')
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError) as exc:
        print(f'FAIL: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
