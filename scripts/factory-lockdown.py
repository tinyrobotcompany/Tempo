#!/usr/bin/env python3
"""Tempo factory lockdown: Secure Boot v2 + flash encryption (Development mode)
and the HMAC_UP identity key, burned from the host in Espressif's documented order.

Safe by default. Without --execute it only prints the plan and runs host-side
checks; it never opens a serial port. With --execute every device command needs
its own typed confirmation, and espefuse asks for 'BURN' again. Key material is
generated in this process and handed to espefuse through a pipe: it is never
printed, logged, sent over serial or written to disk.

See docs/FACTORY_RUNBOOK.md. Only the ESP32-P4 profile is reviewed.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Reviewed per-chip layouts. Other chips are accepted as a parameter but refused
# until their eFuse layout has been reviewed (docs/FACTORY_RUNBOOK.md, rehearsal).
PROFILES = {
    'esp32p4': {
        'min_revision': 3,
        'flash_key': ('BLOCK_KEY0', 'XTS_AES_128_KEY', 32),
        # DIGEST0 is the release signing key, DIGEST1 an independent backup key
        # kept separately for rotation; only the third slot is revoked.
        'digests': [('BLOCK_KEY2', 'SECURE_BOOT_DIGEST0'), ('BLOCK_KEY3', 'SECURE_BOOT_DIGEST1')],
        # Layout ab-8mb-secure-v1 (firmware/partitions-secure.csv): table moved to 0x10000.
        'offsets': {'bootloader.bin': 0x2000, 'partition-table.bin': 0x10000},
        'hmac': ('BLOCK_KEY5', 'HMAC_UP', 32),
        # Mirrors ESP-IDF v6.1 esp32p4 secure-features defaults, minus the
        # Release-only DIS_DOWNLOAD_MANUAL_ENCRYPT so USB re-flashing stays possible.
        'hardening': [('DIS_DIRECT_BOOT', 1), ('DIS_PAD_JTAG', 1), ('DIS_USB_JTAG', 1),
                      ('SOFT_DIS_JTAG', 7), ('SPI_DOWNLOAD_MSPI_DIS', 1)],
        'revoke': ['SECURE_BOOT_KEY_REVOKE2'],
    },
}
UNREVIEWED = {'esp32s3', 'esp32c6'}
# The production profile from scripts/secure-build.py (layout ab-8mb-secure-v1).
REQUIRED_CONFIG = {
    'SECURE_BOOT': True, 'SECURE_BOOT_V2_ENABLED': True, 'SECURE_SIGNED_APPS_RSA_SCHEME': True,
    'SECURE_FLASH_ENC_ENABLED': True, 'SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT': True,
    'SECURE_FLASH_ENCRYPTION_AES128': True, 'SECURE_FLASH_ENCRYPTION_KEY_SOURCE_EFUSES': True,
    'SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC': True, 'TEMPO_PRODUCTION_IDENTITY': True,
    'NVS_ENCRYPTION': True, 'NVS_SEC_KEY_PROTECT_USING_FLASH_ENC': True,
    'PARTITION_TABLE_CUSTOM_FILENAME': 'partitions-secure.csv',
}
FORBIDDEN_CONFIG = ['SECURE_FLASH_ENCRYPTION_MODE_RELEASE', 'SECURE_DISABLE_ROM_DL_MODE',
                    'SECURE_FLASH_ENCRYPTION_KEY_SOURCE_KEY_MGR', 'SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS',
                    'SECURE_BOOT_INSECURE', 'SECURE_BOOT_BUILD_SIGNED_BINARIES', 'SECURE_BOOT_ALLOW_JTAG',
                    'EFUSE_VIRTUAL', 'SECURE_BOOT_V2_FORCE_ENABLE_ECDSA']
# SECURE_BOOT_REQUIRE_ALREADY_ENABLED depends on SECURE_BOOT_INSECURE, so it is not asked for.
RECOMMENDED_CONFIG = ['SECURE_FLASH_REQUIRE_ALREADY_ENABLED']
SIGNED_SET = ('bootloader.bin', 'partition-table.bin', 'ota_data_initial.bin', 'weather_station.bin')
PEM_RE = re.compile(r'-----BEGIN PUBLIC KEY-----\n(?:[A-Za-z0-9+/=]{1,64}\n){2,12}-----END PUBLIC KEY-----\n?')
IDENTITY_RE = re.compile(r'Manufacturing PUBLIC identity for ([0-9a-f]{32}):\s*\n(' + PEM_RE.pattern + ')')
ANSI_RE = re.compile(r'\x1b\[[0-9;]*[A-Za-z]')


class Refused(Exception):
    pass


def block_index(block):
    return int(block.removeprefix('BLOCK_KEY'))


def raw(summary, name):
    return int(summary[name]['raw_value'], 16)


def purpose(summary, block):
    return summary['KEY_PURPOSE_%d' % block_index(block)]['value']


def block_empty(summary, block):
    return raw(summary, block) == 0 and summary[block]['readable'] and purpose(summary, block) == 'USER'


def openssl(args, data=None):
    result = subprocess.run(['openssl', *args], input=data, capture_output=True)
    if result.returncode:
        raise Refused('openssl rejected the key: ' + result.stderr.decode(errors='replace').strip()[:200])
    return result.stdout


def check_public_pem(text, kind):
    """Accept exactly one public key; any private material is refused unread."""
    if 'PRIVATE' in text.upper():
        raise Refused('Expected a PUBLIC key. Never give this tool a private key.')
    keys = re.findall(r'-----BEGIN PUBLIC KEY-----', text)
    if len(keys) != 1:
        raise Refused('Expected exactly one PEM public key.')
    details = openssl(['pkey', '-pubin', '-noout', '-text'], text.encode()).decode()
    if kind == 'rsa3072' and 'Public-Key: (3072 bit)' not in details:
        raise Refused('The Secure Boot key must be an RSA-3072 public key (ECDSA is not recommended on ESP32-P4).')
    if kind == 'p256' and ('prime256v1' not in details and 'P-256' not in details):
        raise Refused('The identity key must be an EC P-256 public key.')


def device_id(pem):
    der = openssl(['pkey', '-pubin', '-outform', 'DER'], pem.encode())
    return hashlib.sha256(der).hexdigest()[:32]


def load_config(build):
    path = build / 'config' / 'sdkconfig.json'
    if not path.is_file():
        raise Refused(f'{path} is missing; build the signed firmware first.')
    return json.loads(path.read_text())


def check_build(args, profile):
    """Host-only checks that must pass before anything touches a device."""
    build, notes = args.build_dir, []
    config = load_config(build)
    if config.get('IDF_TARGET') != args.chip:
        raise Refused(f"Build target {config.get('IDF_TARGET')!r} does not match --chip {args.chip}.")
    wrong = [k for k, v in REQUIRED_CONFIG.items() if config.get(k) != v]
    if config.get('PARTITION_TABLE_OFFSET') not in (0x10000, '0x10000'):
        wrong.append('PARTITION_TABLE_OFFSET (must be 0x10000)')
    wrong += [k for k in FORBIDDEN_CONFIG if config.get(k)]
    hmac_id = block_index(profile['hmac'][0])
    if config.get('TEMPO_IDENTITY_HMAC_KEY_ID') != hmac_id:
        wrong.append(f'TEMPO_IDENTITY_HMAC_KEY_ID (must be {hmac_id})')
    if wrong:
        raise Refused('Build is not a Development-mode Secure Boot v2 build for this plan: ' + ', '.join(wrong))
    notes += [f'Recommended build option not set: CONFIG_{k}' for k in RECOMMENDED_CONFIG if not config.get(k)]
    if not (build / 'encrypted_flash_args').is_file():
        raise Refused('encrypted_flash_args missing: not a Development-mode encrypted build.')
    signed = args.signed_dir
    sums = signed / 'SHA256SUMS'
    if not sums.is_file():
        raise Refused(f'{sums} is missing; run scripts/secure-build.py build first.')
    listed = dict(reversed(line.split('  ', 1)) for line in sums.read_text().splitlines() if line)
    if sorted(listed) != sorted(SIGNED_SET):
        raise Refused('SHA256SUMS does not list exactly the signed release set.')
    for name, want in listed.items():
        if hashlib.sha256((signed / name).read_bytes()).hexdigest() != want:
            raise Refused(f'{name} does not match SHA256SUMS; rebuild the signed set.')
    for image in ('bootloader.bin', 'weather_station.bin'):
        run_host(args, ['espsecure', 'verify-signature', '--version', '2', '--keyfile',
                        str(args.signing_public_key), str(signed / image)],
                 f'{image} is not signed by the given Secure Boot key.')
    flash = flash_arguments(build, signed, profile)
    secure_download = bool(config.get('SECURE_ENABLE_SECURE_ROM_DL_MODE'))
    return config, notes, secure_download, flash


def flash_arguments(build, signed, profile):
    """encrypted_flash_args from the build, with every image swapped for its signed copy."""
    tokens, images = (build / 'encrypted_flash_args').read_text().split(), []
    for i, token in enumerate(tokens):
        if re.fullmatch(r'0x[0-9a-fA-F]+', token) and i + 1 < len(tokens):
            name = Path(tokens[i + 1]).name
            if name not in SIGNED_SET:
                raise Refused(f'encrypted_flash_args lists {name}, which is not in the signed set.')
            tokens[i + 1] = str((signed / name).resolve())
            images.append((int(token, 16), (signed / name).stat().st_size, name))
    if sorted(name for _, _, name in images) != sorted(SIGNED_SET):
        raise Refused('encrypted_flash_args does not flash exactly the signed release set.')
    for name, offset in profile['offsets'].items():
        if (offset, name) not in [(o, n) for o, _, n in images]:
            raise Refused(f'{name} must be flashed at {offset:#x} (layout ab-8mb-secure-v1).')
    images.sort()
    for (start, size, name), (following, _, other) in zip(images, images[1:]):
        if start + size > following:
            raise Refused(f'{name} ({size:#x} bytes at {start:#x}) overlaps {other} at {following:#x}.')
    return tokens


def run_host(args, command, failure):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise Refused(failure)
    return result.stdout


def espefuse(args, *rest):
    # Real devices keep espefuse's own 'BURN' prompt as a second confirmation.
    target = (['--virt', '--path-efuse-file', str(args.virtual_efuse), '--do-not-confirm']
              if args.virtual_efuse else ['--port', args.port or 'PORT'])
    return ['espefuse', '--chip', args.chip, *target, *rest]


def esptool(args, *rest):
    return ['esptool', '--chip', args.chip, '--port', args.port or 'PORT', *rest]


def read_summary(args, name):
    path = args.record_dir / name
    path.unlink(missing_ok=True)  # Never mistake an old file for a fresh read.
    command = espefuse(args, 'summary', '--format', 'json', '--file', str(path))
    if subprocess.run(command, stdout=subprocess.DEVNULL).returncode or not path.is_file():
        raise Refused('Could not read the eFuse summary.')
    return json.loads(path.read_text())


def check_start_state(summary, profile):
    """Every eFuse this plan writes must be blank, or already exactly what we want."""
    major = raw(summary, 'WAFER_VERSION_MAJOR')
    if major < profile['min_revision']:
        raise Refused(f'Chip revision v{major}.x is below the v{profile["min_revision"]}.0 this firmware targets.')
    for name in ('DIS_DOWNLOAD_MANUAL_ENCRYPT', 'ENABLE_SECURITY_DOWNLOAD', 'DIS_DOWNLOAD_MODE',
                 'SECURE_BOOT_KEY_REVOKE0', 'SECURE_BOOT_KEY_REVOKE1', 'SECURE_BOOT_AGGRESSIVE_REVOKE'):
        if raw(summary, name):
            raise Refused(f'{name} is already burned; this unit needs failed-unit review, not this script.')
    if raw(summary, 'SPI_BOOT_CRYPT_CNT') not in (0, 1):
        raise Refused('SPI_BOOT_CRYPT_CNT is not 0 or 1: flash encryption was toggled outside this plan.')
    for block, want, *_ in (profile['flash_key'], profile['hmac'], *profile['digests']):
        if not block_empty(summary, block) and purpose(summary, block) != want:
            raise Refused(f'{block} already holds {purpose(summary, block)}; expected empty or {want}.')


class Step:
    def __init__(self, title, command, phrase, irreversible, done=None, before=None, cwd=None, device=True):
        self.title, self.command, self.phrase, self.irreversible = title, command, phrase, irreversible
        self.done, self.before, self.cwd, self.device = done, before, cwd, device


def key_pipe(length):
    """Generate a key in memory and expose it to espefuse only through a pipe."""
    key = bytearray(os.urandom(length))
    read, write = os.pipe()
    try:
        os.write(write, key)
    finally:
        os.close(write)
        for i in range(len(key)):
            key[i] = 0
    return read


def plan(args, profile, secure_download, flash):
    fe_block, fe_purpose, fe_len = profile['flash_key']
    hm_block, hm_purpose, hm_len = profile['hmac']

    def has(block, want):
        return lambda s: purpose(s, block) == want
    steps = [
        Step('Erase the whole flash (the verified backup is the only copy of the old contents)',
             esptool(args, 'erase-flash'), 'ERASE FLASH', False,
             done=lambda s: purpose(s, fe_block) != 'USER' or raw(s, 'SPI_BOOT_CRYPT_CNT') or raw(s, 'SECURE_BOOT_EN')),
        Step(f'Burn a new random flash-encryption key into {fe_block} ({fe_purpose}, read-protected)',
             espefuse(args, 'burn-key', fe_block, '<KEY-PIPE>', fe_purpose),
             f'BURN {fe_block} {fe_purpose}', True, done=has(fe_block, fe_purpose), before=('key', fe_len)),
        Step(f'Burn a new random identity HMAC key into {hm_block} ({hm_purpose}, read-protected)',
             espefuse(args, 'burn-key', hm_block, '<KEY-PIPE>', hm_purpose),
             f'BURN {hm_block} {hm_purpose}', True, done=has(hm_block, hm_purpose), before=('key', hm_len)),
        *[Step(f'Burn the {role} Secure Boot public-key digest into {block} ({want})',
               espefuse(args, 'burn-key', block, str(args.record_dir / f'secure-boot-digest{n}.bin'), want),
               f'BURN {block} {want}', True, done=has(block, want))
          for n, ((block, want), role) in enumerate(zip(profile['digests'], ('release', 'backup')))],
        Step('Enable flash encryption in Development mode (SPI_BOOT_CRYPT_CNT = 1)',
             espefuse(args, 'burn-efuse', 'SPI_BOOT_CRYPT_CNT', '1'), 'BURN SPI_BOOT_CRYPT_CNT 1', True,
             done=lambda s: raw(s, 'SPI_BOOT_CRYPT_CNT') == 1),
        Step('Disable JTAG, direct boot and download-mode cache access',
             espefuse(args, 'burn-efuse', *[str(x) for pair in profile['hardening'] for x in pair]),
             'BURN HARDENING', True,
             done=lambda s: all(raw(s, name) == value for name, value in profile['hardening'])),
        Step('Revoke the unused third Secure Boot key slot',
             espefuse(args, 'burn-efuse', *[x for name in profile['revoke'] for x in (name, '1')]),
             'BURN REVOKE UNUSED', True, done=lambda s: all(raw(s, n) for n in profile['revoke'])),
        Step('Enable Secure Boot v2 (SECURE_BOOT_EN)', espefuse(args, 'burn-efuse', 'SECURE_BOOT_EN', '1'),
             'BURN SECURE_BOOT_EN', True, done=lambda s: raw(s, 'SECURE_BOOT_EN') == 1),
        Step('Write-protect RD_DIS (last eFuse change; no key can be read-protected afterwards)',
             espefuse(args, 'write-protect-efuse', 'RD_DIS'), 'PROTECT RD_DIS', True,
             done=lambda s: raw(s, 'WR_DIS') & 1),
        Step('Read the eFuses back and verify the whole plan', ['<VERIFY>'], 'yes', False),
        Step('Flash the signed firmware, encrypting it on the chip',
             esptool(args, '--before', 'default-reset', '--after', 'no-reset', 'write-flash', *flash),
             'FLASH SIGNED', False),
    ]
    if secure_download:
        steps.append(Step('Switch ROM download mode to Secure Download Mode (blocks espefuse from now on)',
                          espefuse(args, 'burn-efuse', 'ENABLE_SECURITY_DOWNLOAD', '1'),
                          'BURN ENABLE_SECURITY_DOWNLOAD', True, done=lambda s: raw(s, 'ENABLE_SECURITY_DOWNLOAD')))
    if args.virtual_efuse:
        for step in steps:
            if step.command[0] == 'esptool':
                step.device = False
    return steps


def verify(summary, profile, digests):
    fe_block, fe_purpose, _ = profile['flash_key']
    hm_block, hm_purpose, _ = profile['hmac']
    problems = []
    rd_dis = raw(summary, 'RD_DIS')
    blocks = [(fe_block, fe_purpose, False), (hm_block, hm_purpose, False)]
    blocks += [(block, want, True) for block, want in profile['digests']]
    for block, want, readable in blocks:
        if purpose(summary, block) != want:
            problems.append(f'{block} purpose is {purpose(summary, block)}, expected {want}')
        if bool(rd_dis >> block_index(block) & 1) == readable:
            problems.append(f'{block} read protection is wrong')
        if summary[block]['writeable']:
            problems.append(f'{block} is not write-protected')
    for (block, want), digest in zip(profile['digests'], digests):
        if raw(summary, block).to_bytes(32, 'big') != digest:
            problems.append(f'{block} does not hold the expected {want} value')
    expected = dict(profile['hardening'], SPI_BOOT_CRYPT_CNT=1, SECURE_BOOT_EN=1, DIS_DOWNLOAD_MANUAL_ENCRYPT=0,
                    SECURE_BOOT_KEY_REVOKE0=0, SECURE_BOOT_KEY_REVOKE1=0)
    expected.update({name: 1 for name in profile['revoke']})
    problems += [f'{n} is {raw(summary, n)}, expected {v}' for n, v in expected.items() if raw(summary, n) != v]
    if not raw(summary, 'WR_DIS') & 1:
        problems.append('RD_DIS is not write-protected')
    return problems


def confirm(step, number):
    print(f'\nStep {number}: {step.title}')
    print('  ' + ('IRREVERSIBLE. ' if step.irreversible else '') + 'Command: ' + shlex.join(step.command))
    try:
        answer = input(f'  Type "{step.phrase}" to run this step, anything else stops: ')
    except EOFError:
        answer = ''
    if answer.strip() != step.phrase:
        raise Refused(f'Stopped before step {number}. Nothing in this step was run.')


def confirm_signing_backup(args, digests):
    release, backup = (d.hex()[:16] for d in digests)
    print('\nOnly firmware signed by one of these two keys will ever boot on this unit. Without either')
    print('private key no new firmware can be installed. Check both offline backups now.')
    print(f'Release key fingerprint: {release}\nBackup key fingerprint:  {backup} (stored separately)')
    phrase = f'I HAVE VERIFIED OFFLINE BACKUPS OF {release} AND {backup}'
    try:
        answer = input(f'Type "{phrase}" to continue: ')
    except EOFError:
        answer = ''
    if answer.strip() != phrase:
        raise Refused('Signing key backup not confirmed. Nothing was changed.')


def execute(args, steps, profile, digests):
    summary = read_summary(args, 'efuse-start.json')
    check_start_state(summary, profile)
    for number, step in enumerate(steps, 1):
        if step.done and step.done(summary):
            print(f'\nStep {number}: {step.title}: already done, skipped.')
            continue
        if not step.device:
            print(f'\nStep {number}: {step.title}: skipped (virtual eFuses, no device).')
            continue
        confirm(step, number)
        if step.command == ['<VERIFY>']:
            summary = read_summary(args, 'efuse-after.json')
            problems = verify(summary, profile, digests)
            if problems:
                raise Refused('Verification failed; do not flash. Follow the failed-unit section:\n  ' + '\n  '.join(problems))
            print('  eFuses match the plan.')
            continue
        command, fds = list(step.command), ()
        if step.before and step.before[0] == 'key':
            fd = key_pipe(step.before[1])
            command[command.index('<KEY-PIPE>')] = f'/dev/fd/{fd}'
            fds = (fd,)
        try:
            result = subprocess.run(command, pass_fds=fds, cwd=step.cwd)
        finally:
            for fd in fds:
                os.close(fd)
        if result.returncode:
            raise Refused(f'Step {number} failed (exit {result.returncode}). Re-run this script; finished steps are skipped.')
        if step.command[0] == 'espefuse':
            summary = read_summary(args, f'efuse-step{number}.json')
    print('\nLockdown complete. Next: runbook step "First boot and reading the public key".')


def extract_identity(args):
    text = ANSI_RE.sub('', args.log.read_text(errors='replace')).replace('\r\n', '\n')
    if 'PRIVATE' in text.upper():
        raise Refused('The log contains the word PRIVATE. Stop and report this; do not register the unit.')
    found = {(m.group(1), m.group(2).rstrip('\n') + '\n') for m in IDENTITY_RE.finditer(text)}
    if len(found) != 1:
        raise Refused(f'Expected one manufacturing identity in the log, found {len(found)}.')
    identifier, pem = found.pop()
    check_public_pem(pem, 'p256')
    if device_id(pem) != identifier:
        raise Refused('The logged device ID does not match its public key.')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'identity-public.pem').write_text(pem)
    print(f'Device ID: {identifier}')
    print(f'Hostname:  d-{identifier}.{args.suffix}')
    print(f'Public key saved to {args.out / "identity-public.pem"} (public, safe to share).')
    print('Workflow input public_key_base64:')
    print(base64.b64encode(pem.encode()).decode())


def capture_log(args):
    import serial  # Provided by the ESP-IDF Python environment.
    import time
    answer = input(f'Open {args.port} read-only for {args.seconds} s and save the boot log? Type "yes": ')
    if answer.strip() != 'yes':
        raise Refused('Stopped; the port was not opened.')
    with serial.Serial(args.port, 115200, timeout=0.5) as port, args.out.open('wb') as out:
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            out.write(port.read(4096))
    print(f'Saved {args.out}. Next: --extract-identity.')


def init_virtual(args):
    if args.virtual_efuse.exists():
        raise Refused(f'{args.virtual_efuse} already exists.')
    revision = str(PROFILES[args.chip]['min_revision'])
    command = ['espefuse', '--chip', args.chip, '--virt', '--path-efuse-file', str(args.virtual_efuse),
               '--do-not-confirm', 'burn-efuse', 'WAFER_VERSION_MAJOR_LO', revision]
    if subprocess.run(command, stdout=subprocess.DEVNULL).returncode:
        raise Refused('Could not create the virtual eFuse file.')
    print(f'Created a blank virtual {args.chip} v{revision}.0 eFuse file at {args.virtual_efuse}.')


def parse(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--chip', default='esp32p4')
    p.add_argument('--port')
    p.add_argument('--build-dir', type=Path, default=ROOT / 'build-host' / 'secure-build',
                   help='Compiled production build (scripts/secure-build.py --build-dir)')
    p.add_argument('--signed-dir', type=Path, default=ROOT / 'build-host' / 'secure-release',
                   help='Signed images and SHA256SUMS (scripts/secure-build.py --output)')
    p.add_argument('--signing-public-key', type=Path, help='Release Secure Boot RSA-3072 PUBLIC key (PEM)')
    p.add_argument('--backup-signing-public-key', type=Path, help='Independent backup RSA-3072 PUBLIC key (PEM)')
    p.add_argument('--record-dir', type=Path, help='The unit folder made by factory-backup.sh')
    p.add_argument('--virtual-efuse', type=Path, help='Rehearse against an espefuse virtual eFuse file')
    p.add_argument('--execute', action='store_true', help='Run steps (each needs typed confirmation)')
    p.add_argument('--init-virtual', action='store_true', help='Create a blank virtual eFuse file and stop')
    p.add_argument('--capture-log', type=Path, metavar='FILE', dest='capture', help='Save the serial boot log')
    p.add_argument('--seconds', type=int, default=40)
    p.add_argument('--extract-identity', type=Path, metavar='LOG', dest='log')
    p.add_argument('--out', type=Path)
    p.add_argument('--suffix', default='tempo.tinyrobotcompany.io')
    return p.parse_args(argv)


def main(argv=None):
    args = parse(argv)
    try:
        if args.chip in UNREVIEWED:
            raise Refused(f'{args.chip} is accepted as a parameter, but its eFuse profile is not reviewed yet.')
        if args.chip not in PROFILES:
            raise Refused(f'Unknown chip {args.chip}.')
        profile = PROFILES[args.chip]
        if args.log:
            if not args.out:
                raise Refused('--extract-identity needs --out DIR.')
            return extract_identity(args)
        if args.capture:
            if not args.port:
                raise Refused('--capture-log needs --port.')
            args.out = args.capture
            return capture_log(args)
        if args.init_virtual:
            if not args.virtual_efuse:
                raise Refused('--init-virtual needs --virtual-efuse FILE.')
            return init_virtual(args)
        keys = [args.signing_public_key, args.backup_signing_public_key]
        if not all(key and key.is_file() for key in keys):
            raise Refused('Give both --signing-public-key and --backup-signing-public-key (PUBLIC key files).')
        for key in keys:
            check_public_pem(key.read_text(errors='replace'), 'rsa3072')
        if len({openssl(['pkey', '-pubin', '-outform', 'DER'], k.read_bytes()) for k in keys}) != 2:
            raise Refused('The backup signing key must be a different, independent key.')
        if args.execute and not args.virtual_efuse:
            if not args.port:
                raise Refused('--execute needs --port (or --virtual-efuse for a rehearsal).')
            manifest = args.record_dir / 'manifest.json' if args.record_dir else None
            if not manifest or not manifest.is_file() or json.loads(manifest.read_text()).get('verified') is not True:
                raise Refused('No verified backup: run scripts/factory-backup.sh first and pass --record-dir.')
            # Enabling Secure Boot or flash encryption turns off the ROM's USB-OTG download
            # stack. USB-Serial/JTAG and a UART bridge keep working (Secure Boot v2 guide).
            if json.loads(manifest.read_text()).get('usb_mode') not in ('USB-Serial/JTAG', 'UART'):
                raise Refused('The backup did not record a USB-Serial/JTAG or UART connection; see the runbook.')
        if args.record_dir is None:
            if args.execute:
                raise Refused('--execute needs --record-dir.')
            args.record_dir = ROOT / 'build-host' / 'factory-plan'
        args.record_dir.mkdir(parents=True, exist_ok=True)
        config, notes, secure_download, flash = check_build(args, profile)
        digests = []
        for n, key in enumerate(keys):
            path = args.record_dir / f'secure-boot-digest{n}.bin'
            path.unlink(missing_ok=True)
            run_host(args, ['espsecure', 'digest-sbv2-public-key', '--keyfile', str(key), '--output', str(path)],
                     'Could not compute a Secure Boot key digest.')
            digests.append(path.read_bytes())
        if any(len(d) != 32 for d in digests) or digests[0] == digests[1]:
            raise Refused('Secure Boot digests are malformed or identical.')
        for note in notes:
            print('NOTE: ' + note)
        steps = plan(args, profile, secure_download, flash)
        if not args.execute:
            print(f'DRY RUN for {args.chip}. Nothing was sent to any device. Host checks passed.')
            for number, step in enumerate(steps, 1):
                mark = 'IRREVERSIBLE ' if step.irreversible else ''
                print(f'{number:2}. {mark}{step.title}\n    {shlex.join(step.command)}')
            print('Re-run with --execute --port PORT --record-dir DIR to perform it, one confirmed step at a time.')
            return 0
        confirm_signing_backup(args, digests)
        execute(args, steps, profile, digests)
        return 0
    except Refused as error:
        print('REFUSED: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
