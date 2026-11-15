#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
cc -std=c11 -Wall -Wextra -Werror -Dsettimeofday=test_settimeofday \
  -Itests/network_stubs -Ifirmware/main -Ifirmware/components/station_core/include \
  -I"${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}" \
  tests/test_network.c firmware/main/network_time.c \
  firmware/components/station_core/station_core.c firmware/components/station_core/timezones.c \
  "${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}/cJSON.c" \
  -o build-host/test_network
./build-host/test_network
