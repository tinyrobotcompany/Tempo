#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
archive="${LVGL_ARCHIVE:-$HOME/.espressif/tools/components/lvgl/lvgl/9.5.0/lvgl__lvgl-v9.5.0.zip}"
if [[ ! -f "$archive" ]]; then
  echo 'Set LVGL_ARCHIVE to the LVGL 9.5.0 component archive.' >&2
  exit 1
fi
mkdir -p build-host/lvgl
unzip -q -o "$archive" -d build-host/lvgl
cmake -S tests -B build-host/ui -DLVGL_SOURCE_DIR="$PWD/build-host/lvgl"
cmake --build build-host/ui -j 6
./build-host/ui/test_ui

./build-host/ui/test_map_tiles
