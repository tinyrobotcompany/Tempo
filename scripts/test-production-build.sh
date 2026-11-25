#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Activate the pinned ESP-IDF checkout}"
# Disposable test identities only. No production secret, network enrollment or flash.
trial_dir=$(mktemp -d)
trap 'rm -rf "$trial_dir"' EXIT
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "$trial_dir/test.pem" >/dev/null 2>&1
export SECURE_BOOT_PUBLIC_KEY_SHA256
SECURE_BOOT_PUBLIC_KEY_SHA256=$(openssl pkey -in "$trial_dir/test.pem" -pubout -outform DER 2>/dev/null | openssl dgst -sha256 | awk '{print $NF}')
export TEMPO_ENROLLMENT_URL=https://enrollment.tempo.tinyrobotcompany.io/v1/device
export TEMPO_IDENTITY_HMAC_KEY_ID=2
export TEMPO_SECURE_VERSION=0
config="$PWD/build-host/test-production.sdkconfig"
image="$PWD/build-production-test/weather_station.bin"
python3 scripts/production-release.py configure --config "$config" --key "$trial_dir/test.pem"
idf.py -C firmware -B build-production-test -D SDKCONFIG="$config" build
python3 scripts/production-release.py verify --config "$config" --key "$trial_dir/test.pem" --image "$image"
python3 - "$image" "$trial_dir/tampered.bin" <<'PY'
from pathlib import Path
import sys
blob=bytearray(Path(sys.argv[1]).read_bytes());blob[4096]^=1
Path(sys.argv[2]).write_bytes(blob)
PY
if python3 scripts/production-release.py verify --config "$config" --key "$trial_dir/test.pem" --image "$trial_dir/tampered.bin"; then
  echo 'FAIL: tampered production image accepted' >&2; exit 1
fi
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "$trial_dir/wrong.pem" >/dev/null 2>&1
SECURE_BOOT_PUBLIC_KEY_SHA256=$(openssl pkey -in "$trial_dir/wrong.pem" -pubout -outform DER 2>/dev/null | openssl dgst -sha256 | awk '{print $NF}')
if python3 scripts/production-release.py verify --config "$config" --key "$trial_dir/wrong.pem" --image "$image"; then
  echo 'FAIL: production image accepted under wrong Secure Boot key' >&2; exit 1
fi
echo 'PASS: actual protected SDK build; valid RSA image accepted, tampered and wrong-key images rejected'
