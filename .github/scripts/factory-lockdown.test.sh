#!/usr/bin/env bash
# Factory lockdown: dry-run safety, host refusals, confirmed step order, key
# handling through a pipe, resumability, verification and identity extraction.
# Uses a stateful espefuse mock; no device or real eFuse is involved.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin" "$tmp/build/config" "$tmp/build/bootloader" "$tmp/build/partition_table" "$tmp/unit"
run="$root/scripts/factory-lockdown.py"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat > "$tmp/bin/espefuse" <<'MOCK'
#!/usr/bin/env python3
import json, os, stat, sys
args = sys.argv[1:]
with open(os.environ['CALLS'], 'a') as calls:
    calls.write('espefuse ' + ' '.join(args) + '\n')
state_path = os.environ['EFUSE_STATE']
state = json.load(open(state_path))
def put(name, value, **extra):
    state[name].update(raw_value=hex(value), **extra)
# Skip the global options: --chip X, --port P / --virt --path-efuse-file F / --do-not-confirm.
while args and args[0].startswith('--'):
    args = args[1 + (args[0] in ('--chip', '--port', '--path-efuse-file')):]
command, rest = args[0], args[1:]
if command == 'summary':
    json.dump(state, open(rest[rest.index('--file') + 1], 'w'))
elif command == 'burn-key':
    block, path, purpose = rest
    index = int(block[-1])
    data = open(path, 'rb').read()
    if purpose != 'SECURE_BOOT_DIGEST0':
        # Keys must arrive through a pipe, never a regular file.
        if not stat.S_ISFIFO(os.stat(path).st_mode) or len(data) != 32:
            sys.exit(41)
        with open(os.environ['KEYS'], 'a') as keys:
            keys.write(data.hex() + '\n')
        put('RD_DIS', int(state['RD_DIS']['raw_value'], 16) | 1 << index)
    put(block, int.from_bytes(data, 'big') if purpose == 'SECURE_BOOT_DIGEST0' else 0,
        readable=purpose == 'SECURE_BOOT_DIGEST0', writeable=False)
    state['KEY_PURPOSE_%d' % index]['value'] = purpose
elif command == 'burn-efuse':
    for name, value in zip(rest[::2], rest[1::2]):
        if name == os.environ.get('MOCK_STUCK'):
            continue
        put(name, int(value, 0))
elif command == 'write-protect-efuse':
    put('WR_DIS', int(state['WR_DIS']['raw_value'], 16) | 1)
else:
    sys.exit(99)
json.dump(state, open(state_path, 'w'))
MOCK
cat > "$tmp/bin/esptool" <<'MOCK'
#!/usr/bin/env bash
echo "esptool $* (cwd $(pwd -P))" >> "$CALLS"
MOCK
cat > "$tmp/bin/espsecure" <<'MOCK'
#!/usr/bin/env bash
case "$1" in
  verify-signature) [[ -z "${MOCK_BADSIG:-}" || "$*" != *"$MOCK_BADSIG"* ]] ;;
  digest-sbv2-public-key) printf '\x5a%.0s' $(seq 32) > "${@: -1}" ;;
  *) exit 99 ;;
esac
MOCK
chmod +x "$tmp/bin/"*
export PATH="$tmp/bin:$PATH" CALLS="$tmp/calls" EFUSE_STATE="$tmp/state.json" KEYS="$tmp/keys"

blank_state() {
  python3 - "$EFUSE_STATE" "${1:-3}" "${2:-}" <<'PY'
import json, sys
path, major, preset = sys.argv[1], int(sys.argv[2]), sys.argv[3]
names = ['WR_DIS', 'RD_DIS', 'SPI_BOOT_CRYPT_CNT', 'SECURE_BOOT_EN', 'DIS_DOWNLOAD_MANUAL_ENCRYPT',
         'ENABLE_SECURITY_DOWNLOAD', 'DIS_DOWNLOAD_MODE', 'SECURE_BOOT_AGGRESSIVE_REVOKE', 'DIS_DIRECT_BOOT',
         'DIS_PAD_JTAG', 'DIS_USB_JTAG', 'SOFT_DIS_JTAG', 'SPI_DOWNLOAD_MSPI_DIS',
         *['SECURE_BOOT_KEY_REVOKE%d' % i for i in range(3)]]
state = {n: dict(raw_value='0x0', value=0, readable=True, writeable=True) for n in names}
state['WAFER_VERSION_MAJOR'] = dict(raw_value=hex(major), value=major, readable=True, writeable=True)
for i in range(6):
    state['BLOCK_KEY%d' % i] = dict(raw_value='0x0', value='', readable=True, writeable=True)
    state['KEY_PURPOSE_%d' % i] = dict(raw_value='0x0', value='USER', readable=True, writeable=True)
if preset:
    state[preset]['raw_value'] = '0x1'
json.dump(state, open(path, 'w'))
PY
}

# Test-only keys generated here; none is a real Tempo key.
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "$tmp/sign.pem" 2>/dev/null
openssl pkey -in "$tmp/sign.pem" -pubout -out "$tmp/sign-pub.pem"
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$tmp/id.pem" 2>/dev/null
openssl pkey -in "$tmp/id.pem" -pubout -out "$tmp/id-pub.pem"
openssl pkey -in "$tmp/id.pem" -pubout -out "$tmp/ec-sign-pub.pem"
b=$tmp/build
head -c 24576 /dev/zero > "$b/bootloader/bootloader.bin"
head -c 3072 /dev/zero > "$b/partition_table/partition-table.bin"
head -c 65536 /dev/zero > "$b/tempo.bin"
echo '--encrypt-files 0x2000 bootloader/bootloader.bin' > "$b/encrypted_flash_args"
write_build() {
  python3 - "$b" "$@" <<'PY'
import json, sys
build, changes = sys.argv[1], sys.argv[2:]
config = dict(IDF_TARGET='esp32p4', SECURE_BOOT=True, SECURE_BOOT_V2_ENABLED=True, SECURE_BOOT_V2_RSA_ENABLED=True,
              SECURE_FLASH_ENC_ENABLED=True, SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=True,
              SECURE_FLASH_ENCRYPTION_AES128=True, TEMPO_PRODUCTION_IDENTITY=True, TEMPO_IDENTITY_HMAC_KEY_ID=5,
              SECURE_FLASH_REQUIRE_ALREADY_ENABLED=True, SECURE_BOOT_REQUIRE_ALREADY_ENABLED=True,
              SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC=True)
files = {'0x2000': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin', '0x20000': 'tempo.bin'}
for change in changes:
    key, value = change.split('=')
    if key == 'BOOTLOADER_AT':
        files = {value: files.pop('0x2000'), **files}
    else:
        config[key] = json.loads(value)
json.dump(config, open(build + '/config/sdkconfig.json', 'w'))
json.dump({'flash_files': files, 'bootloader': {'file': 'bootloader/bootloader.bin'}, 'app': {'file': 'tempo.bin'}},
          open(build + '/flasher_args.json', 'w'))
PY
}
manifest() { printf '{"verified": %s, "usb_mode": "%s"}\n' "${1:-true}" "${2:-USB-Serial/JTAG}" > "$tmp/unit/manifest.json"; }
common=(--build-dir "$b" --signing-public-key "$tmp/sign-pub.pem")
device=(--port /dev/cu.mock --record-dir "$tmp/unit" --execute)
fingerprint=$(printf '5a%.0s' $(seq 8))
answers() { printf '%s\n' "I HAVE A VERIFIED OFFLINE BACKUP OF $fingerprint" "$@"; }
all_answers=('ERASE FLASH' 'BURN BLOCK_KEY0 XTS_AES_128_KEY' 'BURN BLOCK_KEY5 HMAC_UP' 'BURN BLOCK_KEY2 SECURE_BOOT_DIGEST0'
  'BURN SPI_BOOT_CRYPT_CNT 1' 'BURN HARDENING' 'BURN REVOKE UNUSED' 'BURN SECURE_BOOT_EN' 'PROTECT RD_DIS' yes 'FLASH SIGNED')
device_calls() { grep -c -E '^(espefuse|esptool)' "$CALLS" || true; }

# 1. The default is a dry run that never runs a device tool.
write_build; blank_state; : > "$CALLS"
out=$(python3 "$run" "${common[@]}" --record-dir "$tmp/plan")
[[ $(device_calls) == 0 ]] || fail 'dry run touched a device tool'
grep -q 'DRY RUN' <<<"$out" && [[ $(grep -c IRREVERSIBLE <<<"$out") == 8 ]] || fail 'dry run plan incomplete'
grep -q 'burn-key BLOCK_KEY5 .<KEY-PIPE>. HMAC_UP' <<<"$out" || fail 'HMAC step missing from plan'
if grep -q ENABLE_SECURITY_DOWNLOAD <<<"$out"; then fail 'secure download step without config'; fi
write_build SECURE_ENABLE_SECURE_ROM_DL_MODE=true
python3 "$run" "${common[@]}" --record-dir "$tmp/plan" | grep -q ENABLE_SECURITY_DOWNLOAD || fail 'secure download step missing'

# 2. Host-side refusals happen before any device command, even with --execute.
refuse() { # refuse NAME SETUP...
  local name=$1; shift
  write_build; manifest; blank_state; : > "$CALLS"
  if (eval "$*" && answers "${all_answers[@]}" | python3 "$run" "${args[@]}") >/dev/null 2>&1; then fail "accepted: $name"; fi
  [[ $(device_calls) == 0 ]] || fail "device touched despite: $name"
}
base='args=("${common[@]}" "${device[@]}")'
refuse private-key "$base; args[3]=$tmp/sign.pem"
refuse ecdsa-signing-key "$base; args[3]=$tmp/ec-sign-pub.pem"
refuse release-mode "$base; write_build SECURE_FLASH_ENCRYPTION_MODE_RELEASE=true"
refuse no-dev-mode "$base; write_build SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=false"
refuse rom-download-disabled "$base; write_build SECURE_DISABLE_ROM_DL_MODE=true"
refuse wrong-hmac-slot "$base; write_build TEMPO_IDENTITY_HMAC_KEY_ID=-1"
refuse wrong-target "$base; write_build 'IDF_TARGET=\"esp32s3\"'"
refuse bootloader-overlap "$base; write_build BOOTLOADER_AT=0x3000"
refuse bad-signature "$base; export MOCK_BADSIG=tempo.bin"
refuse no-encrypted-args "$base; mv $b/encrypted_flash_args $b/x"; mv "$b/x" "$b/encrypted_flash_args"
refuse no-backup "$base; rm $tmp/unit/manifest.json"
refuse unverified-backup "$base; manifest false"
refuse usb-otg "$base; manifest true USB-OTG"
refuse unreviewed-chip "$base; args+=(--chip esp32s3)"
refuse no-port 'args=("${common[@]}" --record-dir "$tmp/unit" --execute)'

# 3. A wrong signing-backup phrase stops before any device command.
write_build; manifest; blank_state; : > "$CALLS"
if printf 'yes\n' | python3 "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail 'ran without backup phrase'; fi
[[ $(device_calls) == 0 ]] || fail 'device touched without backup phrase'

# 4. Chips that are already partly secured, or too old, are refused before burning.
for preset in DIS_DOWNLOAD_MANUAL_ENCRYPT ENABLE_SECURITY_DOWNLOAD SECURE_BOOT_KEY_REVOKE0; do
  blank_state 3 "$preset"; : > "$CALLS"
  if answers "${all_answers[@]}" | python3 "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail "accepted preset $preset"; fi
  if grep -q -E 'burn|erase' "$CALLS"; then fail "burned despite $preset"; fi
done
blank_state 1; : > "$CALLS"
if answers "${all_answers[@]}" | python3 "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail 'accepted rev 1 chip'; fi

# 5. Full confirmed run: Espressif order, keys via pipe only, verify before flash.
blank_state; : > "$CALLS"; : > "$KEYS"
out=$(answers "${all_answers[@]}" | python3 "$run" "${common[@]}" "${device[@]}" 2>&1) || fail "full run failed: $out"
order=$(grep -o -E 'erase-flash|burn-key BLOCK_KEY[0-9]|SPI_BOOT_CRYPT_CNT 1|DIS_DIRECT_BOOT|REVOKE1|SECURE_BOOT_EN|write-protect-efuse RD_DIS|write-flash' "$CALLS" | tr '\n' ,)
[[ "$order" == 'erase-flash,burn-key BLOCK_KEY0,burn-key BLOCK_KEY5,burn-key BLOCK_KEY2,SPI_BOOT_CRYPT_CNT 1,DIS_DIRECT_BOOT,REVOKE1,SECURE_BOOT_EN,write-protect-efuse RD_DIS,write-flash,' ]] || fail "wrong order: $order"
grep -q "write-flash @encrypted_flash_args (cwd $(cd "$b" && pwd -P))" "$CALLS" || fail 'flash not run from build dir'
[[ $(sort -u "$KEYS" | grep -c .) == 2 && $(grep -c . "$KEYS") == 2 ]] || fail "expected two distinct piped keys: $(cat $KEYS)"
while read -r key; do
  if grep -r -q "$key" "$tmp/unit" "$CALLS" <<<"$out"; then fail 'key material leaked to disk, log or output'; fi
  if grep -q "$key" <<<"$out"; then fail 'key printed'; fi
done < "$KEYS"
grep -q 'eFuses match the plan' <<<"$out" || fail 'verification not reported'

# 6. Re-running a finished unit skips every burn.
: > "$CALLS"
answers yes 'FLASH SIGNED' | python3 "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1 || fail 'rerun failed'
if grep -q -E 'burn|erase|write-protect' "$CALLS"; then fail 'rerun burned again'; fi

# 7. Stopping mid-way burns nothing further; resuming continues where it stopped.
blank_state; : > "$CALLS"
if answers 'ERASE FLASH' 'BURN BLOCK_KEY0 XTS_AES_128_KEY' 'no' | python3 "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail 'continued after no'; fi
[[ $(grep -c burn "$CALLS") == 1 ]] || fail 'burned after a refused confirmation'
: > "$CALLS"
answers "${all_answers[@]:2}" | python3 "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1 || fail 'resume failed'
if grep -q -E 'erase-flash|BLOCK_KEY0' "$CALLS"; then fail 'resume repeated finished steps'; fi

# 8. A burn that did not stick fails verification and the firmware is not flashed.
blank_state; : > "$CALLS"
if answers "${all_answers[@]}" | MOCK_STUCK=SECURE_BOOT_EN python3 "$run" "${common[@]}" "${device[@]}" >/dev/null 2>&1; then fail 'verification passed with SECURE_BOOT_EN unset'; fi
if grep -q write-flash "$CALLS"; then fail 'flashed after failed verification'; fi

# 9. Rehearsal on virtual eFuses never runs esptool.
blank_state; : > "$CALLS"
answers "${all_answers[@]:1:9}" | python3 "$run" "${common[@]}" --virtual-efuse "$tmp/virt.bin" --record-dir "$tmp/virt" --execute >/dev/null 2>&1 || fail 'virtual rehearsal failed'
if grep -q esptool "$CALLS"; then fail 'virtual rehearsal ran esptool'; fi
grep -q -- '--virt --path-efuse-file' "$CALLS" || fail 'virtual rehearsal used a port'

# 10. Identity extraction from a boot log.
id=$(openssl pkey -pubin -in "$tmp/id-pub.pem" -outform DER | shasum -a 256 | cut -c1-32)
log() { { printf 'I (812) boot: ESP-IDF\n\033[0;32mI (1200) identity: Manufacturing PUBLIC identity for %s:\r\n' "$1"; sed 's/$/\r/' "$tmp/id-pub.pem"; printf '\033[0m\n'; } ; }
log "$id" > "$tmp/boot.log"; log "$id" >> "$tmp/boot.log"
out=$(python3 "$run" --extract-identity "$tmp/boot.log" --out "$tmp/identity")
cmp -s "$tmp/identity/identity-public.pem" "$tmp/id-pub.pem" || fail 'extracted key differs'
grep -q "Hostname:  d-$id.tempo.tinyrobotcompany.io" <<<"$out" || fail 'hostname not printed'
[[ $(tail -1 <<<"$out" | base64 -d) == "$(cat "$tmp/id-pub.pem")" ]] || fail 'base64 workflow input wrong'
log 00000000000000000000000000000000 > "$tmp/bad.log"
if python3 "$run" --extract-identity "$tmp/bad.log" --out "$tmp/x" >/dev/null 2>&1; then fail 'accepted mismatched device ID'; fi
{ log "$id"; echo 'PRIVATE KEY'; } > "$tmp/bad.log"
if python3 "$run" --extract-identity "$tmp/bad.log" --out "$tmp/x" >/dev/null 2>&1; then fail 'accepted log mentioning PRIVATE'; fi
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 2>/dev/null | openssl pkey -pubout > "$tmp/other.pem"
other=$(openssl pkey -pubin -in "$tmp/other.pem" -outform DER | shasum -a 256 | cut -c1-32)
{ log "$id"; printf 'Manufacturing PUBLIC identity for %s:\n' "$other"; cat "$tmp/other.pem"; } > "$tmp/bad.log"
if python3 "$run" --extract-identity "$tmp/bad.log" --out "$tmp/x" >/dev/null 2>&1; then fail 'accepted two identities'; fi
echo 'PASS: factory lockdown dry run, refusals, order, piped keys, resume, verification and identity extraction'
