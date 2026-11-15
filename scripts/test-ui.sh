#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -z "${LVGL_SOURCE_DIR:-}" ]]; then
  python3 scripts/prepare-tests.py
  source build-host/test-env.sh
fi
cmake -S tests -B build-host/ui -DTEMPO_COVERAGE="${TEMPO_COVERAGE:-OFF}" -DLVGL_SOURCE_DIR="$LVGL_SOURCE_DIR" -DCJSON_SOURCE_DIR="${CJSON_SOURCE_DIR:-$PWD/firmware/managed_components/espressif__cjson/cJSON}"
cmake --build build-host/ui -j 6
./build-host/ui/test_ui
./build-host/ui/test_map_tiles
