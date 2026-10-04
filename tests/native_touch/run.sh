#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_root/tests/native_touch/stubs" -I"$repo_root/src" \
  "$repo_root/tests/native_touch/test_axs15231b_touch.cpp" \
  "$repo_root/src/input/axs15231b_touch.cpp" -o "$binary"
"$binary"
