#!/usr/bin/env bash
# Back up, or restore, a Tempo board's whole flash before factory lockdown.
# Dry run by default: prints the commands and touches nothing. With --execute,
# each device step needs a typed confirmation. Backups can hold Wi-Fi and API
# credentials, so they live in the git-ignored build-host/ with owner-only access.
#
#   bash scripts/factory-backup.sh --port PORT --unit NAME [--execute]
#   bash scripts/factory-backup.sh --restore DIR --port PORT [--execute]
#
# Restore works only while flash encryption and Secure Boot are still off.
# See docs/FACTORY_RUNBOOK.md.
set -euo pipefail
cd "$(dirname "$0")/.."
chip=esp32p4 port='' unit='' restore='' execute=0 size=0x2000000 size_name=32MB
while (($#)); do
  case "$1" in
    --chip) chip=${2:?}; shift ;;
    --port) port=${2:?}; shift ;;
    --unit) unit=${2:?}; shift ;;
    --restore) restore=${2:?}; shift ;;
    --execute) execute=1 ;;
    *) echo "Unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
[[ "$chip" == esp32p4 ]] || { echo "Only the ESP32-P4 (32 MB) profile is reviewed; got $chip." >&2; exit 2; }
[[ -n "$port" ]] || { echo 'Give --port, for example --port /dev/cu.usbmodem1101' >&2; exit 2; }

step() { # step PHRASE DESCRIPTION -- COMMAND...
  local phrase=$1 what=$2; shift 3
  printf '\n%s\n  Command: %s\n' "$what" "$*"
  ((execute)) || return 0
  read -r -p "  Type \"$phrase\" to run this step, anything else stops: " answer || answer=''
  [[ "$answer" == "$phrase" ]] || { echo "Stopped. This step was not run." >&2; exit 3; }
  "$@"
}
sha() { shasum -a 256 "$1" | cut -d' ' -f1; }

if [[ -n "$restore" ]]; then
  manifest="$restore/manifest.json"
  [[ -f "$manifest" && -f "$restore/flash.bin" ]] || { echo "No backup in $restore" >&2; exit 2; }
  want=$(node -e 'const m = JSON.parse(require("fs").readFileSync(process.argv[1], "utf8")); console.log(m.verified === true ? m.sha256 : "")' "$manifest")
  [[ -n "$want" && "$(sha "$restore/flash.bin")" == "$want" ]] || { echo 'Backup is unverified or changed since it was taken. Not restoring.' >&2; exit 2; }
  ((execute)) || echo 'DRY RUN: nothing will be sent to the board.'
  check="$restore/efuse-before-restore.json"
  step yes 'Read the eFuses to check this board can still take a plaintext image' -- \
    espefuse --chip "$chip" --port "$port" summary --format json --file "$check"
  if ((execute)); then
    node - "$check" <<'JS' || { echo 'Flash encryption or Secure Boot is on: this plaintext backup can no longer boot. See "Failed-unit recovery".' >&2; exit 4; }
const s = JSON.parse(require('node:fs').readFileSync(process.argv[2], 'utf8'));
const on = BigInt(s.SPI_BOOT_CRYPT_CNT.raw_value) || BigInt(s.SECURE_BOOT_EN.raw_value);
process.exit(on ? 1 : 0);
JS
  fi
  step 'RESTORE FLASH' 'Write the whole backup back to the board (replaces everything on it)' -- \
    esptool --chip "$chip" --port "$port" write-flash 0x0 "$restore/flash.bin"
  step yes 'Check the board now holds exactly the backup' -- \
    esptool --chip "$chip" --port "$port" verify-flash 0x0 "$restore/flash.bin"
  ((execute)) && echo 'Restore complete.'
  exit 0
fi

[[ "$unit" =~ ^[A-Za-z0-9._-]{1,48}$ ]] || { echo 'Give --unit NAME (letters, digits, . _ -), e.g. --unit p4-proto-1' >&2; exit 2; }
dir="build-host/factory-units/$unit-$(date -u +%Y%m%dT%H%M%SZ)"
((execute)) || echo "DRY RUN: nothing will be sent to the board. Backup folder would be $dir"
if ((execute)); then
  umask 077
  mkdir -p "$dir"
fi
step yes 'Identify the chip, its USB connection and flash size (read-only)' -- \
  bash -c 'esptool --chip "$1" --port "$2" flash-id | tee "$3"' _ "$chip" "$port" "$dir/flash-id.txt"
step yes 'Save the current eFuse state (read-only)' -- \
  espefuse --chip "$chip" --port "$port" summary --format json --file "$dir/efuse-before.json"
step yes "Read the whole $size_name flash to a file (read-only, takes several minutes)" -- \
  esptool --chip "$chip" --port "$port" read-flash 0x0 "$size" "$dir/flash.bin"
step yes 'Compare the file against the board to prove the backup is complete (read-only)' -- \
  esptool --chip "$chip" --port "$port" verify-flash 0x0 "$dir/flash.bin"
((execute)) || exit 0
node - "$dir" "$unit" "$chip" "$port" "$size" "$size_name" "$(sha "$dir/flash.bin")" <<'JS'
const fs = require('node:fs');
const path = require('node:path');
const [folder, unit, chip, port, size, sizeName, digest] = process.argv.slice(2);
const info = fs.readFileSync(path.join(folder, 'flash-id.txt'), 'utf8');
const field = pattern => { const found = info.match(pattern); return found ? found[1].trim() : null; };
const problems = [];
if (fs.statSync(path.join(folder, 'flash.bin')).size !== parseInt(size, 16)) problems.push('backup file has the wrong size');
if (field(/^Detected flash size:\s*(\S+)/m) !== sizeName) problems.push('flash is not ' + sizeName);
if (!(field(/^Chip type:\s*(.+)$/m) ?? '').includes('ESP32-P4')) problems.push('chip is not an ESP32-P4');
const manifest = { unit, chip, port, created: new Date().toISOString().replace(/\.\d+Z$/, 'Z'),
  chip_description: field(/^Chip type:\s*(.+)$/m), mac: field(/^MAC:\s*(\S+)/m),
  usb_mode: field(/^USB mode:\s*(.+)$/m) ?? 'UART', flash_size: sizeName, sha256: digest, verified: problems.length === 0 };
fs.writeFileSync(path.join(folder, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
if (problems.length) { console.error('Backup NOT verified: ' + problems.join('; ')); process.exit(1); }
console.log(`Backup verified: ${folder} (sha256 ${digest.slice(0, 16)}..., USB mode ${manifest.usb_mode}).`);
JS
