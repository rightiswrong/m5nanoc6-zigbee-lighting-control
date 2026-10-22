#!/usr/bin/env bash
# First install of a hardened image on a NEW board. Burns eFuses: PERMANENT.
#
#   hardened/provision.sh [--virt] <board-stack> <device-name> <port>
#     board-stack  nanoc6-zigbee | nanoc6-matter | nanoc6-matter-wifi | nanoh2-zigbee
#                  (built first with hardened/build.sh)
#     device-name  your name for this board, e.g. porch-pumpkin; its keys go to
#                  $SPOOKY_KEYS/devices/<device-name>/
#     --virt       dry run against virtual eFuses (no board needed; port is ignored)
#
# What it does, in this order (docs/HARDENING.md explains each step):
#   1. checks the board has never had security eFuses burned
#   2. erases the flash and writes the signed, still-plaintext images
#      WITHOUT letting the board boot
#   3. burns:  BLOCK_KEY0 = this board's own flash-encryption key (XTS_AES_128_KEY;
#                           a copy stays in your key folder for USB updates)
#              BLOCK_KEY1 = digest of your Secure Boot signing key (SECURE_BOOT_DIGEST0)
#              BLOCK_KEY2 = digest of your backup signing key, if you made one
#              BLOCK_KEY5 = random NVS-encryption HMAC key (HMAC_UP; never stored anywhere)
#              and revokes the unused Secure Boot key slots
#   4. resets the board: its first boot encrypts the flash in place and
#      switches on Secure Boot, JTAG lock-out and Secure Download Mode
set -euo pipefail
die() { echo "provision.sh: $*" >&2; exit 1; }

virt=0
if [[ ${1:-} == --virt ]]; then virt=1; shift; fi
[[ $# -eq 3 ]] || die "usage: provision.sh [--virt] <board-stack> <device-name> <port>"
bs=$1 name=$2 port=$3
[[ $name =~ ^[A-Za-z0-9_-]+$ ]] || die "device name: letters, digits, - and _ only"

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/out/$bs"
[[ -f $out/flash.txt ]] || die "no build in $out: run hardened/build.sh ${bs%%-*} ${bs#*-} first"
chip=$(sed -n 's/^chip=//p' "$out/flash.txt")
if [[ -e $out/NOT-FOR-FLASHING && $virt == 0 ]]; then
  die "$out was signed with a throwaway CI key: rebuild with your own key"
fi
command -v espefuse.py >/dev/null || die "no espefuse.py: open an ESP-IDF 5.5 shell first"

keys="${SPOOKY_KEYS:-$HOME/.spooky-keys}"
sbkey="$keys/secure_boot_signing_key.pem"
sbbackup="$keys/secure_boot_signing_key_backup.pem"
[[ -f $sbkey ]] || die "no signing key at $sbkey"
devdir="$keys/devices/$name"
[[ $virt == 1 ]] && devdir="$(mktemp -d)/$name"
[[ -e $devdir ]] && die "$devdir already exists: every board gets a new name (and a new key)"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
if [[ $virt == 1 ]]; then
  ef=(espefuse.py --chip "$chip" --virt --path-efuse-file "$tmp/virt-efuse.json")
  et=(true)
else
  ef=(espefuse.py --chip "$chip" -p "$port")
  et=(esptool.py --chip "$chip" -p "$port")
fi

# ---- 1. a virgin chip?
"${ef[@]}" summary --format json --file "$tmp/before.json" >/dev/null
python3 - "$tmp/before.json" <<'EOF' || die "this board already has security eFuses burned: refusing to touch it"
import json, sys
e = json.load(open(sys.argv[1]))
def val(k):
    v = e[k]["value"]
    return v
bad = []
for i in range(6):
    v = val(f"KEY_PURPOSE_{i}")
    if v not in ("USER", 0, "0"):
        bad.append(f"KEY_PURPOSE_{i}={v}")
if val("SECURE_BOOT_EN") not in (False, 0, "0"):
    bad.append("SECURE_BOOT_EN set")
if val("SPI_BOOT_CRYPT_CNT") not in (0, "0", "Disable"):
    bad.append(f"SPI_BOOT_CRYPT_CNT={val('SPI_BOOT_CRYPT_CNT')}")
if bad:
    print("  " + ", ".join(bad), file=sys.stderr)
    sys.exit(1)
EOF
echo "OK: no security eFuses burned yet."

backup_note="none (unused Secure Boot slots 1 and 2 will be revoked)"
[[ -f $sbbackup ]] && backup_note="$sbbackup → BLOCK_KEY2 (slot 2 revoked)"
cat <<EOF

About to PERMANENTLY lock this board ($chip, $( [[ $virt == 1 ]] && echo "VIRTUAL eFuses" || echo "port $port")):
  image           $out
  signing key     $sbkey
  backup key      $backup_note
  this board's flash-encryption key will be saved to: $devdir/
After this, the board only runs images signed with your key, and USB updates
need that saved key (hardened/update.sh). There is no undo.

EOF
if [[ ${SPOOKY_YES:-0} != 1 ]]; then
  read -r -p "Type the device name ($name) to continue: " answer
  [[ $answer == "$name" ]] || die "cancelled, nothing was burned"
fi

# ---- 2. flash (plaintext; the chip encrypts it on first boot), but don't boot yet
if [[ $virt == 0 ]]; then
  "${et[@]}" read_mac | tee "$tmp/mac.txt"
  "${et[@]}" --before default_reset --after no_reset erase_flash
  "${et[@]}" --before default_reset --after no_reset write_flash \
    --flash_mode keep --flash_freq keep --flash_size keep \
    0x0 "$out/bootloader.bin" 0xC000 "$out/partition-table.bin" 0x20000 "$out/spooky_lights.bin"
fi

# ---- 3. keys
mkdir -p "$devdir"
chmod 700 "$devdir"
espsecure.py generate_flash_encryption_key "$devdir/flash_encryption_key.bin" >/dev/null
chmod 400 "$devdir/flash_encryption_key.bin"
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(32))" > "$tmp/nvs_hmac_key.bin"

"${ef[@]}" --do-not-confirm burn_key BLOCK_KEY0 "$devdir/flash_encryption_key.bin" XTS_AES_128_KEY
"${ef[@]}" --do-not-confirm burn_key BLOCK_KEY5 "$tmp/nvs_hmac_key.bin" HMAC_UP
"${ef[@]}" --do-not-confirm burn_key_digest BLOCK_KEY1 "$sbkey" SECURE_BOOT_DIGEST0
if [[ -f $sbbackup ]]; then
  "${ef[@]}" --do-not-confirm burn_key_digest BLOCK_KEY2 "$sbbackup" SECURE_BOOT_DIGEST1
  "${ef[@]}" --do-not-confirm burn_efuse SECURE_BOOT_KEY_REVOKE2 1
else
  "${ef[@]}" --do-not-confirm burn_efuse SECURE_BOOT_KEY_REVOKE1 1 SECURE_BOOT_KEY_REVOKE2 1
fi
rm -f "$tmp/nvs_hmac_key.bin"           # lives only in the chip now (read-protected)

"${ef[@]}" summary --format json --file "$tmp/after.json" >/dev/null
python3 - "$tmp/after.json" "$( [[ -f $sbbackup ]] && echo 1 || echo 0)" <<'EOF' || die "eFuse check after burning failed (see above)"
import json, sys
e = json.load(open(sys.argv[1])); backup = sys.argv[2] == "1"
want = {"KEY_PURPOSE_0": "XTS_AES_128_KEY", "KEY_PURPOSE_1": "SECURE_BOOT_DIGEST0", "KEY_PURPOSE_5": "HMAC_UP"}
if backup:
    want["KEY_PURPOSE_2"] = "SECURE_BOOT_DIGEST1"
ok = True
for k, v in want.items():
    got = e[k]["value"]
    print(f"  {k:15} {got}")
    ok &= str(got) == v
for k in (["SECURE_BOOT_KEY_REVOKE2"] if backup else ["SECURE_BOOT_KEY_REVOKE1", "SECURE_BOOT_KEY_REVOKE2"]):
    got = e[k]["value"]
    print(f"  {k:15} {got}")
    ok &= got in (True, 1, "1")
for blk in ("BLOCK_KEY0", "BLOCK_KEY5"):          # keys must not be readable back
    rd = e.get(blk, {}).get("readable", None)
    if rd is not None:
        print(f"  {blk:15} readable={rd}")
        ok &= rd in (False, 0)
sys.exit(0 if ok else 1)
EOF

{
  echo "device=$name"
  echo "provisioned=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "image=$bs"
  [[ -f $tmp/mac.txt ]] && grep -i "^MAC" "$tmp/mac.txt" | head -n1
  cat "$out/flash.txt"
} > "$devdir/device.txt"

if [[ $virt == 1 ]]; then
  echo "== dry run OK: the eFuse sequence is valid for $chip (nothing real was burned) =="
  exit 0
fi

# ---- 4. first boot
"${et[@]}" --after hard_reset read_mac >/dev/null
cat <<EOF

== $name provisioned. It is booting for the first time now. ==
Open a serial monitor (115200). The first boot takes up to a minute: it
encrypts the flash in place. DON'T unplug it until the Spooky Lights banner
appears. It should be followed by its normal start-up.

Keep $devdir/flash_encryption_key.bin and your signing key(s) backed up offline.
Without them you can't update this board over USB.
Check the result: docs/HARDENING.md, "5. Check it worked".
EOF
