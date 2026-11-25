#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "${TEMPO_TEST_LOGGING:-0}" != 1 ]]; then
  mkdir -p build-host/test-results
  TEMPO_TEST_LOGGING=1 bash "$0" "$@" 2>&1 | tee build-host/test-results/latest.log
  exit "${PIPESTATUS[0]}"
fi
echo "Tempo host validation started $(date -u +%Y-%m-%dT%H:%M:%SZ)"
for tool in cc cmake node python3 openssl aea aa actionlint uv; do
  command -v "$tool" >/dev/null || { echo "Missing required test tool: $tool (full suite requires macOS)." >&2; exit 1; }
done
python3 scripts/release.py check
python3 scripts/prepare-tests.py
source build-host/test-env.sh
if [[ "${TEMPO_CERTIFICATES_SEPARATE_JOB:-0}" != 1 ]]; then bash scripts/test-certificates.sh; fi
uv run --project mechanical/cad --frozen pytest mechanical/cad/tests
bash scripts/test-coverage.sh
LLVM_PROFILE_FILE="$PWD/build-host/coverage/asan-%p.profraw" bash scripts/test-map-asan.sh
for suite in tests/test_*.py; do python3 "$suite"; done
node --test .github/scripts/*.test.mjs
for suite in .github/scripts/*.test.sh; do bash "$suite"; done
actionlint .github/workflows/*.yaml .github/workflows/*.yml
echo 'All Tempo tests passed.'
