#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main -Ifirmware/components/station_core/include -Ifirmware/managed_components/espressif__cjson/cJSON tests/test_phone_protocol.c firmware/main/phone_protocol.c firmware/components/station_core/station_core.c firmware/managed_components/espressif__cjson/cJSON/cJSON.c -lm -o build-host/test_phone_protocol
./build-host/test_phone_protocol
node tests/test_phone_location.js
node --check firmware/main/web/app.js
node --check companion/location-helper/helper.js
idf_components="${IDF_PATH:-$HOME/.espressif/v6.1/esp-idf}"
for asset in index.html app.js style.css location.js shortcut.js connection.js; do
  cmake -D "DATA_FILE=$PWD/firmware/main/web/$asset" -D "SOURCE_FILE=$PWD/build-host/phone-$asset.S" -D FILE_TYPE=TEXT -P "$idf_components/tools/cmake/scripts/data_file_embed_asm.cmake"
done
cc -std=c11 -Wall -Wextra -Werror -Itests/phone_stubs -Itests/network_stubs -Ifirmware/main -Ifirmware/components/station_core/include -Ifirmware/managed_components/espressif__cjson/cJSON tests/test_phone_server.c build-host/phone-*.S firmware/main/phone_setup.c firmware/main/phone_protocol.c firmware/components/station_core/station_core.c firmware/managed_components/espressif__cjson/cJSON/cJSON.c -lm -o build-host/test_phone_server
./build-host/test_phone_server

node tests/test_shortcut.js

# Exercise the production installer branch using the actual signed artifact.
cmake -D "DATA_FILE=$PWD/companion/shortcut/Weather Station Location.shortcut" -D "SOURCE_FILE=$PWD/build-host/location-installer.S" -D FILE_TYPE=BINARY -P "$idf_components/tools/cmake/scripts/data_file_embed_asm.cmake"
cc -std=c11 -Wall -Wextra -Werror -DSTATION_HAS_LOCATION_SHORTCUT=1 -Itests/phone_stubs -Itests/network_stubs -Ifirmware/main -Ifirmware/components/station_core/include -Ifirmware/managed_components/espressif__cjson/cJSON tests/test_phone_server.c build-host/phone-*.S build-host/location-installer.S firmware/main/phone_setup.c firmware/main/phone_protocol.c firmware/components/station_core/station_core.c firmware/managed_components/espressif__cjson/cJSON/cJSON.c -lm -o build-host/test_phone_server_installer
./build-host/test_phone_server_installer

python3 tests/test_shortcut_artifact.py

node tests/test_phone_connection.js
node tests/test_phone_reconnect_ui.js
