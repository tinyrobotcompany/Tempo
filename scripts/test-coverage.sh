#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 scripts/prepare-tests.py
source build-host/test-env.sh
mkdir -p build-host/coverage/bin
# Remove only previous instrumentation outputs so stale hits cannot pass the gate.
rm -rf build-host/coverage/raw
mkdir -p build-host/coverage/raw
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
export TEMPO_CLANG="$(xcrun --find clang)"
cat > build-host/coverage/bin/cc <<'WRAPPER'
#!/bin/sh
exec "$TEMPO_CLANG" -fprofile-instr-generate -fcoverage-mapping "$@"
WRAPPER
chmod +x build-host/coverage/bin/cc
export PATH="$PWD/build-host/coverage/bin:$PATH"
export LLVM_PROFILE_FILE="$PWD/build-host/coverage/raw/%m-%p.profraw"
export TEMPO_COVERAGE=ON
for suite in test.sh test-settings.sh test-network.sh test-weather.sh test-transport.sh test-phone.sh test-ui.sh test-updates.sh; do
  bash "scripts/$suite"
done
xcrun llvm-profdata merge -sparse build-host/coverage/raw/*.profraw -o build-host/coverage/coverage.profdata
rm -rf build-host/coverage/unit-raw
mkdir -p build-host/coverage/unit-raw
for binary in test_core test_weather test_transport test_transport_json_filter test_phone_protocol; do
  LLVM_PROFILE_FILE="$PWD/build-host/coverage/unit-raw/%m-%p.profraw" "build-host/$binary" tests/fixtures/transport/traffic-compact.json
done
xcrun llvm-profdata merge -sparse build-host/coverage/unit-raw/*.profraw -o build-host/coverage/unit.profdata
python3 scripts/check-coverage.py
node --test --experimental-test-coverage --test-coverage-lines=80 \
  '--test-coverage-include=**/firmware/main/web/*.js' \
  tests/test_phone_location.js tests/test_phone_connection.js \
  tests/test_phone_reconnect_ui.js tests/test_location_editor.js tests/test_update_ui.js \
  | tee build-host/coverage/browser.txt
python3 scripts/check-browser-coverage.py
