#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -Ifirmware/components/station_core/include \
  firmware/components/station_core/station_core.c firmware/components/station_core/timezones.c tests/test_core.c -o build-host/test_core
./build-host/test_core
