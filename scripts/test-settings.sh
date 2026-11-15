#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
cc -std=c11 -Wall -Wextra -Werror -Itests/network_stubs -Ifirmware/main -Ifirmware/components/station_core/include tests/test_settings.c firmware/components/station_core/station_core.c -o build-host/test_settings
./build-host/test_settings
