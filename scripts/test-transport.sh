#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
python3 tests/test_transport_tls_config.py
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/components/station_transport/include -I"${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}" tests/test_transport.c firmware/components/station_transport/station_map.c firmware/components/station_transport/transport_json_filter.c firmware/components/station_transport/station_transport.c "${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}/cJSON.c" -lm -o build-host/test_transport
./build-host/test_transport
cc -std=c11 -Wall -Wextra -Werror -Itests/transport_stubs -Itests/network_stubs -Ifirmware/main -Ifirmware/components/station_core/include -Ifirmware/components/station_transport/include -I"${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}" tests/test_transport_service.c firmware/components/station_transport/transport_json_filter.c firmware/components/station_transport/station_transport.c "${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}/cJSON.c" -lm -o build-host/test_transport_service
./build-host/test_transport_service

cc -std=c11 -Wall -Wextra -Werror -Ifirmware/components/station_transport/include -I"${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}" tests/test_transport_json_filter.c firmware/components/station_transport/transport_json_filter.c firmware/components/station_transport/station_transport.c "${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}/cJSON.c" -lm -o build-host/test_transport_json_filter
./build-host/test_transport_json_filter tests/fixtures/transport/traffic-compact.json
