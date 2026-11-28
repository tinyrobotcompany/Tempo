#!/usr/bin/env bash
# Factory backup/restore: dry run touches nothing, each step needs confirmation,
# the manifest proves a verified 32 MB P4 backup, and restore refuses locked chips.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/repo/scripts" "$tmp/bin"
cp "$root/scripts/factory-backup.sh" "$tmp/repo/scripts/"
cat > "$tmp/bin/esptool" <<'MOCK'
#!/usr/bin/env bash
echo "esptool $*" >> "$CALLS"
case "$*" in
  *flash-id*) printf 'Chip type:          %s\n%sMAC:                30:ed:a0:00:00:01\nDetected flash size: %s\n' \
      "${MOCK_CHIP:-ESP32-P4 (revision v3.1)}" "${MOCK_USB-USB mode:           USB-Serial/JTAG
}" "${MOCK_SIZE:-32MB}" ;;
  *read-flash*) head -c "$((${MOCK_BYTES:-0x2000000}))" /dev/zero > "${@: -1}" ;;
esac
MOCK
cat > "$tmp/bin/espefuse" <<'MOCK'
#!/usr/bin/env bash
echo "espefuse $*" >> "$CALLS"
printf '{"SPI_BOOT_CRYPT_CNT": {"raw_value": "%s"}, "SECURE_BOOT_EN": {"raw_value": "0x0"}}' "${MOCK_CRYPT:-0x0}" > "${@: -1}"
MOCK
chmod +x "$tmp/bin/"*
export PATH="$tmp/bin:$PATH" CALLS="$tmp/calls"
run="$tmp/repo/scripts/factory-backup.sh"
cd "$tmp/repo"
yes4() { printf 'yes\nyes\nyes\nyes\n'; }

: > "$CALLS"
out=$(bash "$run" --port /dev/cu.mock --unit p4-proto-1)
[[ ! -s "$CALLS" ]] || { echo 'Dry run touched the board' >&2; exit 1; }
grep -q 'read-flash 0x0 0x2000000' <<<"$out"
[[ ! -d build-host ]] || { echo 'Dry run created files' >&2; exit 1; }

: > "$CALLS"
yes4 | bash "$run" --port /dev/cu.mock --unit p4-proto-1 --execute >/dev/null
dir=$(echo build-host/factory-units/p4-proto-1-*)
python3 - "$dir/manifest.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
assert m['verified'] is True and m['usb_mode'] == 'USB-Serial/JTAG' and m['flash_size'] == '32MB', m
assert m['mac'] == '30:ed:a0:00:00:01' and len(m['sha256']) == 64, m
PY
[[ $(stat -f %Lp "$dir") == 700 && $(stat -f %Lp "$dir/flash.bin") == 600 ]] || { echo 'Backup is not owner-only' >&2; exit 1; }
grep -q 'verify-flash 0x0' "$CALLS"

# A missing confirmation stops before the step runs.
: > "$CALLS"
if printf 'yes\nno\n' | bash "$run" --port /dev/cu.mock --unit stop >/dev/null 2>&1 --execute; then echo 'Continued after no' >&2; exit 1; fi
[[ $(grep -c . "$CALLS") == 1 ]] || { echo 'Ran a step that was not confirmed' >&2; exit 1; }

# A wrong chip, size, short read or bridge is recorded as unverified.
for scenario in chip size short uart; do
  (
    case "$scenario" in
      chip) export MOCK_CHIP=ESP32-S3 ;;
      size) export MOCK_SIZE=16MB ;;
      short) export MOCK_BYTES=4096 ;;
      uart) export MOCK_USB= ;;
    esac
    if yes4 | bash "$run" --port /dev/cu.mock --unit "$scenario" --execute >/dev/null 2>&1; then
      [[ "$scenario" == uart ]] || { echo "Accepted $scenario" >&2; exit 1; }
      grep -q '"usb_mode": "UART"' build-host/factory-units/uart-*/manifest.json
    else
      grep -q '"verified": false' build-host/factory-units/"$scenario"-*/manifest.json
    fi
  )
done
for bad in '' 'a b' '../x'; do
  if bash "$run" --port /dev/cu.mock --unit "$bad" >/dev/null 2>&1; then echo "Accepted unit name '$bad'" >&2; exit 1; fi
done
if bash "$run" --port /dev/cu.mock --unit x --chip esp32s3 >/dev/null 2>&1; then echo 'Accepted unreviewed chip' >&2; exit 1; fi

# Restore: refuses a changed backup and a chip with flash encryption on.
: > "$CALLS"
printf 'yes\nRESTORE FLASH\nyes\n' | bash "$run" --restore "$dir" --port /dev/cu.mock --execute >/dev/null
grep -q "write-flash 0x0 $dir/flash.bin" "$CALLS"
: > "$CALLS"
if printf 'yes\nRESTORE FLASH\nyes\n' | MOCK_CRYPT=0x1 bash "$run" --restore "$dir" --port /dev/cu.mock --execute >/dev/null 2>&1; then echo 'Restored onto an encrypted chip' >&2; exit 1; fi
if grep -q write-flash "$CALLS"; then echo 'Wrote to an encrypted chip' >&2; exit 1; fi
printf x >> "$dir/flash.bin"; : > "$CALLS"
if printf 'yes\nRESTORE FLASH\nyes\n' | bash "$run" --restore "$dir" --port /dev/cu.mock --execute >/dev/null 2>&1; then echo 'Restored a changed backup' >&2; exit 1; fi
[[ ! -s "$CALLS" ]] || { echo 'Touched the board with a changed backup' >&2; exit 1; }
echo 'PASS: factory backup dry run, confirmations, verified manifest and guarded restore'
