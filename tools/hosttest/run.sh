#!/usr/bin/env bash
# Compile the firmware logic for the host against API stubs and run the tests.
# Builds twice: once against the newer colour-light API (core >= 3.3.5) and once
# against the older one, so both #ifdef branches are proven to compile.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../../firmware/NanoC6_SpookyLights"
out="${TMPDIR:-/tmp}/spooky-hosttest"
mkdir -p "$out"
flags=(-std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Werror
       -DZIGBEE_MODE_ZCZR -I"$here/stubs" -I"$fw" -fsanitize=address,undefined)
srcs=("$here/test_main.cpp" "$fw/P9813Chain.cpp" "$fw/Effects.cpp" "$fw/Renderer.cpp")
for variant in new old; do
  extra=()
  [[ $variant == new ]] && extra=(-DHOST_NEW_COLOR_API)
  echo "== build ($variant colour-light API) =="
  g++ "${flags[@]}" "${extra[@]}" -x c++ "${srcs[@]}" -o "$out/test_$variant" -lm
  echo "== run ($variant) =="
  "$out/test_$variant"
done
