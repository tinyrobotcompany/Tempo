#!/usr/bin/env node
// Build and Secure Boot v2-sign Tempo firmware for locked boards.
// Never flashes, burns eFuses or publishes. The private signing key is read from the
// path the operator passes and is never copied into the repository or build-host/.
import { createHash } from 'node:crypto';
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, realpathSync, rmSync, unlinkSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import zlib from 'node:zlib';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const FIRMWARE = path.join(ROOT, 'firmware');
export const PROFILE = ['sdkconfig.defaults', 'sdkconfig.rev3', 'sdkconfig.production'];
export const LAYOUT = 'ab-8mb-secure-v1';
export const PARTITIONS = 'partitions-secure.csv';
export const APP = 'weather_station.bin';
export const PUBLIC_KEY = 'secure_boot_public_key.pem';
const SECTOR = 0x1000;

// Every option a locked-board build must have. The factory lockdown depends on this
// list; docs/RELEASING.md mirrors it.
export const REQUIRED = {
  CONFIG_SECURE_BOOT: 'y',
  CONFIG_SECURE_BOOT_V2_ENABLED: 'y',
  CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME: 'y',
  CONFIG_SECURE_SIGNED_ON_BOOT: 'y',
  CONFIG_SECURE_SIGNED_ON_UPDATE: 'y',
  CONFIG_SECURE_FLASH_ENC_ENABLED: 'y',
  CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT: 'y',
  // The factory script enables Secure Boot and flash encryption in eFuse first; the
  // bootloader must refuse to boot rather than lock a board by itself. ESP-IDF gates
  // SECURE_BOOT_REQUIRE_ALREADY_ENABLED behind SECURE_BOOT_INSECURE, so that menu is
  // allowed only with every other option in it off (INSECURE_GATED).
  CONFIG_SECURE_BOOT_INSECURE: 'y',
  CONFIG_SECURE_BOOT_REQUIRE_ALREADY_ENABLED: 'y',
  CONFIG_SECURE_FLASH_REQUIRE_ALREADY_ENABLED: 'y',
  CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC: 'y',
  CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE: 'y',
  CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK: 'y',
  CONFIG_NVS_ENCRYPTION: 'y',
  CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC: 'y',
  CONFIG_TEMPO_PRODUCTION_IDENTITY: 'y',
  CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID: '5', // Factory HMAC_UP key in eFuse BLOCK_KEY5.
  CONFIG_PARTITION_TABLE_CUSTOM_FILENAME: `"${PARTITIONS}"`,
  CONFIG_PARTITION_TABLE_OFFSET: '0x10000',
};
// Every other option ESP-IDF 6.1 exposes behind SECURE_BOOT_INSECURE or Development mode.
export const INSECURE_GATED = ['CONFIG_SECURE_BOOT_ALLOW_JTAG', 'CONFIG_SECURE_BOOT_ALLOW_ROM_BASIC',
  'CONFIG_SECURE_BOOT_ALLOW_SHORT_APP_PARTITION', 'CONFIG_SECURE_BOOT_ALLOW_UNUSED_DIGEST_SLOTS',
  'CONFIG_SECURE_BOOT_V2_FORCE_ENABLE_ECDSA', 'CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP',
  'CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_DEC', 'CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_CACHE',
  'CONFIG_SECURE_FLASH_SKIP_WRITE_PROTECTION_CACHE'];
// Options that weaken the lockdown or put the signing key into the build.
export const FORBIDDEN = ['CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES', 'CONFIG_BOOTLOADER_EFUSE_SECURE_VERSION_EMULATE',
  'CONFIG_EFUSE_VIRTUAL', 'CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE',
  'CONFIG_BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON', 'CONFIG_BOOTLOADER_SKIP_VALIDATE_ALWAYS', ...INSECURE_GATED];
const ENROLLMENT = /^"https:\/\/[a-z0-9.-]+\.[a-z]{2,}\/v1\/device"$/;

export function readConfig(file) {
  const values = {};
  for (const line of readFileSync(file, 'utf8').split('\n')) {
    const at = line.indexOf('=');
    if (line.startsWith('CONFIG_') && at > 0) values[line.slice(0, at)] = line.slice(at + 1);
  }
  return values;
}

// Return the secure version, or throw listing every missing guard.
export function checkConfig(file, required = REQUIRED) {
  const values = readConfig(file);
  const errors = Object.entries(required).filter(([key, want]) => values[key] !== want)
    .map(([key, want]) => `${key} must be ${want}`);
  errors.push(...FORBIDDEN.filter((key) => values[key] === 'y').map((key) => `${key} must not be set`));
  if (!ENROLLMENT.test(values.CONFIG_TEMPO_ENROLLMENT_URL ?? '')) {
    errors.push('CONFIG_TEMPO_ENROLLMENT_URL must be the HTTPS .../v1/device endpoint');
  }
  const secure = values.CONFIG_BOOTLOADER_APP_SECURE_VERSION ?? '';
  const width = values.CONFIG_BOOTLOADER_APP_SEC_VER_SIZE_EFUSE_FIELD ?? '';
  if (!/^\d+$/.test(secure) || !/^\d+$/.test(width) || Number(secure) < 1 || Number(secure) > Number(width)) {
    errors.push('CONFIG_BOOTLOADER_APP_SECURE_VERSION must be set between 1 and the eFuse field size');
  }
  if (errors.length) throw new Error(`Production configuration rejected:\n  ${errors.join('\n  ')}`);
  return Number(secure);
}

export const crc32 = (data) => zlib.crc32(data) >>> 0;
const sha256 = (data) => createHash('sha256').update(data).digest();

// Check the appended RSA-3072 Secure Boot v2 signature sector covers this image.
export function signatureBlock(blob) {
  if (blob.length < 2 * SECTOR || blob.length % SECTOR) throw new Error('Image is not a Secure Boot v2 signed binary');
  const sector = blob.subarray(blob.length - SECTOR);
  if (!sector.subarray(0, 4).equals(Buffer.from([0xe7, 0x02, 0x00, 0x00]))) {
    throw new Error('Missing RSA Secure Boot v2 signature block');
  }
  if (crc32(sector.subarray(0, 1196)) !== sector.readUInt32LE(1196)) throw new Error('Corrupt Secure Boot v2 signature block');
  if (!sector.subarray(4, 36).equals(sha256(blob.subarray(0, blob.length - SECTOR)))) {
    throw new Error('Secure Boot v2 signature does not cover this image');
  }
}

export function secureVersion(blob) {
  if (blob.length < 112 || blob[0] !== 0xe9 || blob.readUInt32LE(32) !== 0xabcd5432) {
    throw new Error('Invalid ESP application descriptor');
  }
  return blob.readUInt32LE(36);
}

export function espsecure() {
  for (const name of ['espsecure', 'espsecure.py']) {
    const found = spawnSync('sh', ['-c', 'command -v "$1"', 'sh', name], { encoding: 'utf8' });
    if (found.status === 0) return [found.stdout.trim()];
  }
  throw new Error('espsecure not found; activate the pinned ESP-IDF environment first');
}

export function outsideRepository(key, root = ROOT) {
  const resolved = realpathSync(key);
  const base = realpathSync(root);
  const relative = path.relative(base, resolved);
  if (!relative.startsWith('..') && !path.isAbsolute(relative)) {
    throw new Error('Keep the signing key outside the repository and build-host/');
  }
  return resolved;
}

const run = (tool, ...args) => spawnSync(tool[0], [...tool.slice(1), ...args.map(String)], { encoding: 'utf8' });

export function must(tool, ...args) {
  const result = run(tool, ...args);
  if (result.status !== 0) throw new Error(`espsecure ${args[0]} failed:\n${result.stdout}${result.stderr}${result.error ?? ''}`);
}

// Sign, then prove good/tampered/wrong-key behaviour before anything is published.
export function signAndProve(tool, unsigned, key, signed) {
  const scratch = mkdtempSync(path.join(path.dirname(signed), '.proof-'));
  try {
    const pub = path.join(scratch, 'public.pem');
    must(tool, 'sign-data', '--version', '2', '--keyfile', key, '--output', signed, unsigned);
    must(tool, 'extract-public-key', '--version', '2', '--keyfile', key, pub);
    must(tool, 'verify-signature', '--version', '2', '--keyfile', pub, signed);
    signatureBlock(readFileSync(signed));
    const tampered = path.join(scratch, 'tampered.bin');
    const blob = Buffer.from(readFileSync(signed));
    blob[Math.floor(blob.length / 3)] ^= 0x01;
    writeFileSync(tampered, blob);
    if (run(tool, 'verify-signature', '--version', '2', '--keyfile', pub, tampered).status === 0) {
      throw new Error('Tampered image was accepted; refusing to continue');
    }
    const wrongKey = path.join(scratch, 'disposable.pem');
    const wrong = path.join(scratch, 'wrong-key.bin');
    must(tool, 'generate-signing-key', '--version', '2', '--scheme', 'rsa3072', wrongKey);
    must(tool, 'sign-data', '--version', '2', '--keyfile', wrongKey, '--output', wrong, unsigned);
    if (run(tool, 'verify-signature', '--version', '2', '--keyfile', pub, wrong).status === 0) {
      throw new Error('Wrong-key image was accepted; refusing to continue');
    }
  } finally {
    rmSync(scratch, { recursive: true, force: true });
  }
}

const runChecked = (command, args) => {
  const result = spawnSync(command, args, { stdio: 'inherit' });
  if (result.status !== 0) throw new Error(`${path.basename(args[0] ?? command)} ${args.at(-1)} failed`);
};

export function compileFirmware(build, { sdk = process.env.IDF_PATH, run: exec = runChecked, required = REQUIRED } = {}) {
  if (!sdk) throw new Error('IDF_PATH is not set; activate the pinned ESP-IDF environment first');
  const python = 'python3'; // The ESP-IDF environment's interpreter after export.sh.
  exec(python, [path.join(ROOT, 'scripts/check-idf-security.py'), sdk]);
  const dir = path.resolve(build);
  mkdirSync(dir, { recursive: true });
  const defaults = PROFILE.map((name) => path.join(FIRMWARE, name)).join(';');
  const idf = [path.join(sdk, 'tools/idf.py'), '-C', FIRMWARE, '-B', dir,
    '-D', `SDKCONFIG=${path.join(dir, 'sdkconfig')}`, '-D', `SDKCONFIG_DEFAULTS=${defaults}`];
  exec(python, [...idf, 'reconfigure']);
  checkConfig(path.join(dir, 'sdkconfig'), required); // Fail before compiling anything.
  exec(python, [...idf, 'build']);
  return checkConfig(path.join(dir, 'sdkconfig'), required);
}

// esptool @file for the signed set: IDF's offsets plus the bootloader it omits under Secure Boot.
export function flashArgs(build) {
  const lines = readFileSync(path.join(build, 'flash_args'), 'utf8').split('\n');
  const files = Object.fromEntries(lines.slice(1).filter((line) => line.trim())
    .map((line) => line.trim().split(/\s+/)).map(([offset, file]) => [file, offset]));
  const offsets = [
    ['bootloader.bin', readConfig(path.join(build, 'sdkconfig')).CONFIG_BOOTLOADER_OFFSET_IN_FLASH],
    ['partition-table.bin', files['partition_table/partition-table.bin']],
    ['ota_data_initial.bin', files['ota_data_initial.bin']],
    [APP, files[APP]],
  ];
  for (const [name, offset] of offsets) if (!offset) throw new Error(`No flash offset for ${name}`);
  return `${lines[0]}\n${offsets.map(([name, offset]) => `${offset} ${name}\n`).join('')}`;
}

export function signBuild(build, key, output, { root = ROOT, tool, signAndProve: sign = signAndProve } = {}) {
  const signingKey = outsideRepository(key, root);
  const version = checkConfig(path.join(build, 'sdkconfig'));
  if (secureVersion(readFileSync(path.join(build, APP))) !== version) {
    throw new Error('Application secure version does not match the production profile');
  }
  mkdirSync(output, { recursive: true });
  const names = ['bootloader.bin', 'partition-table.bin', 'ota_data_initial.bin', APP, PUBLIC_KEY, 'flash_args'];
  for (const name of [...names, 'SHA256SUMS']) { // A failed run must not leave an earlier signed set behind.
    try { unlinkSync(path.join(output, name)); } catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
  const espsecureTool = tool ?? espsecure();
  sign(espsecureTool, path.join(build, APP), signingKey, path.join(output, APP));
  sign(espsecureTool, path.join(build, 'bootloader/bootloader.bin'), signingKey, path.join(output, 'bootloader.bin'));
  for (const name of ['partition_table/partition-table.bin', 'ota_data_initial.bin']) {
    copyFileSync(path.join(build, name), path.join(output, path.basename(name)));
  }
  must(espsecureTool, 'extract-public-key', '--version', '2', '--keyfile', signingKey, path.join(output, PUBLIC_KEY));
  writeFileSync(path.join(output, 'flash_args'), flashArgs(build));
  writeFileSync(path.join(output, 'SHA256SUMS'), names
    .map((name) => `${sha256(readFileSync(path.join(output, name))).toString('hex')}  ${name}\n`).join(''));
  return path.join(output, APP);
}

function options(args, allowed) {
  const parsed = {};
  for (let i = 0; i < args.length; i += 2) {
    const name = args[i].replace(/^--/, '');
    if (!allowed.includes(name) || args[i + 1] === undefined) throw new Error(`Unexpected argument: ${args[i]}`);
    parsed[name] = args[i + 1];
  }
  return parsed;
}

export function main(argv) {
  const [action, ...rest] = argv;
  const build = (value) => path.resolve(value ?? path.join(ROOT, 'build-host/secure-build'));
  const out = (value) => path.resolve(value ?? path.join(ROOT, 'build-host/secure-release'));
  try {
    if (action === 'check-config' && rest.length === 1) {
      console.log(`PASS: production profile, secure version ${checkConfig(rest[0])}`);
    } else if (action === 'check-signature' && rest.length === 1) {
      signatureBlock(readFileSync(rest[0]));
    } else if (action === 'compile') {
      compileFirmware(build(options(rest, ['build-dir'])['build-dir']));
    } else if (action === 'build' || action === 'sign') {
      const opts = options(rest, ['build-dir', 'signing-key', 'output']);
      if (!opts['signing-key']) throw new Error('--signing-key is required');
      outsideRepository(opts['signing-key']); // Refuse before a long compile.
      if (action === 'build') compileFirmware(build(opts['build-dir']));
      const image = signBuild(build(opts['build-dir']), opts['signing-key'], out(opts.output));
      console.log(`PASS: ${image} is Secure Boot v2 signed; tampered and wrong-key images were rejected.`);
    } else {
      throw new Error('Usage: secure-build.mjs check-config <sdkconfig> | check-signature <image> | '
        + 'compile [--build-dir DIR] | build|sign --signing-key KEY [--build-dir DIR] [--output DIR]');
    }
  } catch (error) {
    console.error(`FAIL: ${error.message}`);
    return 1;
  }
  return 0;
}

if (process.argv[1] && realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exitCode = main(process.argv.slice(2));
}
