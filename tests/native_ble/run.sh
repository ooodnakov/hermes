#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${TMPDIR:-/tmp}/hermes-native-ble"
mkdir -p "$build_dir"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  "$repo_root/tests/native_ble/test_v1_ble_transport.cpp" \
  "$repo_root/src/network/v1_ble_transport.cpp" \
  -o "$build_dir/test_v1_ble_transport"
"$build_dir/test_v1_ble_transport"
