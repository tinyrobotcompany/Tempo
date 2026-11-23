#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Use the pinned ESP-IDF checkout}"
source_dir="$IDF_PATH/components/mbedtls/mbedtls"
build="$PWD/build-host/mbedtls-real"
mkdir -p "$build/include/mbedtls" "$build/fixtures"
# ESP's fork uses legacy include names in two portable crypto source files.
# Map to the same SDK headers without enabling ESP hardware acceleration.
printf '#include "mbedtls/private/bignum.h"\n' > "$build/include/mbedtls/bignum.h"
printf '#include "mbedtls/private/ecp.h"\n' > "$build/include/mbedtls/ecp.h"
cat > "$build/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.10.2)
project(tempo_tls_proof C)
add_subdirectory("$source_dir" sdk)
add_executable(tempo_tls_test "$PWD/tests/test_phone_tls_real.c")
target_include_directories(tempo_tls_test PRIVATE "$source_dir/include" "$source_dir/tf-psa-crypto/include" "$source_dir/tf-psa-crypto/drivers/builtin/include" "$PWD/tests/tls_stubs" "$PWD/tests/phone_stubs" "$PWD/tests/network_stubs" "$PWD/firmware/main")
target_link_libraries(tempo_tls_test PRIVATE mbedx509)
CMAKE
cmake -S "$build" -B "$build/host" -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DGEN_FILES=OFF "-DCMAKE_C_FLAGS=-I$build/include" > "$build/configure.log" 2>&1
cmake --build "$build/host" --target tempo_tls_test -j 4 > "$build/build.log" 2>&1
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -pkeyopt ec_param_enc:named_curve -sha256 -nodes -keyout "$build/fixtures/ca.key" -out "$build/fixtures/ca.pem" -days 1 -subj /CN=TestCA > "$build/fixtures.log" 2>&1
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -pkeyopt ec_param_enc:named_curve -sha256 -nodes -keyout "$build/fixtures/device.key" -out "$build/fixtures/device.csr" -subj /CN=device-test.tempo.tinyrobotcompany.io >> "$build/fixtures.log" 2>&1
printf 'subjectAltName=DNS:device-test.tempo.tinyrobotcompany.io\nbasicConstraints=CA:FALSE\n' > "$build/fixtures/extensions"
openssl x509 -sha256 -req -in "$build/fixtures/device.csr" -CA "$build/fixtures/ca.pem" -CAkey "$build/fixtures/ca.key" -CAcreateserial -days 1 -extfile "$build/fixtures/extensions" -out "$build/fixtures/leaf.pem" >> "$build/fixtures.log" 2>&1
printf 'malformed' > "$build/fixtures/invalid.pem"
"$build/host/tempo_tls_test" "$build/fixtures/leaf.pem" "$build/fixtures/device.key" "$build/fixtures/ca.key" "$build/fixtures/invalid.pem"
