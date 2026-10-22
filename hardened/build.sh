#!/usr/bin/env bash
# Build a hardened (Secure Boot V2 + Flash Encryption + NVS encryption) Spooky
# Lights image with ESP-IDF 5.5 and Arduino-ESP32 3.3.12 as a component.
#
#   hardened/build.sh <nanoc6|nanoh2> <zigbee|matter|matter-wifi>
#
# Needs an ESP-IDF 5.5 shell (. $IDF_PATH/export.sh) and your Secure Boot
# signing key at $SPOOKY_KEYS/secure_boot_signing_key.pem (default
# SPOOKY_KEYS=~/.spooky-keys). docs/HARDENING.md explains every step.
#
# Output: hardened/out/<board>-<stack>/{bootloader,partition-table,spooky_lights}.bin
# (signed, plaintext; the chip encrypts them on first boot) + flash.txt.
#
# CI only: SPOOKY_THROWAWAY_KEY=1 signs with a temporary key. Those images
# would only boot on a chip burned with that discarded key: never flash them.
set -euo pipefail

die() { echo "build.sh: $*" >&2; exit 1; }
[[ $# -eq 2 ]] || die "usage: build.sh <nanoc6|nanoh2> <zigbee|matter|matter-wifi>"
board=$1 stack=$2
case $board in
  nanoc6) chip=esp32c6 ;;
  nanoh2) chip=esp32h2 ;;
  *) die "board must be nanoc6 or nanoh2" ;;
esac
case $stack in
  zigbee) proj=zigbee ;;
  matter | matter-wifi)
    proj=matter
    [[ $board == nanoh2 ]] && die "the NanoH2 has too little RAM for Matter (see docs/MATTER.md); use zigbee"
    ;;
  *) die "stack must be zigbee, matter or matter-wifi" ;;
esac

here="$(cd "$(dirname "$0")" && pwd)"
command -v idf.py >/dev/null || die "no idf.py: open an ESP-IDF 5.5 shell first (. \$IDF_PATH/export.sh)"
idf_ver=$(idf.py --version 2>/dev/null || true)
[[ $idf_ver == *v5.5* ]] || die "needs ESP-IDF v5.5.x (Arduino-ESP32 3.3.12 is built on it), found: ${idf_ver:-none}"

keys="${SPOOKY_KEYS:-$HOME/.spooky-keys}"
key="$keys/secure_boot_signing_key.pem"
throwaway=0
if [[ ${SPOOKY_THROWAWAY_KEY:-0} == 1 ]]; then
  throwaway=1
  key="$(mktemp -d)/THROWAWAY_secure_boot_signing_key.pem"
  espsecure.py generate_signing_key --version 2 --scheme rsa3072 "$key" >/dev/null
fi
[[ -f $key ]] || die "no signing key at $key (docs/HARDENING.md, step 1)"

bdir="$here/build/$board-$stack"          # CMake build dir (idf.py owns it)
cdir="$here/build/$board-$stack.config"   # our generated config, kept outside it
out="$here/out/$board-$stack"
rm -rf "$bdir" "$out"                     # always a clean build from the defaults below
mkdir -p "$cdir" "$out"
keycfg="$cdir/sdkconfig.signing"
printf 'CONFIG_SECURE_BOOT_SIGNING_KEY="%s"\n' "$key" > "$keycfg"
defaults="$here/common/sdkconfig.arduino;$here/common/sdkconfig.security;$here/$proj/sdkconfig.defaults;$keycfg"
args=(-C "$here/$proj" -B "$bdir" -D SDKCONFIG="$cdir/sdkconfig" -D SDKCONFIG_DEFAULTS="$defaults")
[[ $stack == matter-wifi ]] && args+=(-D SPOOKY_WIFI=1)

rm -f "$cdir/sdkconfig"                 # regenerate from the defaults above
idf.py "${args[@]}" set-target "$chip"
idf.py "${args[@]}" build

# A misspelt option name in a defaults file is silently ignored, so check the
# security settings really made it into the final configuration.
cfg="$cdir/sdkconfig"
need=(CONFIG_SECURE_BOOT=y CONFIG_SECURE_BOOT_V2_ENABLED=y CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y
      CONFIG_SECURE_FLASH_ENC_ENABLED=y CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y
      CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE=y CONFIG_NVS_ENCRYPTION=y CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y
      CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=5 CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y CONFIG_PARTITION_TABLE_OFFSET=0xC000)
case $stack in
  zigbee) need+=(CONFIG_ZB_ENABLED=y CONFIG_ZB_ZCZR=y) ;;
  *) need+=(CONFIG_OPENTHREAD_ENABLED=y CONFIG_ENABLE_CHIPOBLE=y) ;;
esac
for opt in "${need[@]}"; do
  grep -qx "$opt" "$cfg" || die "final sdkconfig lacks $opt (got: $(grep "^${opt%%=*}=" "$cfg" || echo 'not set'))"
done

app="$bdir/spooky_lights.bin"
espsecure.py verify_signature --version 2 --keyfile "$key" "$app" >/dev/null || die "app signature doesn't verify"
espsecure.py verify_signature --version 2 --keyfile "$key" "$bdir/bootloader/bootloader.bin" >/dev/null ||
  die "bootloader signature doesn't verify"

cp "$bdir/bootloader/bootloader.bin" "$out/bootloader.bin"
cp "$bdir/partition_table/partition-table.bin" "$out/partition-table.bin"
cp "$app" "$out/spooky_lights.bin"
{
  echo "chip=$chip"
  echo "board=$board"
  echo "stack=$stack"
  echo "bootloader=0x0"
  echo "partition_table=0xC000"
  echo "app=0x20000"
  echo "signing_key_sha256=$(espsecure.py digest_sbv2_public_key --keyfile "$key" -o /dev/stdout 2>/dev/null | sha256sum | cut -c1-16)"
  echo "throwaway_key=$throwaway"
  (cd "$out" && sha256sum bootloader.bin partition-table.bin spooky_lights.bin | sed 's/^/sha256 /')
} > "$out/flash.txt"
[[ $throwaway == 1 ]] && touch "$out/NOT-FOR-FLASHING"
[[ $throwaway == 1 ]] || rm -f "$out/NOT-FOR-FLASHING"

size=$(stat -c%s "$app" 2>/dev/null || stat -f%z "$app")
echo
echo "== hardened $board-$stack built: $out (app $((size / 1024)) KiB, signed) =="
[[ $throwaway == 1 ]] && echo "!! signed with a THROWAWAY key (CI). Never flash these files."
echo "First install: hardened/provision.sh $board-$stack <device-name> <port>   Updates: hardened/update.sh"
