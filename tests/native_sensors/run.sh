#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_root/tests/native_sensors/stubs" -I"$repo_root/src" \
  "$repo_root/tests/native_sensors/test_sensors.cpp" \
  "$repo_root/src/peripherals/sensors.cpp" -o "$binary"
"$binary"
