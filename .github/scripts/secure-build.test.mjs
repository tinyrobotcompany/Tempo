// Secure Boot v2 production profile, signing proof and release wiring, with disposable keys only.
import assert from 'node:assert/strict';
import { createHash, randomBytes } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import * as sb from '../../scripts/secure-build.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const read = (relative) => readFileSync(path.join(ROOT, relative), 'utf8');
const LOCK = path.join(ROOT, 'tests/esptool-requirements.txt');

function scratch(t) {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'tempo-secure-build-'));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  return dir;
}

function sh(command, args, options = {}) {
  const result = spawnSync(command, args, { encoding: 'utf8', ...options });
  assert.equal(result.status, 0, `${command} ${args.join(' ')}\n${result.stdout}${result.stderr}`);
  return result.stdout;
}

// Hash-locked espsecure for macOS host tests; the CI firmware job uses the pinned ESP-IDF one.
function espsecureBin() {
  if (process.env.TEMPO_ESPSECURE_FROM_PATH === '1') {
    return path.dirname(sh('sh', ['-c', 'command -v espsecure']).trim());
  }
  const cache = process.env.TEMPO_TEST_CACHE || path.join(ROOT, 'build-host/dependencies');
  const digest = createHash('sha256').update(readFileSync(LOCK)).digest('hex').slice(0, 16);
  const venv = path.join(cache, `espsecure-${digest}`);
  const python = path.join(venv, 'bin/python');
  if (!existsSync(path.join(venv, 'ready'))) {
    rmSync(venv, { recursive: true, force: true });
    sh('python3', ['-m', 'venv', venv]);
    const options = ['-m', 'pip', 'install', '-q', '--disable-pip-version-check', '--no-cache-dir',
      '--require-hashes', '--no-deps', '--only-binary=:all:'];
    const lines = readFileSync(LOCK, 'utf8').split('\n');
    const start = lines.findIndex((line) => line.startsWith('setuptools=='));
    const backend = path.join(venv, 'build-backend.txt');
    writeFileSync(backend, lines.slice(start, start + 2).join('\n') + '\n');
    // esptool ships only as an sdist; its hashed build backend is installed first.
    sh(python, [...options, '-r', backend]);
    sh(python, [...options, '--no-build-isolation', '--no-binary=esptool', '-r', LOCK]);
    writeFileSync(path.join(venv, 'ready'), '');
  }
  return path.join(venv, 'bin');
}

function profileConfig(changes = {}) {
  const values = sb.readConfig(path.join(ROOT, 'firmware/sdkconfig.production'));
  Object.assign(values, { CONFIG_SECURE_SIGNED_ON_BOOT: 'y', CONFIG_SECURE_SIGNED_ON_UPDATE: 'y' });
  for (const [key, value] of Object.entries(changes)) {
    if (value === null) delete values[key];
    else values[key] = value;
  }
  return Object.entries(values).map(([key, value]) => `${key}=${value}\n`).join('');
}

function appImage(secureVersion = 1, version = '1.0.0') {
  const blob = Buffer.alloc(0x3000);
  blob[0] = 0xe9;
  blob.writeUInt32LE(0xabcd5432, 32);
  blob.writeUInt32LE(secureVersion, 36);
  blob.write(version, 48, 'latin1');
  blob.write('weather_station', 80, 'latin1');
  randomBytes(0x100).copy(blob, 0x1000);
  return blob;
}

function checkText(t, text) {
  const file = path.join(scratch(t), 'sdkconfig');
  writeFileSync(file, text);
  return sb.checkConfig(file);
}

test('production profile passes and every guard is required', (t) => {
  assert.equal(checkText(t, profileConfig()), 1);
  const rejected = (pattern, changes) => assert.throws(() => checkText(t, profileConfig(changes)), pattern);
  for (const key of Object.keys(sb.REQUIRED)) rejected(new RegExp(key), { [key]: null });
  rejected(/ENROLLMENT_URL/, { CONFIG_TEMPO_ENROLLMENT_URL: '""' });
  rejected(/ENROLLMENT_URL/, { CONFIG_TEMPO_ENROLLMENT_URL: '"http://localhost/v1/device"' });
  rejected(/HMAC/, { CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID: '-1' });
  rejected(/HMAC/, { CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID: null });
  rejected(/HMAC/, { CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID: '2' });
  rejected(/SECURE_VERSION/, { CONFIG_BOOTLOADER_APP_SECURE_VERSION: null });
  rejected(/SECURE_VERSION/, { CONFIG_BOOTLOADER_APP_SECURE_VERSION: '0' });
  rejected(/SECURE_VERSION/, { CONFIG_BOOTLOADER_APP_SECURE_VERSION: '17' });
  rejected(/MODE_DEVELOPMENT/, { CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT: null });
  rejected(/MODE_RELEASE/, { CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE: 'y' });
  rejected(/REQUIRE_ALREADY/, { CONFIG_SECURE_BOOT_REQUIRE_ALREADY_ENABLED: null });
  rejected(/REQUIRE_ALREADY/, { CONFIG_SECURE_FLASH_REQUIRE_ALREADY_ENABLED: null });
  for (const key of sb.FORBIDDEN) rejected(new RegExp(key), { [key]: 'y' });
  const generated = path.join(scratch(t), 'sdkconfig');
  const cli = () => spawnSync(process.execPath, [path.join(ROOT, 'scripts/secure-build.mjs'), 'check-config', generated], { encoding: 'utf8' });
  writeFileSync(generated, profileConfig());
  assert.equal(cli().status, 0);
  writeFileSync(generated, profileConfig({ CONFIG_SECURE_BOOT: null }));
  assert.equal(cli().status, 1);
});

test('profile commits the documented choices', () => {
  const profile = read('firmware/sdkconfig.production');
  for (const line of ['CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y', 'CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=y',
    '# CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES is not set', 'CONFIG_BOOTLOADER_APP_SECURE_VERSION=1',
    'CONFIG_TEMPO_ENROLLMENT_URL="https://tempo-westeurope-api.politemushroom-8174e848.westeurope.azurecontainerapps.io/v1/device"',
    'CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=5', 'CONFIG_SECURE_BOOT_REQUIRE_ALREADY_ENABLED=y',
    'CONFIG_SECURE_FLASH_REQUIRE_ALREADY_ENABLED=y', 'CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC=y']) {
    assert.ok(profile.includes(line + '\n'), line);
  }
  // The insecure menu is open only for REQUIRE_ALREADY_ENABLED; nothing else in it is enabled.
  const keys = Object.keys(sb.readConfig(path.join(ROOT, 'firmware/sdkconfig.production')));
  assert.deepEqual(sb.INSECURE_GATED.filter((key) => keys.includes(key)), []);
  assert.ok(!profile.includes('SECURE_BOOT_SIGNING_KEY'));
  for (const name of sb.PROFILE) assert.ok(existsSync(path.join(ROOT, 'firmware', name)), name);
  for (const name of ['firmware/sdkconfig.defaults', 'firmware/sdkconfig.production', 'firmware/sdkconfig']) {
    assert.equal(sb.readConfig(path.join(ROOT, name)).CONFIG_LWIP_MAX_SOCKETS, '16', name);
  }
  const releasing = read('docs/RELEASING.md');
  for (const key of [...Object.keys(sb.REQUIRED), ...sb.FORBIDDEN, 'CONFIG_TEMPO_ENROLLMENT_URL']) {
    assert.ok(releasing.includes(key), key);
  }
  assert.ok(releasing.includes('node scripts/secure-build.mjs build --signing-key'));
});

test('compile layers the profile and stops before building when the gate fails', (t) => {
  const dir = scratch(t);
  const build = path.join(dir, 'build');
  const calls = [];
  const run = (command, args) => {
    const step = args.at(-1);
    calls.push(step === dir ? path.basename(args[0]) : step);
    if (step === 'reconfigure') {
      const defaults = args.find((arg) => arg.startsWith('SDKCONFIG_DEFAULTS=')).split('=')[1].split(';');
      assert.deepEqual(defaults.map((file) => path.basename(file)), sb.PROFILE);
      writeFileSync(path.join(build, 'sdkconfig'), profileConfig());
    }
  };
  assert.equal(sb.compileFirmware(build, { sdk: dir, run }), 1);
  assert.deepEqual(calls, ['check-idf-security.py', 'reconfigure', 'build']);
  calls.length = 0;
  assert.throws(() => sb.compileFirmware(build, { sdk: dir, run, required: { ...sb.REQUIRED, CONFIG_TEMPO_TEST_ONLY: 'y' } }),
    /CONFIG_TEMPO_TEST_ONLY/);
  assert.ok(!calls.includes('build'));
});

test('secure layout keeps app slots and encrypts nvs_keys', () => {
  const table = (name) => Object.fromEntries(read(`firmware/${name}`).split('\n')
    .filter((line) => line.trim() && !line.startsWith('#'))
    .map((line) => line.split(',').map((field) => field.trim()))
    .map(([n, type, sub, offset, size, flags]) => [n, [type, sub, Number(offset), Number(size), flags]]));
  const secure = table(sb.PARTITIONS);
  const plain = table('partitions.csv');
  assert.equal(plain.nvs[2], 0x9000); // Unlocked development layout is unchanged.
  for (const name of ['ota_0', 'ota_1', 'phone_tls']) assert.deepEqual(secure[name], plain[name]);
  assert.deepEqual(secure.nvs_keys.slice(0, 2), ['data', 'nvs_keys']);
  assert.equal(secure.nvs_keys[4], 'encrypted');
  const spans = Object.values(secure).map(([, , offset, size]) => [offset, offset + size]).sort((a, b) => a[0] - b[0]);
  assert.ok(spans[0][0] >= 0x10000 + 0x1000); // After the moved partition table.
  spans.slice(1).forEach((span, i) => assert.ok(spans[i][1] <= span[0]));
  assert.ok(spans.at(-1)[1] <= 32 * 1024 * 1024);
  for (const name of ['ota_0', 'ota_1']) assert.equal(secure[name][2] % 0x10000 + secure[name][3] % 0x10000, 0);
});

test('layout identifier tracks hardware security', (t) => {
  const dir = scratch(t);
  const source = path.join(dir, 'layout.c');
  writeFileSync(source, '#include <stdio.h>\n#include "station_update.h"\nint main(void){puts(TEMPO_UPDATE_LAYOUT);}\n');
  const include = path.join(ROOT, 'firmware/components/station_core/include');
  const layout = (...flags) => {
    const binary = path.join(dir, 'layout');
    sh('cc', [...flags, '-I', include, source, '-o', binary]);
    return sh(binary, []).trim();
  };
  assert.equal(layout(), 'ab-8mb-https-v1');
  assert.equal(layout('-DCONFIG_SECURE_BOOT_V2_ENABLED=1'), 'ab-8mb-https-v1');
  assert.equal(layout('-DCONFIG_SECURE_BOOT_V2_ENABLED=1', '-DCONFIG_SECURE_FLASH_ENC_ENABLED=1'), sb.LAYOUT);
  assert.match(read('scripts/ota-release.py'), new RegExp(`^LAYOUT = '${sb.LAYOUT}'`, 'm'));
});

test('signing proof with real espsecure and disposable keys', async (t) => {
  const bin = espsecureBin();
  const tool = [path.join(bin, 'python'), '-m', 'espsecure'];
  const env = { ...process.env, PATH: `${bin}${path.delimiter}${process.env.PATH}` };
  const dir = scratch(t);
  const key = path.join(dir, 'disposable-signing-key.pem');
  sb.must(tool, 'generate-signing-key', '--version', '2', '--scheme', 'rsa3072', key);

  await t.test('signed image verifies; tampered and corrupt images fail', () => {
    const unsigned = path.join(dir, 'app.bin');
    const signed = path.join(dir, 'signed.bin');
    writeFileSync(unsigned, appImage());
    sb.signAndProve(tool, unsigned, key, signed);
    const blob = readFileSync(signed);
    sb.signatureBlock(blob);
    assert.equal(sb.secureVersion(blob), 1);
    assert.throws(() => sb.signatureBlock(readFileSync(unsigned)), /Secure Boot v2/);
    const tampered = Buffer.from(blob);
    tampered[0x1010] ^= 1;
    assert.throws(() => sb.signatureBlock(tampered), /does not cover/);
    const corrupt = Buffer.from(blob);
    corrupt[blob.length - 4096 + 100] ^= 1;
    assert.throws(() => sb.signatureBlock(corrupt), /Corrupt/);
    assert.ok(!blob.includes(readFileSync(key)));
  });

  await t.test('proof fails closed when the verifier accepts bad images', () => {
    const unsigned = path.join(dir, 'app.bin');
    const permissive = path.join(dir, 'permissive.sh');
    writeFileSync(permissive, `#!/bin/sh\n[ "$1" = verify-signature ] && exit 0\nexec '${tool[0]}' -m espsecure "$@"\n`, { mode: 0o755 });
    assert.throws(() => sb.signAndProve([permissive], unsigned, key, path.join(dir, 'out.bin')), /Tampered image was accepted/);
    assert.throws(() => sb.signAndProve(tool, unsigned, path.join(dir, 'missing.pem'), path.join(dir, 'out.bin')), /sign-data failed/);
  });

  await t.test('sign build writes the USB flash set and feeds the OTA package', () => {
    const build = path.join(dir, 'build');
    const output = path.join(dir, 'release');
    const repo = path.join(dir, 'repo');
    for (const name of ['bootloader', 'partition_table']) mkdirSync(path.join(build, name), { recursive: true });
    mkdirSync(repo);
    writeFileSync(path.join(build, 'sdkconfig'), profileConfig({ CONFIG_BOOTLOADER_OFFSET_IN_FLASH: '0x2000' }));
    writeFileSync(path.join(build, 'flash_args'), '--flash-mode dio --flash-freq 80m --flash-size keep\n'
      + '0x10000 partition_table/partition-table.bin\n0x17000 ota_data_initial.bin\n'
      + `0x20000 ${sb.APP}\n`);
    writeFileSync(path.join(build, sb.APP), appImage());
    writeFileSync(path.join(build, 'bootloader/bootloader.bin'), Buffer.concat([Buffer.from([0xe9]), Buffer.alloc(0x2fff)]));
    writeFileSync(path.join(build, 'partition_table/partition-table.bin'), 'table');
    writeFileSync(path.join(build, 'ota_data_initial.bin'), Buffer.alloc(0x2000, 0xff));
    const inside = path.join(repo, 'key.pem');
    copyFileSync(key, inside);
    const options = { root: repo, tool };
    assert.throws(() => sb.signBuild(build, inside, output, options), /outside the repository/);
    assert.ok(!existsSync(output));
    assert.throws(() => sb.signBuild(build, key, output, { ...options, signAndProve: () => { throw new Error('signing failed'); } }),
      /signing failed/);
    assert.ok(!existsSync(path.join(output, sb.APP))); // No stale signed image survives a failure.
    const image = sb.signBuild(build, key, output, options);
    const cli = spawnSync(process.execPath, [path.join(ROOT, 'scripts/secure-build.mjs'), 'sign', '--signing-key',
      path.join(ROOT, 'firmware/sdkconfig.production'), '--build-dir', build, '--output', output], { encoding: 'utf8', env });
    assert.equal(cli.status, 1);
    assert.match(cli.stderr, /outside the repository/);
    assert.deepEqual(readdirSync(output).sort(), ['SHA256SUMS', 'bootloader.bin', 'flash_args', 'ota_data_initial.bin',
      'partition-table.bin', sb.PUBLIC_KEY, sb.APP].sort());
    assert.equal(readFileSync(path.join(output, 'flash_args'), 'utf8'), '--flash-mode dio --flash-freq 80m --flash-size keep\n'
      + '0x2000 bootloader.bin\n0x10000 partition-table.bin\n0x17000 ota_data_initial.bin\n'
      + `0x20000 ${sb.APP}\n`);
    assert.match(readFileSync(path.join(output, sb.PUBLIC_KEY), 'utf8'), /^-----BEGIN PUBLIC KEY-----/);
    sb.must(tool, 'verify-signature', '--version', '2', '--keyfile', path.join(output, sb.PUBLIC_KEY), image);
    assert.match(readFileSync(path.join(output, 'SHA256SUMS'), 'utf8'), new RegExp(sb.PUBLIC_KEY));
    sb.signatureBlock(readFileSync(path.join(output, 'bootloader.bin')));
    for (const name of readdirSync(output)) assert.ok(!readFileSync(path.join(output, name)).includes('PRIVATE'), name);
    writeFileSync(path.join(build, sb.APP), appImage(0));
    assert.throws(() => sb.signBuild(build, key, output, options), /secure version/);
    // ota-release.py packages only the signed image.
    const otaKey = path.join(dir, 'ota.pem');
    const otaPublic = path.join(dir, 'ota-public.pem');
    sh('openssl', ['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', otaKey]);
    sh('openssl', ['pkey', '-in', otaKey, '-pubout', '-out', otaPublic]);
    const pack = (file) => spawnSync('python3', [path.join(ROOT, 'scripts/ota-release.py'), 'package', '--image', file,
      '--version', '1.0.0', '--key', otaKey, '--output', path.join(dir, 'ota')], { encoding: 'utf8', env });
    // The signed image passes the Secure Boot check; this disposable OTA key then fails the committed public key.
    assert.doesNotMatch(pack(image).stderr, /Secure Boot v2/);
    assert.match(pack(path.join(build, sb.APP)).stderr, /Secure Boot v2/);
  });
});

test('signature checker CLI accepts only a covering signature block', (t) => {
  const dir = scratch(t);
  const image = Buffer.concat([appImage(), Buffer.alloc(0x1000, 0xff)]);
  const block = Buffer.alloc(1196);
  Buffer.from([0xe7, 2, 0, 0]).copy(block);
  const body = image.subarray(0, image.length - 0x1000);
  createHash('sha256').update(body).digest().copy(block, 4);
  const sector = Buffer.alloc(0x1000, 0xff);
  block.copy(sector);
  sector.writeUInt32LE(sb.crc32(block), 1196);
  const good = Buffer.concat([body, sector]);
  const file = path.join(dir, 'image.bin');
  const cli = (blob) => {
    writeFileSync(file, blob);
    return spawnSync(process.execPath, [path.join(ROOT, 'scripts/secure-build.mjs'), 'check-signature', file], { encoding: 'utf8' });
  };
  assert.equal(cli(good).status, 0);
  assert.equal(cli(body).status, 1);
});

test('release workflow signs and proves the app before signing the manifest', () => {
  const workflow = read('.github/workflows/ota-release.yml');
  const step = (name) => workflow.split(`- name: ${name}`)[1].split('\n      - name: ')[0];
  const build = step('Build credential-free release firmware');
  const sign = step('Secure Boot sign and verify application');
  const manifest = step('Sign and verify release manifest');
  assert.ok(build.includes('node scripts/secure-build.mjs compile --build-dir firmware/build\n'));
  assert.ok(!workflow.includes('TEMPO_IDENTITY_HMAC_KEY_ID'));
  assert.ok(!build.includes('secrets.'));
  assert.ok(sign.includes('SECURE_BOOT_SIGNING_KEY: ${{ secrets.SECURE_BOOT_SIGNING_KEY }}'));
  assert.ok(sign.includes('node scripts/secure-build.mjs sign'));
  assert.ok(sign.includes(`trap 'rm -f "$signing_key"' EXIT`));
  assert.ok(!sign.includes('OTA_SIGNING_PRIVATE_KEY'));
  assert.ok(!manifest.includes('SECURE_BOOT_SIGNING_KEY'));
  assert.ok(manifest.includes('--image build-host/secure-release/weather_station.bin'));
  const at = (name) => workflow.indexOf(`- name: ${name}`);
  assert.ok(at('Build credential-free release firmware') < at('Secure Boot sign and verify application'));
  assert.ok(at('Secure Boot sign and verify application') < at('Sign and verify release manifest'));
  assert.equal(workflow.split('secrets.SECURE_BOOT_SIGNING_KEY').length, 2);
  assert.ok(!workflow.includes('idf.py -C firmware build'));
});

test('pull requests build and sign the production profile on the pinned SDK', () => {
  const ci = read('.github/workflows/ci.yaml');
  const job = ci.split('\n  firmware:\n')[1].split('\n  changeset:\n')[0];
  const step = job.split('- name: Build and Secure Boot sign the production profile')[1];
  assert.ok(job.includes('ref: 051e4c4eb20346e043cdd47bc388fde75ff8af63'));
  assert.ok(step.includes('TEMPO_ESPSECURE_FROM_PATH=1 node --test .github/scripts/secure-build.test.mjs'));
  assert.ok(step.includes('generate-signing-key --version 2 --scheme rsa3072 "$key_dir/disposable.pem"'));
  assert.ok(step.includes('node scripts/secure-build.mjs build --build-dir build-host/secure-build --signing-key "$key_dir/disposable.pem"'));
  assert.ok(!job.includes('secrets.'));
});
