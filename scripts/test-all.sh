#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
for tool in cc cmake node python3 openssl aea aa actionlint; do
  command -v "$tool" >/dev/null || { echo "Missing required test tool: $tool (full suite requires macOS)." >&2; exit 1; }
done
python3 scripts/prepare-tests.py
source build-host/test-env.sh
for suite in test.sh test-network.sh test-weather.sh test-transport.sh test-phone.sh test-ui.sh test-map-asan.sh; do
  echo "=== $suite ==="
  bash "scripts/$suite"
done
for suite in tests/test_*.py; do python3 "$suite"; done
node --test .github/scripts/*.test.mjs
for suite in .github/scripts/*.test.sh; do bash "$suite"; done
actionlint .github/workflows/*.yaml .github/workflows/*.yml
echo 'All Tempo tests passed.'
