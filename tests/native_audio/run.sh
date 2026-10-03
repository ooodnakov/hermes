#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${CXX:-c++}"
temporary_dir="$(mktemp -d)"
trap 'rm -rf "$temporary_dir"' EXIT

"$compiler" -std=c++17 -Wall -Wextra -Werror \
  "$repo_root/tests/native_audio/test_pcm16.cpp" \
  -o "$temporary_dir/test_pcm16"
"$temporary_dir/test_pcm16"
