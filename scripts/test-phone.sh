#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main -Ifirmware/components/station_transport/include -Ifirmware/components/station_core/include -I"${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}" tests/test_phone_protocol.c firmware/main/phone_protocol.c firmware/components/station_core/station_core.c "${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}/cJSON.c" -lm -o build-host/test_phone_protocol
./build-host/test_phone_protocol
node tests/test_phone_location.js
node --check firmware/main/web/app.js
idf_components="${IDF_PATH:-$HOME/.espressif/v6.1/esp-idf}"
embed_script="${IDF_EMBED_SCRIPT:-$idf_components/tools/cmake/scripts/data_file_embed_asm.cmake}"
for asset in index.html app.js style.css location.js connection.js; do
  cmake -D "DATA_FILE=$PWD/firmware/main/web/$asset" -D "SOURCE_FILE=$PWD/build-host/phone-$asset.S" -D FILE_TYPE=TEXT -P "$embed_script"
done
cc -std=c11 -Wall -Wextra -Werror -Itests/phone_stubs -Itests/network_stubs -Ifirmware/main -Ifirmware/components/station_ui/include -Ifirmware/components/station_transport/include -Ifirmware/components/station_core/include -I"${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}" tests/test_phone_server.c build-host/phone-*.S firmware/main/phone_setup.c firmware/main/phone_protocol.c firmware/components/station_core/station_core.c "${CJSON_SOURCE_DIR:-firmware/managed_components/espressif__cjson/cJSON}/cJSON.c" -lm -o build-host/test_phone_server
./build-host/test_phone_server
for boot_case in --boot-local-unlocked --boot-local-on --boot-rotate --boot-unreadable --boot-no-lock; do
  ./build-host/test_phone_server "$boot_case"
done

node tests/test_phone_connection.js
node tests/test_phone_reconnect_ui.js
node tests/test_phone_local_mode.js

node tests/test_update_ui.js
