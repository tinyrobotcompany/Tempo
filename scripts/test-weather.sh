#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/components/station_weather/include -Ifirmware/managed_components/espressif__cjson/cJSON tests/test_weather.c firmware/components/station_weather/station_weather.c firmware/managed_components/espressif__cjson/cJSON/cJSON.c -lm -o build-host/test_weather
./build-host/test_weather
cc -std=c11 -Wall -Wextra -Werror -Dtime=test_time -Itests/network_stubs -Ifirmware/main -Ifirmware/components/station_core/include -Ifirmware/components/station_weather/include -Ifirmware/managed_components/espressif__cjson/cJSON tests/test_weather_service.c firmware/main/weather_service.c firmware/components/station_weather/station_weather.c firmware/managed_components/espressif__cjson/cJSON/cJSON.c -lm -o build-host/test_weather_service
./build-host/test_weather_service
