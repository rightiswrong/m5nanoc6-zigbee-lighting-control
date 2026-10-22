#!/usr/bin/env bash
# Update the app on a provisioned (hardened) board over USB. Keeps its pairing.
#
#   hardened/update.sh [--dry-run] <board-stack> <device-name> <port>
#
# The board is in Secure Download Mode and Release-mode flash encryption, so the
# new app (built with hardened/build.sh, signed with your key) is encrypted
# HERE with that board's own key, then written as ciphertext. --dry-run stops
# after encrypting.
set -euo pipefail
die() { echo "update.sh: $*" >&2; exit 1; }

dry=0
if [[ ${1:-} == --dry-run ]]; then dry=1; shift; fi
[[ $# -eq 3 ]] || die "usage: update.sh [--dry-run] <board-stack> <device-name> <port>"
bs=$1 name=$2 port=$3

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/out/$bs"
[[ -f $out/flash.txt ]] || die "no build in $out: run hardened/build.sh first"
[[ -e $out/NOT-FOR-FLASHING && $dry == 0 ]] && die "$out was signed with a throwaway CI key"
chip=$(sed -n 's/^chip=//p' "$out/flash.txt")
keys="${SPOOKY_KEYS:-$HOME/.spooky-keys}"
fekey="${SPOOKY_FE_KEY:-$keys/devices/$name/flash_encryption_key.bin}"
[[ -f $fekey ]] || die "no flash-encryption key for '$name' at $fekey"
if [[ -f $keys/devices/$name/device.txt ]]; then
  was=$(sed -n 's/^image=//p' "$keys/devices/$name/device.txt")
  [[ $was == "$bs" ]] || die "$name was provisioned with $was, not $bs (partition layouts differ)"
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
espsecure.py encrypt_flash_data --aes_xts --keyfile "$fekey" --address 0x20000 \
  --output "$tmp/spooky_lights.enc.bin" "$out/spooky_lights.bin"
echo "encrypted $(stat -c%s "$tmp/spooky_lights.enc.bin" 2>/dev/null || stat -f%z "$tmp/spooky_lights.enc.bin") bytes for $name"
if [[ $dry == 1 ]]; then
  echo "== dry run: not flashing =="
  exit 0
fi

# --no-stub: Secure Download Mode only accepts the ROM's own commands.
esptool.py --chip "$chip" -p "$port" --before default_reset --after hard_reset --no-stub \
  write_flash -u --flash_mode keep --flash_freq keep --flash_size keep \
  0x20000 "$tmp/spooky_lights.enc.bin"
echo "== $name updated; it's rebooting. A wrong key or an unsigned app just won't boot. =="
echo "   Recover by running this again with the right key and a signed build."
