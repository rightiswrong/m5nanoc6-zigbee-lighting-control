#!/usr/bin/env bash
# Compile the firmware for the host against API stubs and run the tests, for
# every board × radio-stack variant:
#   nanoc6-zigbee (new + old colour-light API), nanoh2-zigbee,
#   nanoc6-matter, nanoh2-matter
# Matter variants also print a pairing QR code on the simulated serial port;
# check_qr.py decodes it (independently, with jsQR if $JSQR points at it).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../../firmware/SpookyLights"
out="${TMPDIR:-/tmp}/spooky-hosttest"
mkdir -p "$out"
flags=(-std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Werror
       -I"$here/stubs" -I"$fw" -fsanitize=address,undefined)
cflags=(-std=c99 -O1 -g -Wall -Wextra -Werror -I"$fw" -fsanitize=address,undefined)
srcs=("$here/test_main.cpp" "$fw/P9813Chain.cpp" "$fw/Effects.cpp" "$fw/Demo.cpp" "$fw/Renderer.cpp"
      "$fw/Show.cpp" "$fw/MatterCodes.cpp" "$fw/TransportZigbee.cpp" "$fw/TransportMatter.cpp")

gcc "${cflags[@]}" -c "$fw/spookyqr.c" -o "$out/spookyqr.o"

build_and_run() {
  local name="$1"; shift
  echo "== build $name =="
  g++ "${flags[@]}" "$@" "${srcs[@]}" "$out/spookyqr.o" -o "$out/test_$name" -lm
  echo "== run $name =="
  if [[ $name == *matter* ]]; then
    "$out/test_$name" all "$out/pairing_$name.log"
    python3 "$here/check_qr.py" "$out/pairing_$name.log"
  else
    "$out/test_$name" all
  fi
  "$out/test_$name" orient
}

build_and_run nanoc6-zigbee     -DZIGBEE_MODE_ZCZR -DHOST_NEW_COLOR_API
build_and_run nanoc6-zigbee-old -DZIGBEE_MODE_ZCZR
build_and_run nanoh2-zigbee     -DZIGBEE_MODE_ZCZR -DHOST_NEW_COLOR_API -DCONFIG_IDF_TARGET_ESP32H2
build_and_run nanoc6-matter
build_and_run nanoh2-matter     -DCONFIG_IDF_TARGET_ESP32H2
