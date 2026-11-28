#!/usr/bin/env bash
# Factory lockdown: dry-run safety, host refusals, confirmed step order, key
# handling through a pipe, resumability, verification and identity extraction.
# Uses a stateful espefuse mock; no device or real eFuse is involved.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin" "$tmp/build" "$tmp/unit" "$tmp/repo/scripts"
# Run a copy, so the production config gate next to it can be a stub or #57's real one.
cp "$root/scripts/factory-lockdown.mjs" "$tmp/repo/scripts/"
run="$tmp/repo/scripts/factory-lockdown.mjs"
cat > "$tmp/stub-gate.mjs" <<'JS'
import fs from 'node:fs';
fs.appendFileSync(process.env.CALLS, 'gate ' + process.argv.slice(2).join(' ') + '\n');
process.exit(process.env.MOCK_GATE === 'reject' ? 1 : 0);
JS
cp "$tmp/stub-gate.mjs" "$tmp/repo/scripts/secure-build.mjs"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat > "$tmp/bin/espefuse" <<'MOCK'
#!/usr/bin/env node
const fs = require('node:fs');
let args = process.argv.slice(2);
fs.appendFileSync(process.env.CALLS, 'espefuse ' + args.join(' ') + '\n');
const statePath = process.env.EFUSE_STATE;
const state = JSON.parse(fs.readFileSync(statePath, 'utf8'));
const raw = name => parseInt(state[name].raw_value, 16);
const put = (name, value, extra = {}) => Object.assign(state[name], { raw_value: '0x' + value.toString(16) }, extra);
// Skip the global options: --chip X, --port P / --virt --path-efuse-file F / --do-not-confirm.
while (args.length && args[0].startsWith('--')) {
  args = args.slice(['--chip', '--port', '--path-efuse-file'].includes(args[0]) ? 2 : 1);
}
const [command, ...rest] = args;
if (command === 'summary') {
  fs.writeFileSync(rest[rest.indexOf('--file') + 1], JSON.stringify(state));
} else if (command === 'burn-key') {
  const [block, path, purpose] = rest;
  const index = Number(block.at(-1));
  const digest = purpose.startsWith('SECURE_BOOT_DIGEST');
  // Keys must arrive through an inherited descriptor, never a regular file.
  if (!digest && fs.statSync(path).isFile()) process.exit(41);
  const data = fs.readFileSync(path);
  if (!digest) {
    if (data.length !== 32) process.exit(42);
    fs.appendFileSync(process.env.KEYS, data.toString('hex') + '\n');
    put('RD_DIS', raw('RD_DIS') | 1 << index);
  }
  put(block, digest ? BigInt('0x' + data.toString('hex')) : 0, { readable: digest, writeable: false });
  state['KEY_PURPOSE_' + index].value = purpose;
} else if (command === 'burn-efuse') {
  for (let i = 0; i < rest.length; i += 2) {
    if (rest[i] !== process.env.MOCK_STUCK) put(rest[i], Number(rest[i + 1]));
  }
} else if (command === 'write-protect-efuse') {
  put('WR_DIS', raw('WR_DIS') | 1);
} else {
  process.exit(99);
}
fs.writeFileSync(statePath, JSON.stringify(state));
MOCK
cat > "$tmp/bin/esptool" <<'MOCK'
#!/usr/bin/env bash
echo "esptool $* (cwd $(pwd -P))" >> "$CALLS"
MOCK
cat > "$tmp/bin/espsecure" <<'MOCK'
#!/usr/bin/env bash
case "$1" in
  verify-signature) [[ -z "${MOCK_BADSIG:-}" || "$*" != *"$MOCK_BADSIG"* ]] ;;
  digest-sbv2-public-key) shasum -a 256 "$3" | cut -c1-64 | xxd -r -p > "${@: -1}" ;;
  *) exit 99 ;;
esac
MOCK
chmod +x "$tmp/bin/"*
export PATH="$tmp/bin:$PATH" CALLS="$tmp/calls" EFUSE_STATE="$tmp/state.json" KEYS="$tmp/keys"

blank_state() {
  node - "$EFUSE_STATE" "${1:-3}" "${2:-}" <<'JS'
const fs = require('node:fs');
const [path, major, preset] = process.argv.slice(2);
const names = ['WR_DIS', 'RD_DIS', 'SPI_BOOT_CRYPT_CNT', 'SECURE_BOOT_EN', 'DIS_DOWNLOAD_MANUAL_ENCRYPT',
  'ENABLE_SECURITY_DOWNLOAD', 'DIS_DOWNLOAD_MODE', 'SECURE_BOOT_AGGRESSIVE_REVOKE', 'DIS_DIRECT_BOOT',
  'DIS_PAD_JTAG', 'DIS_USB_JTAG', 'SOFT_DIS_JTAG', 'SPI_DOWNLOAD_MSPI_DIS',
  'SECURE_BOOT_KEY_REVOKE0', 'SECURE_BOOT_KEY_REVOKE1', 'SECURE_BOOT_KEY_REVOKE2'];
const state = {};
for (const n of names) state[n] = { raw_value: '0x0', value: 0, readable: true, writeable: true };
state.WAFER_VERSION_MAJOR = { raw_value: '0x' + Number(major).toString(16), value: Number(major), readable: true, writeable: true };
for (let i = 0; i < 6; i++) {
  state['BLOCK_KEY' + i] = { raw_value: '0x0', value: '', readable: true, writeable: true };
  state['KEY_PURPOSE_' + i] = { raw_value: '0x0', value: 'USER', readable: true, writeable: true };
}
if (preset) state[preset].raw_value = '0x1';
fs.writeFileSync(path, JSON.stringify(state));
JS
}

# Test-only keys generated here; none is a real Tempo key.
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "$tmp/sign.pem" 2>/dev/null
openssl pkey -in "$tmp/sign.pem" -pubout -out "$tmp/sign-pub.pem"
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$tmp/id.pem" 2>/dev/null
openssl pkey -in "$tmp/id.pem" -pubout -out "$tmp/id-pub.pem"
openssl pkey -in "$tmp/id.pem" -pubout -out "$tmp/ec-sign-pub.pem"
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 2>/dev/null | openssl pkey -pubout > "$tmp/backup-pub.pem"
b=$tmp/build sg=$tmp/signed
mkdir -p "$sg"
write_build_inner() {
  node - "$b" "$sg" "$tmp/sign-pub.pem" "$@" <<'JS'
const fs = require('node:fs');
const { createHash } = require('node:crypto');
const [build, signed, publicKey, ...changes] = process.argv.slice(2);
// Factory-relevant lines of a #57 production sdkconfig (the gate itself is stubbed or real).
const config = { CONFIG_IDF_TARGET: '"esp32p4"', CONFIG_SECURE_FLASH_ENCRYPTION_KEY_SOURCE_EFUSES: 'y',
  CONFIG_SECURE_FLASH_ENCRYPTION_AES128: 'y', CONFIG_PARTITION_TABLE_OFFSET: '0x10000',
  CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID: '5', CONFIG_SECURE_INSECURE_ALLOW_DL_MODE: 'y' };
// Layout ab-8mb-secure-v1: a 0xD000-byte signed bootloader fits below the table at 0x10000.
const images = { 'bootloader.bin': [0x2000, 0xD000], 'partition-table.bin': [0x10000, 0xC00],
  'ota_data_initial.bin': [0x17000, 0x2000], 'weather_station.bin': [0x20000, 0x10000] };
let tamper = false, extra = false, key = publicKey, omit = '';
for (const change of changes) {
  const [name, value] = change.split('=');
  if (name === 'BOOTLOADER_SIZE') images['bootloader.bin'][1] = parseInt(value, 16);
  else if (name === 'TABLE_AT') images['partition-table.bin'][0] = parseInt(value, 16);
  else if (name === 'TAMPER') tamper = true;
  else if (name === 'EXTRA') extra = true;
  else if (name === 'RELEASE_KEY') key = value;
  else if (name === 'OMIT_SUM') omit = value;
  else if (value === '') delete config[name];
  else config[name] = value;
}
const extraLines = process.env.EXTRA_CONFIG ? process.env.EXTRA_CONFIG + '\n' : '';
fs.writeFileSync(build + '/sdkconfig', (process.env.BASE_CONFIG ?? '') + Object.entries(config).map(([k, v]) => `${k}=${v}\n`).join('') + extraLines);
for (const [name, [, size]] of Object.entries(images)) fs.writeFileSync(signed + '/' + name, Buffer.alloc(size));
fs.copyFileSync(key, signed + '/secure_boot_public_key.pem');
fs.writeFileSync(signed + '/flash_args', '--flash-mode dio --flash-freq 80m --flash-size 32MB\n'
  + Object.entries(images).map(([name, [offset]]) => '0x' + offset.toString(16) + ' ' + name + '\n').join('')
  + (extra ? '0x30000 storage.bin\n' : ''));
const sums = [...Object.keys(images), 'secure_boot_public_key.pem', 'flash_args'].filter(n => n !== omit)
  .map(n => createHash('sha256').update(fs.readFileSync(signed + '/' + n)).digest('hex') + '  ' + n + '\n');
fs.writeFileSync(signed + '/SHA256SUMS', sums.join(''));
if (tamper) fs.appendFileSync(signed + '/weather_station.bin', 'x');
JS
}
write_build() { write_build_inner "$@"; }
manifest() { printf '{"verified": %s, "usb_mode": "%s"}\n' "${1:-true}" "${2:-USB-Serial/JTAG}" > "$tmp/unit/manifest.json"; }
common=(--build-dir "$b" --signed-dir "$sg" --signing-public-key "$tmp/sign-pub.pem" --backup-signing-public-key "$tmp/backup-pub.pem")
device=(--port /dev/cu.mock --record-dir "$tmp/unit" --execute)
fp() { shasum -a 256 "$1" | cut -c1-16; }
answers() { printf '%s\n' "I HAVE VERIFIED OFFLINE BACKUPS OF $(fp "$tmp/sign-pub.pem") AND $(fp "$tmp/backup-pub.pem")" "$@"; }
all_answers=('ERASE FLASH' 'BURN BLOCK_KEY0 XTS_AES_128_KEY' 'BURN BLOCK_KEY5 HMAC_UP' 'BURN BLOCK_KEY2 SECURE_BOOT_DIGEST0'
  'BURN BLOCK_KEY3 SECURE_BOOT_DIGEST1' 'BURN SPI_BOOT_CRYPT_CNT 1' 'BURN HARDENING' 'BURN REVOKE UNUSED'
  'BURN SECURE_BOOT_EN' 'PROTECT RD_DIS' yes 'FLASH SIGNED')
device_calls() { grep -c -E '^(espefuse|esptool)' "$CALLS" || true; }

# 1. The default is a dry run that never runs a device tool.
write_build; blank_state; : > "$CALLS"
out=$(node "$run" "${common[@]}" --record-dir "$tmp/plan")
[[ $(device_calls) == 0 ]] || fail 'dry run touched a device tool'
grep -q 'DRY RUN' <<<"$out" && [[ $(grep -c IRREVERSIBLE <<<"$out") == 9 ]] || fail 'dry run plan incomplete'
grep -q 'burn-key BLOCK_KEY5 .<KEY-PIPE>. HMAC_UP' <<<"$out" || fail 'HMAC step missing from plan'
if grep -q ENABLE_SECURITY_DOWNLOAD <<<"$out"; then fail 'secure download step without config'; fi
write_build CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE=y
node "$run" "${common[@]}" --record-dir "$tmp/plan" | grep -q ENABLE_SECURITY_DOWNLOAD || fail 'secure download step missing'

# 2. Host-side refusals happen before any device command, even with --execute.
refuse() { # refuse NAME SETUP...
  local name=$1; shift
  write_build; manifest; blank_state; : > "$CALLS"
  if (run_prefix=(); eval "$*" && answers "${all_answers[@]}" | node ${run_prefix[@]+"${run_prefix[@]}"} "$run" "${args[@]}") >/dev/null 2>&1; then fail "accepted: $name"; fi
  [[ $(device_calls) == 0 ]] || fail "device touched despite: $name"
}
base='args=("${common[@]}" "${device[@]}")'
refuse private-key "$base; args[5]=$tmp/sign.pem"
refuse ecdsa-signing-key "$base; args[5]=$tmp/ec-sign-pub.pem"
refuse gate-rejects "$base; export MOCK_GATE=reject"
refuse no-gate "$base; rm $tmp/repo/scripts/secure-build.mjs"; cp "$tmp/stub-gate.mjs" "$tmp/repo/scripts/secure-build.mjs"
refuse no-sdkconfig "$base; rm $b/sdkconfig"
refuse rom-download-disabled "$base; write_build CONFIG_SECURE_DISABLE_ROM_DL_MODE=y"
refuse allow-rd-dis "$base; write_build CONFIG_SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS=y"
refuse key-manager "$base; write_build CONFIG_SECURE_FLASH_ENCRYPTION_KEY_SOURCE_EFUSES="
refuse aes256 "$base; write_build CONFIG_SECURE_FLASH_ENCRYPTION_AES128="
refuse wrong-hmac-slot "$base; write_build CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=2"
refuse wrong-target "$base; write_build 'CONFIG_IDF_TARGET=\"esp32s3\"'"
refuse table-offset-config "$base; write_build CONFIG_PARTITION_TABLE_OFFSET=0x8000"
refuse bootloader-overlap "$base; write_build BOOTLOADER_SIZE=0xF000"
refuse old-table-offset "$base; write_build TABLE_AT=0x8000"
refuse tampered-signed-image "$base; write_build TAMPER=1"
refuse unsigned-extra-image "$base; write_build EXTRA=1"
refuse unhashed-flash-args "$base; write_build OMIT_SUM=flash_args"
refuse no-signed-set "$base; rm $sg/SHA256SUMS"
refuse other-release-key "$base; write_build RELEASE_KEY=$tmp/backup-pub.pem"
refuse no-backup-key "$base; args=(\"\${args[@]:0:6}\" \"\${args[@]:8}\")"
refuse same-backup-key "$base; args[7]=$tmp/sign-pub.pem"
refuse ecdsa-backup-key "$base; args[7]=$tmp/ec-sign-pub.pem"
refuse bad-signature "$base; export MOCK_BADSIG=weather_station.bin"
refuse no-backup "$base; rm $tmp/unit/manifest.json"
refuse unverified-backup "$base; manifest false"
refuse usb-otg "$base; manifest true USB-OTG"
refuse unreviewed-chip "$base; args+=(--chip esp32s3)"
refuse no-port 'args=("${common[@]}" --record-dir "$tmp/unit" --execute)'
# Keys reach espefuse through /dev/fd/3 (macOS fdesc semantics): any other OS is refused.
not_mac='data:text/javascript,Object.defineProperty(process,"platform",{value:"linux"})'
refuse not-macos "$base; run_prefix=(--import '$not_mac')"
refuse not-macos-identity "args=(--extract-identity $tmp/none.log --out $tmp/x); run_prefix=(--import '$not_mac')"

# 3. A wrong signing-backup phrase stops before any device command.
write_build; manifest; blank_state; : > "$CALLS"
if printf 'yes\n' | node "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail 'ran without backup phrase'; fi
[[ $(device_calls) == 0 ]] || fail 'device touched without backup phrase'

# 4. Chips that are already partly secured, or too old, are refused before burning.
for preset in DIS_DOWNLOAD_MANUAL_ENCRYPT ENABLE_SECURITY_DOWNLOAD SECURE_BOOT_KEY_REVOKE0 SECURE_BOOT_KEY_REVOKE1; do
  blank_state 3 "$preset"; : > "$CALLS"
  if answers "${all_answers[@]}" | node "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail "accepted preset $preset"; fi
  if grep -q -E 'burn|erase' "$CALLS"; then fail "burned despite $preset"; fi
done
blank_state 1; : > "$CALLS"
if answers "${all_answers[@]}" | node "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail 'accepted rev 1 chip'; fi

# 5. Full confirmed run: Espressif order, keys via pipe only, verify before flash.
blank_state; : > "$CALLS"; : > "$KEYS"
out=$(answers "${all_answers[@]}" | node "$run" "${common[@]}" "${device[@]}" 2>&1) || fail "full run failed: $out"
order=$(grep -o -E 'erase-flash|burn-key BLOCK_KEY[0-9]|SPI_BOOT_CRYPT_CNT 1|DIS_DIRECT_BOOT|REVOKE[0-9]|SECURE_BOOT_EN|write-protect-efuse RD_DIS|write-flash' "$CALLS" | tr '\n' ,)
[[ "$order" == 'erase-flash,burn-key BLOCK_KEY0,burn-key BLOCK_KEY5,burn-key BLOCK_KEY2,burn-key BLOCK_KEY3,SPI_BOOT_CRYPT_CNT 1,DIS_DIRECT_BOOT,REVOKE2,SECURE_BOOT_EN,write-protect-efuse RD_DIS,write-flash,' ]] || fail "wrong order: $order"
signed_real=$(cd "$sg" && pwd -P)
grep -q "write-flash --flash-mode dio --flash-freq 80m --flash-size 32MB --encrypt 0x2000 $signed_real/bootloader.bin 0x10000 $signed_real/partition-table.bin 0x17000 $signed_real/ota_data_initial.bin 0x20000 $signed_real/weather_station.bin" "$CALLS" || fail 'signed images not flashed at the secure layout offsets'
if grep 'write-flash' "$CALLS" | grep -q "$b"; then fail 'flashed an unsigned build image'; fi
[[ $(sort -u "$KEYS" | grep -c .) == 2 && $(grep -c . "$KEYS") == 2 ]] || fail "expected two distinct piped keys: $(cat $KEYS)"
while read -r key; do
  if grep -r -q "$key" "$tmp/unit" "$CALLS" <<<"$out"; then fail 'key material leaked to disk, log or output'; fi
  if grep -q "$key" <<<"$out"; then fail 'key printed'; fi
done < "$KEYS"
grep -q 'eFuses match the plan' <<<"$out" || fail 'verification not reported'
grep -q "^gate check-config $b/sdkconfig$" "$CALLS" || fail 'production config gate not run on the build sdkconfig'

# 6. Re-running a finished unit skips every burn.
: > "$CALLS"
answers yes 'FLASH SIGNED' | node "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1 || fail 'rerun failed'
if grep -q -E 'burn|erase|write-protect' "$CALLS"; then fail 'rerun burned again'; fi

# 7. Stopping mid-way burns nothing further; resuming continues where it stopped.
blank_state; : > "$CALLS"
if answers 'ERASE FLASH' 'BURN BLOCK_KEY0 XTS_AES_128_KEY' 'no' | node "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail 'continued after no'; fi
[[ $(grep -c burn "$CALLS") == 1 ]] || fail 'burned after a refused confirmation'
: > "$CALLS"
answers "${all_answers[@]:2}" | node "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1 || fail 'resume failed'
if grep -q -E 'erase-flash|BLOCK_KEY0' "$CALLS"; then fail 'resume repeated finished steps'; fi

# 8. A burn that did not stick fails verification and the firmware is not flashed.
blank_state; : > "$CALLS"
if answers "${all_answers[@]}" | MOCK_STUCK=SECURE_BOOT_EN node "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail 'verification passed with SECURE_BOOT_EN unset'; fi
if grep -q write-flash "$CALLS"; then fail 'flashed after failed verification'; fi

# 9. Rehearsal on virtual eFuses never runs esptool.
blank_state; : > "$CALLS"
answers "${all_answers[@]:1:10}" | node "$run" "${common[@]}" --virtual-efuse "$tmp/virt.bin" --record-dir "$tmp/virt" --execute >/dev/null 2>&1 || fail 'virtual rehearsal failed'
if grep -q esptool "$CALLS"; then fail 'virtual rehearsal ran esptool'; fi
grep -q -- '--virt --path-efuse-file' "$CALLS" || fail 'virtual rehearsal used a port'

# 10. The real production gate from #57 accepts its own profile and refuses an extra
# insecure option. Uses the tree copy, or #57's branch until it is merged.
real_gate="$tmp/real-gate.mjs"
if [[ -f "$root/scripts/secure-build.mjs" ]]; then cp "$root/scripts/secure-build.mjs" "$real_gate"
elif git -C "$root" show origin/feat/release-signing:scripts/secure-build.mjs > "$real_gate" 2>/dev/null; then :
else rm -f "$real_gate"; fi
if [[ -f "$real_gate" ]]; then
  cp "$real_gate" "$tmp/repo/scripts/secure-build.mjs"
  # Path via the environment: as argv[1] the module would run its own CLI.
  base_config=$(GATE="$real_gate" node --input-type=module -e "
    const sb = await import(process.env.GATE);
    const lines = Object.entries(sb.REQUIRED).map(([k, v]) => k + '=' + v);
    lines.push('CONFIG_TEMPO_ENROLLMENT_URL=\"https://api.example.test/v1/device\"',
      'CONFIG_BOOTLOADER_APP_SECURE_VERSION=1', 'CONFIG_BOOTLOADER_APP_SEC_VER_SIZE_EFUSE_FIELD=16');
    console.log(lines.join('\n'));")
  BASE_CONFIG="$base_config"$'\n' write_build_inner; blank_state; : > "$CALLS"
  node "$run" "${common[@]}" --record-dir "$tmp/plan" >/dev/null 2>"$tmp/err" || fail "real #57 gate refused its own profile: $(cat "$tmp/err")"
  BASE_CONFIG="$base_config"$'\n' EXTRA_CONFIG=CONFIG_SECURE_BOOT_ALLOW_JTAG=y write_build_inner
  if node "$run" "${common[@]}" --record-dir "$tmp/plan" >/dev/null 2>"$tmp/err"; then fail 'real #57 gate accepted ALLOW_JTAG'; fi
  grep -q 'check-config rejected' "$tmp/err" || fail "insecure option not refused by the gate: $(cat "$tmp/err")"
  echo 'Real #57 production gate: own profile accepted, extra insecure option refused.'
else
  echo 'NOTE: scripts/secure-build.mjs (#57) not available; real-gate check runs once #57 is merged.'
fi

# 11. Identity extraction from a boot log.
id=$(openssl pkey -pubin -in "$tmp/id-pub.pem" -outform DER | shasum -a 256 | cut -c1-32)
log() { { printf 'I (812) boot: ESP-IDF\n\033[0;32mI (1200) identity: Manufacturing PUBLIC identity for %s:\r\n' "$1"; sed 's/$/\r/' "$tmp/id-pub.pem"; printf '\033[0m\n'; } ; }
log "$id" > "$tmp/boot.log"; log "$id" >> "$tmp/boot.log"
out=$(node "$run" --extract-identity "$tmp/boot.log" --out "$tmp/identity")
cmp -s "$tmp/identity/identity-public.pem" "$tmp/id-pub.pem" || fail 'extracted key differs'
grep -q "Hostname:  d-$id.tempo.tinyrobotcompany.io" <<<"$out" || fail 'hostname not printed'
[[ $(tail -1 <<<"$out" | base64 -d) == "$(cat "$tmp/id-pub.pem")" ]] || fail 'base64 workflow input wrong'
log 00000000000000000000000000000000 > "$tmp/bad.log"
if node "$run" --extract-identity "$tmp/bad.log" --out "$tmp/x" >/dev/null 2>&1; then fail 'accepted mismatched device ID'; fi
{ log "$id"; echo 'PRIVATE KEY'; } > "$tmp/bad.log"
if node "$run" --extract-identity "$tmp/bad.log" --out "$tmp/x" >/dev/null 2>&1; then fail 'accepted log mentioning PRIVATE'; fi
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 2>/dev/null | openssl pkey -pubout > "$tmp/other.pem"
other=$(openssl pkey -pubin -in "$tmp/other.pem" -outform DER | shasum -a 256 | cut -c1-32)
{ log "$id"; printf 'Manufacturing PUBLIC identity for %s:\n' "$other"; cat "$tmp/other.pem"; } > "$tmp/bad.log"
if node "$run" --extract-identity "$tmp/bad.log" --out "$tmp/x" >/dev/null 2>&1; then fail 'accepted two identities'; fi
echo 'PASS: factory lockdown dry run, refusals, order, piped keys, resume, verification and identity extraction'
