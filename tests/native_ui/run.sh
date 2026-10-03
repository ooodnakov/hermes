#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${CXX:-c++}"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT

"$compiler" -std=c++17 -Wall -Wextra -Werror -Wno-sign-compare \
  -I"$root/tests/native_ui/stubs" -I"$root/src" \
  "$root/tests/native_ui/test_familiar_ui.cpp" \
  "$root/src/ui/familiar_ui.cpp" \
  "$root/src/ui/emoji_text.cpp" \
  "$root/src/ui/emoji_assets.cpp" \
  "$root/src/ui/nerd_icons.cpp" \
  -o "$binary"
"$binary"
