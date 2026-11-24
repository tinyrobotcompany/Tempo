#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Use the pinned ESP-IDF checkout}"
source_dir="$IDF_PATH/components/mbedtls/mbedtls"
build="$PWD/build-host/identity-real"
mkdir -p "$build/include/mbedtls" "$build/fixtures"
printf '#include "mbedtls/private/bignum.h"\n' > "$build/include/mbedtls/bignum.h"
printf '#include "mbedtls/private/ecp.h"\n' > "$build/include/mbedtls/ecp.h"
python3 tests/identity_chain_fixtures.py "$build/fixtures" prepare
cat > "$build/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.10.2)
project(tempo_identity_proof C ASM)
add_subdirectory("$source_dir" sdk)
add_executable(identity_install_test "$PWD/tests/test_identity_install_real.c" "$build/fixtures/roots.S")
target_include_directories(identity_install_test PRIVATE "$source_dir/include" "$source_dir/tf-psa-crypto/include" "$source_dir/tf-psa-crypto/drivers/builtin/include" "$PWD/tests/tls_stubs" "$PWD/tests/phone_stubs" "$PWD/tests/network_stubs" "$PWD/firmware/main" "$PWD/tests/enrollment_stubs")
target_link_libraries(identity_install_test PRIVATE mbedx509)
CMAKE
cmake -S "$build" -B "$build/host" -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DGEN_FILES=OFF "-DCMAKE_C_FLAGS=-I$build/include" > "$build/configure.log" 2>&1
cmake --build "$build/host" --target identity_install_test -j 4 > "$build/build.log" 2>&1
(cd "$build/fixtures" && "$build/host/identity_install_test" csr)
python3 tests/identity_chain_fixtures.py "$build/fixtures" issue
(cd "$build/fixtures" && "$build/host/identity_install_test" verify)
