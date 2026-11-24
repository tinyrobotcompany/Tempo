#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -z "${CJSON_SOURCE_DIR:-}" ]]; then
  python3 scripts/prepare-tests.py
  source build-host/test-env.sh
fi
mkdir -p build-host
cc -std=c11 -Wall -Wextra -Werror -Itests/enrollment_stubs -Itests/tls_stubs -Itests/phone_stubs -Itests/network_stubs -Ifirmware/main tests/test_device_identity.c -o build-host/test_device_identity
./build-host/test_device_identity
cc -std=c11 -Wall -Wextra -Werror -Itests/enrollment_stubs -Itests/phone_stubs -Itests/network_stubs -Ifirmware/main -Ifirmware/components/station_core/include -I"$CJSON_SOURCE_DIR" tests/test_device_enrollment.c "$CJSON_SOURCE_DIR/cJSON.c" -o build-host/test_device_enrollment
./build-host/test_device_enrollment
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main tests/test_phone_dns.c -o build-host/test_phone_dns
./build-host/test_phone_dns
cc -std=c11 -Wall -Wextra -Werror -Itests/tls_stubs -Itests/phone_stubs -Itests/network_stubs -Ifirmware/main tests/test_phone_tls.c -o build-host/test_phone_tls
./build-host/test_phone_tls
cc -std=c11 -Wall -Wextra -Werror -Itests/phone_stubs -Itests/network_stubs -Ifirmware/main tests/test_pairing.c -o build-host/test_pairing
./build-host/test_pairing
cc -std=c11 -Wall -Wextra -Werror -Itests/network_stubs -Ifirmware/main tests/test_credentials.c -o build-host/test_credentials
./build-host/test_credentials
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/components/station_core/include -I"$CJSON_SOURCE_DIR" tests/test_update_policy.c firmware/main/update_policy.c "$CJSON_SOURCE_DIR/cJSON.c" -lm -o build-host/test_update_policy
./build-host/test_update_policy
cc -std=c11 -Wall -Wextra -Werror -Itests/update_stubs -Itests/network_stubs -Ifirmware/main -Ifirmware/components/station_core/include -I"$CJSON_SOURCE_DIR" tests/test_update_service.c firmware/main/phone_protocol.c firmware/components/station_core/station_core.c firmware/main/update_policy.c "$CJSON_SOURCE_DIR/cJSON.c" -lm -o build-host/test_update_service
./build-host/test_update_service
