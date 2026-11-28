#!/usr/bin/env node
// Tempo factory lockdown: Secure Boot v2 + flash encryption (Development mode)
// and the HMAC_UP identity key, burned from the host in Espressif's documented order.
//
// Safe by default. Without --execute it only prints the plan and runs host-side
// checks; it never opens a serial port. With --execute every device command needs
// its own typed confirmation, and espefuse asks for 'BURN' again. Key material is
// generated in this process and handed to espefuse through an inherited descriptor
// (/dev/fd/3): it is never printed, logged, sent over serial or written to disk.
//
// Node.js 22+, macOS factory host. See docs/FACTORY_RUNBOOK.md. Only the ESP32-P4
// profile is reviewed.
import { spawn, spawnSync } from 'node:child_process';
import { createHash, randomBytes } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

// Reviewed per-chip layouts. Other chips are accepted as a parameter but refused
// until their eFuse layout has been reviewed (docs/FACTORY_RUNBOOK.md, rehearsal).
const PROFILES = {
  esp32p4: {
    minRevision: 3,
    flashKey: ['BLOCK_KEY0', 'XTS_AES_128_KEY', 32],
    hmac: ['BLOCK_KEY5', 'HMAC_UP', 32],
    // DIGEST0 is the release signing key, DIGEST1 an independent backup key
    // kept separately for rotation; only the third slot is revoked.
    digests: [['BLOCK_KEY2', 'SECURE_BOOT_DIGEST0'], ['BLOCK_KEY3', 'SECURE_BOOT_DIGEST1']],
    // Layout ab-8mb-secure-v1 (firmware/partitions-secure.csv): table moved to 0x10000.
    offsets: { 'bootloader.bin': 0x2000, 'partition-table.bin': 0x10000 },
    // Mirrors ESP-IDF v6.1 esp32p4 secure-features defaults, minus the
    // Release-only DIS_DOWNLOAD_MANUAL_ENCRYPT so USB re-flashing stays possible.
    hardening: [['DIS_DIRECT_BOOT', 1], ['DIS_PAD_JTAG', 1], ['DIS_USB_JTAG', 1],
      ['SOFT_DIS_JTAG', 7], ['SPI_DOWNLOAD_MSPI_DIS', 1]],
    revoke: ['SECURE_BOOT_KEY_REVOKE2'],
  },
};
const UNREVIEWED = new Set(['esp32s3', 'esp32c6']);
// The production profile itself is checked by scripts/secure-build.mjs check-config
// (one list, owned by the release tooling). Only what this factory plan relies on
// is checked here: the eFuse key it burns, the HMAC slot, the layout and USB re-flash.
const FACTORY_CONFIG = {
  CONFIG_SECURE_FLASH_ENCRYPTION_KEY_SOURCE_EFUSES: 'y', CONFIG_SECURE_FLASH_ENCRYPTION_AES128: 'y',
  CONFIG_PARTITION_TABLE_OFFSET: '0x10000',
};
const FACTORY_FORBIDDEN = ['CONFIG_SECURE_DISABLE_ROM_DL_MODE', 'CONFIG_SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS',
  'CONFIG_SECURE_FLASH_ENCRYPTION_KEY_SOURCE_KEY_MGR'];
const SIGNED_SET = ['bootloader.bin', 'partition-table.bin', 'ota_data_initial.bin', 'weather_station.bin'];
const PUBLIC_KEY = 'secure_boot_public_key.pem';
// Everything scripts/secure-build.mjs sign writes into the signed folder.
const SIGNED_FILES = [...SIGNED_SET, PUBLIC_KEY, 'flash_args'];
const PEM = '-----BEGIN PUBLIC KEY-----\\n(?:[A-Za-z0-9+/=]{1,64}\\n){2,12}-----END PUBLIC KEY-----\\n?';
const IDENTITY_RE = new RegExp('Manufacturing PUBLIC identity for ([0-9a-f]{32}):\\s*\\n(' + PEM + ')', 'g');
const ANSI_RE = /\x1b\[[0-9;]*[A-Za-z]/g;
const KEY_PIPE = '<KEY-PIPE>';
const VERIFY = '<VERIFY>';

class Refused extends Error {}

const blockIndex = block => Number(block.replace('BLOCK_KEY', ''));
const raw = (summary, name) => BigInt(summary[name].raw_value);
const purpose = (summary, block) => summary['KEY_PURPOSE_' + blockIndex(block)].value;
const blockEmpty = (summary, block) =>
  raw(summary, block) === 0n && summary[block].readable && purpose(summary, block) === 'USER';
const sha256 = data => createHash('sha256').update(data).digest('hex');
const quote = word => (/^[\w@%+=:,./-]+$/.test(word) ? word : `'${word.replaceAll("'", "'\\''")}'`);
const shellJoin = words => words.map(quote).join(' ');

function readLine() {
  // Byte by byte, so nothing meant for espefuse's own prompt is consumed here.
  const bytes = [];
  const one = Buffer.alloc(1);
  for (;;) {
    let n;
    try {
      n = fs.readSync(0, one, 0, 1, null);
    } catch (error) {
      if (error.code === 'EAGAIN') continue;
      if (error.code === 'EOF') return null;
      throw error;
    }
    if (n === 0) return bytes.length ? Buffer.from(bytes).toString() : null;
    if (one[0] === 10) return Buffer.from(bytes).toString();
    bytes.push(one[0]);
  }
}

function ask(prompt) {
  process.stdout.write(prompt);
  return (readLine() ?? '').trim();
}

function openssl(args, input) {
  const result = spawnSync('openssl', args, { input });
  if (result.status !== 0) {
    throw new Refused('openssl rejected the key: ' + String(result.stderr).trim().slice(0, 200));
  }
  return result.stdout;
}

function checkPublicPem(text, kind) {
  // Accept exactly one public key; any private material is refused unread.
  if (text.toUpperCase().includes('PRIVATE')) {
    throw new Refused('Expected a PUBLIC key. Never give this tool a private key.');
  }
  if ((text.match(/-----BEGIN PUBLIC KEY-----/g) ?? []).length !== 1) {
    throw new Refused('Expected exactly one PEM public key.');
  }
  const details = openssl(['pkey', '-pubin', '-noout', '-text'], text).toString();
  if (kind === 'rsa3072' && !details.includes('Public-Key: (3072 bit)')) {
    throw new Refused('The Secure Boot key must be an RSA-3072 public key (ECDSA is not recommended on ESP32-P4).');
  }
  if (kind === 'p256' && !details.includes('prime256v1') && !details.includes('P-256')) {
    throw new Refused('The identity key must be an EC P-256 public key.');
  }
}

const deviceId = pem => sha256(openssl(['pkey', '-pubin', '-outform', 'DER'], pem)).slice(0, 32);

function runHost(command, failure) {
  const result = spawnSync(command[0], command.slice(1), { encoding: 'utf8' });
  if (result.status !== 0) throw new Refused(failure);
  return result.stdout;
}

function readConfig(file) {
  const values = {};
  for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
    const at = line.indexOf('=');
    if (line.startsWith('CONFIG_') && at > 0) values[line.slice(0, at)] = line.slice(at + 1);
  }
  return values;
}

function flashArguments(signed, profile) {
  // The signed set's own flash_args (hash-checked), every image encrypted on the chip.
  const [options, ...lines] = fs.readFileSync(path.join(signed, 'flash_args'), 'utf8').split('\n');
  const tokens = [...options.split(/\s+/).filter(Boolean), '--encrypt'];
  const images = [];
  for (const line of lines.filter(l => l.trim())) {
    const [offset, name, extra] = line.trim().split(/\s+/);
    if (extra !== undefined || !/^0x[0-9a-fA-F]+$/.test(offset) || !SIGNED_SET.includes(name)) {
      throw new Refused(`flash_args line '${line.trim()}' is not an image of the signed set.`);
    }
    const file = fs.realpathSync(path.join(signed, name));
    tokens.push(offset, file);
    images.push([parseInt(offset, 16), fs.statSync(file).size, name]);
  }
  if (images.map(([, , n]) => n).sort().join() !== [...SIGNED_SET].sort().join()) {
    throw new Refused('flash_args does not flash exactly the signed release set.');
  }
  for (const [name, offset] of Object.entries(profile.offsets)) {
    if (!images.some(([o, , n]) => o === offset && n === name)) {
      throw new Refused(`${name} must be flashed at 0x${offset.toString(16)} (layout ab-8mb-secure-v1).`);
    }
  }
  images.sort((a, b) => a[0] - b[0]);
  for (let i = 0; i + 1 < images.length; i++) {
    const [start, size, name] = images[i];
    const [following, , other] = images[i + 1];
    if (start + size > following) {
      throw new Refused(`${name} (0x${size.toString(16)} bytes at 0x${start.toString(16)}) overlaps ${other} at 0x${following.toString(16)}.`);
    }
  }
  return tokens;
}

const der = pem => openssl(['pkey', '-pubin', '-outform', 'DER'], pem).toString('hex');

function checkBuild(args, profile) {
  // Host-only checks that must pass before anything touches a device.
  const sdkconfig = path.join(args.buildDir, 'sdkconfig');
  if (!fs.existsSync(sdkconfig)) throw new Refused(`${sdkconfig} is missing; run scripts/secure-build.mjs build first.`);
  const gate = path.join(ROOT, 'scripts', 'secure-build.mjs');
  if (!fs.existsSync(gate)) throw new Refused('scripts/secure-build.mjs (the production config gate) is missing.');
  const checked = spawnSync(process.execPath, [gate, 'check-config', sdkconfig], { encoding: 'utf8' });
  if (checked.status !== 0) {
    throw new Refused('secure-build.mjs check-config rejected the build:\n' + (checked.stderr || checked.stdout).trim());
  }
  const config = readConfig(sdkconfig);
  if (config.CONFIG_IDF_TARGET !== `"${args.chip}"`) {
    throw new Refused(`Build target ${config.CONFIG_IDF_TARGET} does not match --chip ${args.chip}.`);
  }
  const wrong = Object.entries(FACTORY_CONFIG).filter(([k, v]) => config[k] !== v).map(([k, v]) => `${k} (must be ${v})`);
  wrong.push(...FACTORY_FORBIDDEN.filter(k => config[k] === 'y').map(k => `${k} (must not be set)`));
  const hmacId = String(blockIndex(profile.hmac[0]));
  if (config.CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID !== hmacId) wrong.push(`CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID (must be ${hmacId})`);
  if (wrong.length) throw new Refused('Build does not match this factory plan: ' + wrong.join(', '));
  const signed = args.signedDir;
  const sums = path.join(signed, 'SHA256SUMS');
  if (!fs.existsSync(sums)) throw new Refused(`${sums} is missing; run scripts/secure-build.mjs build first.`);
  const listed = Object.fromEntries(fs.readFileSync(sums, 'utf8').split('\n').filter(Boolean)
    .map(line => line.split('  ')).map(([hash, name]) => [name, hash]));
  if (Object.keys(listed).sort().join() !== [...SIGNED_FILES].sort().join()) {
    throw new Refused('SHA256SUMS does not list exactly the signed release set.');
  }
  for (const [name, want] of Object.entries(listed)) {
    if (sha256(fs.readFileSync(path.join(signed, name))) !== want) {
      throw new Refused(`${name} does not match SHA256SUMS; rebuild the signed set.`);
    }
  }
  if (der(fs.readFileSync(args.signingPublicKey)) !== der(fs.readFileSync(path.join(signed, PUBLIC_KEY)))) {
    throw new Refused(`--signing-public-key is not the key that signed this release (${PUBLIC_KEY}).`);
  }
  for (const image of ['bootloader.bin', 'weather_station.bin']) {
    runHost(['espsecure', 'verify-signature', '--version', '2', '--keyfile', args.signingPublicKey,
      path.join(signed, image)], `${image} is not signed by the given Secure Boot key.`);
  }
  const flash = flashArguments(signed, profile);
  return { notes: [], secureDownload: config.CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE === 'y', flash };
}

function espefuse(args, ...rest) {
  // Real devices keep espefuse's own 'BURN' prompt as a second confirmation.
  const target = args.virtualEfuse
    ? ['--virt', '--path-efuse-file', args.virtualEfuse, '--do-not-confirm']
    : ['--port', args.port ?? 'PORT'];
  return ['espefuse', '--chip', args.chip, ...target, ...rest];
}

const esptool = (args, ...rest) => ['esptool', '--chip', args.chip, '--port', args.port ?? 'PORT', ...rest];

function readSummary(args, name) {
  const file = path.join(args.recordDir, name);
  fs.rmSync(file, { force: true }); // Never mistake an old file for a fresh read.
  const command = espefuse(args, 'summary', '--format', 'json', '--file', file);
  const result = spawnSync(command[0], command.slice(1), { stdio: ['inherit', 'ignore', 'inherit'] });
  if (result.status !== 0 || !fs.existsSync(file)) throw new Refused('Could not read the eFuse summary.');
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function checkStartState(summary, profile) {
  // Every eFuse this plan writes must be blank, or already exactly what we want.
  const major = Number(raw(summary, 'WAFER_VERSION_MAJOR'));
  if (major < profile.minRevision) {
    throw new Refused(`Chip revision v${major}.x is below the v${profile.minRevision}.0 this firmware targets.`);
  }
  for (const name of ['DIS_DOWNLOAD_MANUAL_ENCRYPT', 'ENABLE_SECURITY_DOWNLOAD', 'DIS_DOWNLOAD_MODE',
    'SECURE_BOOT_KEY_REVOKE0', 'SECURE_BOOT_KEY_REVOKE1', 'SECURE_BOOT_AGGRESSIVE_REVOKE']) {
    if (raw(summary, name)) throw new Refused(`${name} is already burned; this unit needs failed-unit review, not this script.`);
  }
  if (![0n, 1n].includes(raw(summary, 'SPI_BOOT_CRYPT_CNT'))) {
    throw new Refused('SPI_BOOT_CRYPT_CNT is not 0 or 1: flash encryption was toggled outside this plan.');
  }
  for (const [block, want] of [profile.flashKey, profile.hmac, ...profile.digests]) {
    if (!blockEmpty(summary, block) && purpose(summary, block) !== want) {
      throw new Refused(`${block} already holds ${purpose(summary, block)}; expected empty or ${want}.`);
    }
  }
}

function plan(args, profile, secureDownload, flash) {
  const [feBlock, fePurpose, feLength] = profile.flashKey;
  const [hmBlock, hmPurpose, hmLength] = profile.hmac;
  const has = (block, want) => s => purpose(s, block) === want;
  const step = (title, command, phrase, irreversible, extra = {}) =>
    ({ title, command, phrase, irreversible, device: true, ...extra });
  const steps = [
    step('Erase the whole flash (the verified backup is the only copy of the old contents)',
      esptool(args, 'erase-flash'), 'ERASE FLASH', false,
      { done: s => purpose(s, feBlock) !== 'USER' || raw(s, 'SPI_BOOT_CRYPT_CNT') || raw(s, 'SECURE_BOOT_EN') }),
    step(`Burn a new random flash-encryption key into ${feBlock} (${fePurpose}, read-protected)`,
      espefuse(args, 'burn-key', feBlock, KEY_PIPE, fePurpose), `BURN ${feBlock} ${fePurpose}`, true,
      { done: has(feBlock, fePurpose), keyLength: feLength }),
    step(`Burn a new random identity HMAC key into ${hmBlock} (${hmPurpose}, read-protected)`,
      espefuse(args, 'burn-key', hmBlock, KEY_PIPE, hmPurpose), `BURN ${hmBlock} ${hmPurpose}`, true,
      { done: has(hmBlock, hmPurpose), keyLength: hmLength }),
    ...profile.digests.map(([block, want], n) =>
      step(`Burn the ${['release', 'backup'][n]} Secure Boot public-key digest into ${block} (${want})`,
        espefuse(args, 'burn-key', block, path.join(args.recordDir, `secure-boot-digest${n}.bin`), want),
        `BURN ${block} ${want}`, true, { done: has(block, want) })),
    step('Enable flash encryption in Development mode (SPI_BOOT_CRYPT_CNT = 1)',
      espefuse(args, 'burn-efuse', 'SPI_BOOT_CRYPT_CNT', '1'), 'BURN SPI_BOOT_CRYPT_CNT 1', true,
      { done: s => raw(s, 'SPI_BOOT_CRYPT_CNT') === 1n }),
    step('Disable JTAG, direct boot and download-mode cache access',
      espefuse(args, 'burn-efuse', ...profile.hardening.flat().map(String)), 'BURN HARDENING', true,
      { done: s => profile.hardening.every(([name, value]) => raw(s, name) === BigInt(value)) }),
    step('Revoke the unused third Secure Boot key slot',
      espefuse(args, 'burn-efuse', ...profile.revoke.flatMap(name => [name, '1'])), 'BURN REVOKE UNUSED', true,
      { done: s => profile.revoke.every(n => raw(s, n)) }),
    step('Enable Secure Boot v2 (SECURE_BOOT_EN)', espefuse(args, 'burn-efuse', 'SECURE_BOOT_EN', '1'),
      'BURN SECURE_BOOT_EN', true, { done: s => raw(s, 'SECURE_BOOT_EN') === 1n }),
    step('Write-protect RD_DIS (last eFuse change; no key can be read-protected afterwards)',
      espefuse(args, 'write-protect-efuse', 'RD_DIS'), 'PROTECT RD_DIS', true,
      { done: s => raw(s, 'WR_DIS') & 1n }),
    step('Read the eFuses back and verify the whole plan', [VERIFY], 'yes', false),
    step('Flash the signed firmware, encrypting it on the chip',
      esptool(args, '--before', 'default-reset', '--after', 'no-reset', 'write-flash', ...flash),
      'FLASH SIGNED', false),
  ];
  if (secureDownload) {
    steps.push(step('Switch ROM download mode to Secure Download Mode (blocks espefuse from now on)',
      espefuse(args, 'burn-efuse', 'ENABLE_SECURITY_DOWNLOAD', '1'), 'BURN ENABLE_SECURITY_DOWNLOAD', true,
      { done: s => raw(s, 'ENABLE_SECURITY_DOWNLOAD') }));
  }
  if (args.virtualEfuse) for (const s of steps) if (s.command[0] === 'esptool') s.device = false;
  return steps;
}

function verify(summary, profile, digests) {
  const [feBlock, fePurpose] = profile.flashKey;
  const [hmBlock, hmPurpose] = profile.hmac;
  const problems = [];
  const rdDis = raw(summary, 'RD_DIS');
  const blocks = [[feBlock, fePurpose, false], [hmBlock, hmPurpose, false],
    ...profile.digests.map(([block, want]) => [block, want, true])];
  for (const [block, want, readable] of blocks) {
    if (purpose(summary, block) !== want) problems.push(`${block} purpose is ${purpose(summary, block)}, expected ${want}`);
    if (Boolean((rdDis >> BigInt(blockIndex(block))) & 1n) === readable) problems.push(`${block} read protection is wrong`);
    if (summary[block].writeable) problems.push(`${block} is not write-protected`);
  }
  profile.digests.forEach(([block, want], n) => {
    if (raw(summary, block).toString(16).padStart(64, '0') !== digests[n].toString('hex')) {
      problems.push(`${block} does not hold the expected ${want} value`);
    }
  });
  const expected = Object.fromEntries([...profile.hardening, ['SPI_BOOT_CRYPT_CNT', 1], ['SECURE_BOOT_EN', 1],
    ['DIS_DOWNLOAD_MANUAL_ENCRYPT', 0], ['SECURE_BOOT_KEY_REVOKE0', 0], ['SECURE_BOOT_KEY_REVOKE1', 0],
    ...profile.revoke.map(name => [name, 1])]);
  for (const [name, value] of Object.entries(expected)) {
    if (raw(summary, name) !== BigInt(value)) problems.push(`${name} is ${raw(summary, name)}, expected ${value}`);
  }
  if (!(raw(summary, 'WR_DIS') & 1n)) problems.push('RD_DIS is not write-protected');
  return problems;
}

function confirm(step, number) {
  console.log(`\nStep ${number}: ${step.title}`);
  console.log('  ' + (step.irreversible ? 'IRREVERSIBLE. ' : '') + 'Command: ' + shellJoin(step.command));
  if (ask(`  Type "${step.phrase}" to run this step, anything else stops: `) !== step.phrase) {
    throw new Refused(`Stopped before step ${number}. Nothing in this step was run.`);
  }
}

function confirmSigningBackup(digests) {
  const [release, backup] = digests.map(d => d.toString('hex').slice(0, 16));
  console.log('\nOnly firmware signed by one of these two keys will ever boot on this unit. Without either');
  console.log('private key no new firmware can be installed. Check both offline backups now.');
  console.log(`Release key fingerprint: ${release}\nBackup key fingerprint:  ${backup} (stored separately)`);
  const phrase = `I HAVE VERIFIED OFFLINE BACKUPS OF ${release} AND ${backup}`;
  if (ask(`Type "${phrase}" to continue: `) !== phrase) {
    throw new Refused('Signing key backup not confirmed. Nothing was changed.');
  }
}

function runDevice(command, keyLength) {
  // A key is generated here and written only into the child's descriptor 3.
  return new Promise((resolve, reject) => {
    const argv = command.map(word => (word === KEY_PIPE ? '/dev/fd/3' : word));
    const child = spawn(argv[0], argv.slice(1), { stdio: ['inherit', 'inherit', 'inherit', keyLength ? 'pipe' : 'ignore'] });
    child.on('error', reject);
    child.on('exit', code => resolve(code ?? 1));
    if (keyLength) {
      const key = randomBytes(keyLength);
      child.stdio[3].on('error', () => {});
      child.stdio[3].end(key, () => key.fill(0));
    }
  });
}

async function execute(args, steps, profile, digests) {
  let summary = readSummary(args, 'efuse-start.json');
  checkStartState(summary, profile);
  for (const [i, step] of steps.entries()) {
    const number = i + 1;
    if (step.done && step.done(summary)) {
      console.log(`\nStep ${number}: ${step.title}: already done, skipped.`);
      continue;
    }
    if (!step.device) {
      console.log(`\nStep ${number}: ${step.title}: skipped (virtual eFuses, no device).`);
      continue;
    }
    confirm(step, number);
    if (step.command[0] === VERIFY) {
      summary = readSummary(args, 'efuse-after.json');
      const problems = verify(summary, profile, digests);
      if (problems.length) {
        throw new Refused('Verification failed; do not flash. Follow the failed-unit section:\n  ' + problems.join('\n  '));
      }
      console.log('  eFuses match the plan.');
      continue;
    }
    const code = await runDevice(step.command, step.keyLength);
    if (code !== 0) throw new Refused(`Step ${number} failed (exit ${code}). Re-run this script; finished steps are skipped.`);
    if (step.command[0] === 'espefuse') summary = readSummary(args, `efuse-step${number}.json`);
  }
  console.log('\nLockdown complete. Next: runbook step "First boot and reading the public key".');
}

function extractIdentity(args) {
  const text = fs.readFileSync(args.log, 'latin1').replace(ANSI_RE, '').replaceAll('\r\n', '\n');
  if (text.toUpperCase().includes('PRIVATE')) {
    throw new Refused('The log contains the word PRIVATE. Stop and report this; do not register the unit.');
  }
  const found = new Map();
  for (const m of text.matchAll(IDENTITY_RE)) found.set(m[1] + m[2].replace(/\n*$/, '\n'), [m[1], m[2].replace(/\n*$/, '\n')]);
  if (found.size !== 1) throw new Refused(`Expected one manufacturing identity in the log, found ${found.size}.`);
  const [[identifier, pem]] = found.values();
  checkPublicPem(pem, 'p256');
  if (deviceId(pem) !== identifier) throw new Refused('The logged device ID does not match its public key.');
  fs.mkdirSync(args.out, { recursive: true });
  const file = path.join(args.out, 'identity-public.pem');
  fs.writeFileSync(file, pem);
  console.log(`Device ID: ${identifier}`);
  console.log(`Hostname:  d-${identifier}.${args.suffix}`);
  console.log(`Public key saved to ${file} (public, safe to share).`);
  console.log('Workflow input public_key_base64:');
  console.log(Buffer.from(pem).toString('base64'));
}

async function captureLog(args) {
  if (ask(`Open ${args.port} read-only for ${args.seconds} s and save the boot log? Type "yes": `) !== 'yes') {
    throw new Refused('Stopped; the port was not opened.');
  }
  const out = fs.openSync(args.capture, 'w');
  // stty -f is macOS, -F is Linux; cat only reads the port.
  const reader = spawn('sh', ['-c', 'stty -f "$1" 115200 raw 2>/dev/null || stty -F "$1" 115200 raw; exec cat "$1"',
    'sh', args.port], { stdio: ['ignore', out, 'inherit'] });
  await new Promise(resolve => {
    const timer = setTimeout(() => reader.kill(), args.seconds * 1000);
    reader.on('exit', () => { clearTimeout(timer); resolve(); });
  });
  fs.closeSync(out);
  console.log(`Saved ${args.capture}. Next: --extract-identity.`);
}

function initVirtual(args) {
  if (fs.existsSync(args.virtualEfuse)) throw new Refused(`${args.virtualEfuse} already exists.`);
  fs.mkdirSync(path.dirname(args.virtualEfuse), { recursive: true });
  const revision = String(PROFILES[args.chip].minRevision);
  const result = spawnSync('espefuse', ['--chip', args.chip, '--virt', '--path-efuse-file', args.virtualEfuse,
    '--do-not-confirm', 'burn-efuse', 'WAFER_VERSION_MAJOR_LO', revision], { stdio: ['inherit', 'ignore', 'inherit'] });
  if (result.status !== 0) throw new Refused('Could not create the virtual eFuse file.');
  console.log(`Created a blank virtual ${args.chip} v${revision}.0 eFuse file at ${args.virtualEfuse}.`);
}

function parse(argv) {
  const { values } = parseArgs({
    args: argv,
    options: {
      chip: { type: 'string', default: 'esp32p4' },
      port: { type: 'string' },
      'build-dir': { type: 'string', default: path.join(ROOT, 'build-host', 'secure-build') },
      'signed-dir': { type: 'string', default: path.join(ROOT, 'build-host', 'secure-release') },
      'signing-public-key': { type: 'string' },
      'backup-signing-public-key': { type: 'string' },
      'record-dir': { type: 'string' },
      'virtual-efuse': { type: 'string' },
      execute: { type: 'boolean', default: false },
      'init-virtual': { type: 'boolean', default: false },
      'capture-log': { type: 'string' },
      seconds: { type: 'string', default: '40' },
      'extract-identity': { type: 'string' },
      out: { type: 'string' },
      suffix: { type: 'string', default: 'tempo.tinyrobotcompany.io' },
      help: { type: 'boolean', default: false },
    },
  });
  return {
    chip: values.chip, port: values.port, buildDir: values['build-dir'], signedDir: values['signed-dir'],
    signingPublicKey: values['signing-public-key'], backupSigningPublicKey: values['backup-signing-public-key'],
    recordDir: values['record-dir'], virtualEfuse: values['virtual-efuse'], execute: values.execute,
    initVirtual: values['init-virtual'], capture: values['capture-log'], seconds: Number(values.seconds),
    log: values['extract-identity'], out: values.out, suffix: values.suffix, help: values.help,
  };
}

const USAGE = `macOS only. Usage: node scripts/factory-lockdown.mjs --signing-public-key RELEASE.pem
         --backup-signing-public-key BACKUP.pem [--record-dir UNIT] [--port PORT --execute]
       [--build-dir build-host/secure-build] [--signed-dir build-host/secure-release]
       [--virtual-efuse FILE [--init-virtual]] [--chip esp32p4]
       --capture-log FILE --port PORT [--seconds 40]
       --extract-identity LOG --out DIR [--suffix tempo.tinyrobotcompany.io]
Dry run unless --execute. See docs/FACTORY_RUNBOOK.md.`;

async function main(argv) {
  let args;
  try {
    args = parse(argv);
  } catch (error) {
    console.error(error.message + '\n' + USAGE);
    return 2;
  }
  if (args.help) {
    console.log(USAGE);
    return 0;
  }
  try {
    // Keys reach espefuse through /dev/fd/3, which relies on macOS fdesc semantics.
    if (process.platform !== 'darwin') {
      throw new Refused(`This factory tool runs only on macOS (this is ${process.platform}); see docs/FACTORY_RUNBOOK.md.`);
    }
    if (UNREVIEWED.has(args.chip)) {
      throw new Refused(`${args.chip} is accepted as a parameter, but its eFuse profile is not reviewed yet.`);
    }
    const profile = PROFILES[args.chip];
    if (!profile) throw new Refused(`Unknown chip ${args.chip}.`);
    if (args.log) {
      if (!args.out) throw new Refused('--extract-identity needs --out DIR.');
      extractIdentity(args);
      return 0;
    }
    if (args.capture) {
      if (!args.port) throw new Refused('--capture-log needs --port.');
      await captureLog(args);
      return 0;
    }
    if (args.initVirtual) {
      if (!args.virtualEfuse) throw new Refused('--init-virtual needs --virtual-efuse FILE.');
      initVirtual(args);
      return 0;
    }
    const keys = [args.signingPublicKey, args.backupSigningPublicKey];
    if (!keys.every(key => key && fs.existsSync(key) && fs.statSync(key).isFile())) {
      throw new Refused('Give both --signing-public-key and --backup-signing-public-key (PUBLIC key files).');
    }
    for (const key of keys) checkPublicPem(fs.readFileSync(key, 'latin1'), 'rsa3072');
    if (der(fs.readFileSync(keys[0])) === der(fs.readFileSync(keys[1]))) throw new Refused('The backup signing key must be a different, independent key.');
    if (args.execute && !args.virtualEfuse) {
      if (!args.port) throw new Refused('--execute needs --port (or --virtual-efuse for a rehearsal).');
      const manifestPath = args.recordDir ? path.join(args.recordDir, 'manifest.json') : null;
      const manifest = manifestPath && fs.existsSync(manifestPath) ? JSON.parse(fs.readFileSync(manifestPath, 'utf8')) : {};
      if (manifest.verified !== true) {
        throw new Refused('No verified backup: run scripts/factory-backup.sh first and pass --record-dir.');
      }
      // Enabling Secure Boot or flash encryption turns off the ROM's USB-OTG download
      // stack. USB-Serial/JTAG and a UART bridge keep working (Secure Boot v2 guide).
      if (!['USB-Serial/JTAG', 'UART'].includes(manifest.usb_mode)) {
        throw new Refused('The backup did not record a USB-Serial/JTAG or UART connection; see the runbook.');
      }
    }
    if (!args.recordDir) {
      if (args.execute) throw new Refused('--execute needs --record-dir.');
      args.recordDir = path.join(ROOT, 'build-host', 'factory-plan');
    }
    fs.mkdirSync(args.recordDir, { recursive: true });
    const { notes, secureDownload, flash } = checkBuild(args, profile);
    const digests = keys.map((key, n) => {
      const file = path.join(args.recordDir, `secure-boot-digest${n}.bin`);
      fs.rmSync(file, { force: true });
      runHost(['espsecure', 'digest-sbv2-public-key', '--keyfile', key, '--output', file],
        'Could not compute a Secure Boot key digest.');
      return fs.readFileSync(file);
    });
    if (digests.some(d => d.length !== 32) || digests[0].equals(digests[1])) {
      throw new Refused('Secure Boot digests are malformed or identical.');
    }
    for (const note of notes) console.log('NOTE: ' + note);
    const steps = plan(args, profile, secureDownload, flash);
    if (!args.execute) {
      console.log(`DRY RUN for ${args.chip}. Nothing was sent to any device. Host checks passed.`);
      steps.forEach((s, i) => {
        console.log(`${String(i + 1).padStart(2)}. ${s.irreversible ? 'IRREVERSIBLE ' : ''}${s.title}\n    ${shellJoin(s.command)}`);
      });
      console.log('Re-run with --execute --port PORT --record-dir DIR to perform it, one confirmed step at a time.');
      return 0;
    }
    confirmSigningBackup(digests);
    await execute(args, steps, profile, digests);
    return 0;
  } catch (error) {
    if (!(error instanceof Refused)) throw error;
    console.error('REFUSED: ' + error.message);
    return 2;
  }
}

process.exitCode = await main(process.argv.slice(2));
