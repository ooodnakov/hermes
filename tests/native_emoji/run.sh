#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -Wall -Wextra -Werror \
  "$root/src/ui/emoji_assets.cpp" \
  "$root/tests/native_emoji/test_emoji_assets.cpp" \
  -o "$build_dir/test_emoji_assets"
"$build_dir/test_emoji_assets"
