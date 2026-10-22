# Hardening your boards: Secure Boot, flash encryption, NVS encryption

The stock Spooky Lights images are built for convenience: flash them from a web
page, update them any time. The price is that **anyone who can plug a USB-C
cable into the light owns it.** In about 30 seconds they can:

* read the whole flash, including your **Thread network key** (enough to join your
  Thread mesh), your **Zigbee network key** (enough to sniff and inject on every Zigbee
  device you own), or your **Wi-Fi password**, plus the light's Matter keys and
  pairing code;
* flash their own firmware, which keeps those keys and stays in your network as a
  rogue router.

If your lights live outside on the porch, that's a real exposure. This guide locks
**your own boards** down with the security hardware built into the ESP32-C6 (NanoC6)
and ESP32-H2 (NanoH2).

> ⚠️ **Read this whole page before you start.** Hardening **burns eFuses**: one-time
> bits inside the chip. **There is no undo.** A hardened board:
> * only ever runs firmware **signed with your key**. The stock release `.bin` files and
>   ESPHome Web won't work on it again;
> * can only be updated over USB with **that board's own key**, from the key folder
>   this guide creates. Lose it and the board keeps working, but can't be updated.
>
> Do your first one on a **spare board**.

## What changes

| Attack with physical access | Stock image | Hardened |
|---|---|---|
| Read the flash over USB (keys, Wi-Fi password) | ✅ works | ❌ Secure Download Mode refuses to read |
| Read the flash chip directly (desolder / clip) | ✅ plaintext | ❌ XTS-AES-128 ciphertext; key locked in eFuse |
| Read NVS secrets from a flash dump | ✅ plaintext | ❌ NVS encryption, HMAC key locked in eFuse |
| Flash your own firmware | ✅ works | ❌ Secure Boot V2 rejects unsigned images |
| Debug over JTAG (USB or pins) | ✅ works | ❌ disabled in eFuse |
| Erase / overwrite the flash | ✅ | ⚠️ still possible: it **bricks** the board, but steals nothing |
| Hold the button 5 s to unpair it | ✅ | ⚠️ still works (see [Limits](#limits-what-this-doesnt-fix)) |

### The three features, and why all three

1. **Secure Boot V2** (ECDSA P-256). The chip's ROM checks the bootloader's signature,
   then the bootloader checks the app's. Only images signed with your private key run.
2. **Flash encryption, Release mode.** The bootloader, partition table, app and the
   Zigbee storage partitions are encrypted with a key that's burned into the chip and
   can't be read back. Release mode also turns off JTAG and limits USB to
   **Secure Download Mode**: writing flash still works, but reading it doesn't.
3. **NVS encryption** (HMAC scheme). This is the one that protects your network keys.
   Wi-Fi, OpenThread, Matter, Bluetooth and the pairing code all live in NVS, and **NVS
   can't use flash encryption** (Espressif's docs: "NVS partitions cannot be encrypted").
   It needs its own scheme. Ours derives the NVS keys from an HMAC key in eFuse.

### Why it needs a different build

Arduino's prebuilt libraries have all of this switched off, and NVS encryption has to be
compiled into the libraries themselves. So the hardened images are built with
**ESP-IDF 5.5**, using **Arduino-ESP32 3.3.12 as a component**. That's the same Arduino
version and the same sketch, compiled from source with the security options on. The
project is in [`hardened/`](../hardened). CI builds it for every board on every change,
and dry-runs the eFuse sequence (*Hardened build* workflow).

## The eFuse plan

The ESP32-C6 and ESP32-H2 have six 256-bit key blocks. The provisioning script uses them like this:

| Block | Purpose | What's in it | Copy kept? |
|---|---|---|---|
| `BLOCK_KEY0` | `XTS_AES_128_KEY` | This board's own flash-encryption key | **Yes**, in `~/.spooky-keys/devices/<name>/`. You need it for USB updates |
| `BLOCK_KEY1` | `SECURE_BOOT_DIGEST0` | Hash of your Secure Boot public key | The private key, in `~/.spooky-keys/` |
| `BLOCK_KEY2` | `SECURE_BOOT_DIGEST1` | Hash of your *backup* signing key (optional) | Offline |
| `BLOCK_KEY3–4` | (unused) | | |
| `BLOCK_KEY5` | `HMAC_UP` | Random NVS-encryption root key | **No**: it exists only inside the chip |

Unused Secure Boot slots are **revoked**, so nobody can add a key later. Keys in
`BLOCK_KEY0` and `BLOCK_KEY5` are read-protected: software, JTAG and USB can't read them.
On first boot the chip also burns `SECURE_BOOT_EN`, `SPI_BOOT_CRYPT_CNT`, `DIS_USB_JTAG`,
`DIS_PAD_JTAG`, `DIS_DIRECT_BOOT`, `DIS_DOWNLOAD_ICACHE`, `DIS_DOWNLOAD_MANUAL_ENCRYPT`
and `ENABLE_SECURITY_DOWNLOAD`.

> **Why a host-generated flash-encryption key?** If the chip makes its own key, nobody
> can ever encrypt an update for it. In Release mode the only way left would be
> over-the-air updates, which this project doesn't have. Making the key on your computer
> (one per board) keeps USB updates possible. The trade-off: protect that key folder
> like a house key.

## 0. What you need

* A **Linux or macOS** computer. On Windows, use WSL2 and pass the USB port through with
  `usbipd`.
* **ESP-IDF v5.5.5** (Arduino-ESP32 3.3.12 is built on 5.5). Install it with Espressif's
  installer (EIM), or use Docker: `docker run --rm -it -v $PWD:/w -w /w --device=/dev/ttyACM0 espressif/idf:v5.5.5`.
  Open an IDF shell with `. $IDF_PATH/export.sh`. `idf.py --version` should print `v5.5.x`.
* This repository (`git clone`), about 5 GB of disk for the first build, and a USB-C
  **data** cable.
* A board that has **never been hardened**. The script checks.
* Decide **Zigbee or Matter now**. The partition layouts differ, so a hardened board stays
  on the stack it was provisioned with. Thread and Wi-Fi Matter share a layout.

## 1. Make your keys (once)

```bash
mkdir -p ~/.spooky-keys && chmod 700 ~/.spooky-keys
espsecure.py generate_signing_key --version 2 --scheme ecdsa256 ~/.spooky-keys/secure_boot_signing_key.pem

# Optional but wise: a backup signing key, so a lost or leaked primary isn't fatal.
espsecure.py generate_signing_key --version 2 --scheme ecdsa256 ~/.spooky-keys/secure_boot_signing_key_backup.pem
```

The backup key must be in that folder when you provision a board, because its hash gets
burned then. **Afterwards, move it off this computer** (an encrypted USB stick in a drawer).

**Back up `~/.spooky-keys/`** (encrypted, offline) after every new board. Everything in it
is needed for updates, and none of it can be recovered from the boards.

## 2. Build

```bash
. $IDF_PATH/export.sh
hardened/build.sh nanoc6 matter         # or: nanoc6 zigbee | nanoc6 matter-wifi | nanoh2 zigbee
```

The first build downloads ESP-IDF components (Arduino, esp-matter, the Zigbee SDK) and
takes 10 to 30 minutes; later builds are quicker. The script:
* re-checks the final configuration, failing the build if any security option didn't take;
* verifies both signatures;
* puts the signed, still-plaintext images in `hardened/out/<board>-<stack>/` with a
  `flash.txt` of offsets and hashes.

The `config.h` options apply as usual. For an outdoor light, consider
`MATTER_NEW_CODE_ON_DECOMMISSION 1`: a decommission then also discards the pairing code.

## 3. Provision the board (once per board)

Plug the board in, then do a dry run on virtual eFuses first. It checks your keys and the
sequence without touching the chip:

```bash
hardened/provision.sh --virt nanoc6-matter porch-pumpkin -
```

Then for real (the port is usually `/dev/ttyACM0` on Linux, `/dev/cu.usbmodem*` on macOS):

```bash
hardened/provision.sh nanoc6-matter porch-pumpkin /dev/ttyACM0
```

It shows exactly what it's about to burn and asks you to type the board's name. Then it:
1. checks that no security eFuses are burned yet. If any are, it stops;
2. erases the flash and writes the images **without letting the board boot**;
3. makes this board's flash-encryption key in `~/.spooky-keys/devices/porch-pumpkin/`,
   burns it plus a random NVS key and your signing-key hash(es), and revokes unused slots;
4. re-reads the eFuses to check every purpose landed, then resets the board.

If the script can't put the board into download mode: unplug it, **hold the button**
(G9, the boot pin on both boards), plug it in, release, and run it again.

## 4. First boot

Watch the serial monitor at 115200 baud (`idf.py -p /dev/ttyACM0 monitor`, or the Arduino
IDE's monitor). The first boot **takes up to a minute**: the bootloader encrypts the flash
in place, then enables Secure Boot and locks JTAG and download mode. **Don't unplug it**
until the usual banner appears:

```
== Spooky Lights v1.1.4 · NanoC6 · Matter ==
```

After that, pair it exactly as before ([MATTER.md](MATTER.md) or the README's Zigbee
section).

## 5. Check it worked

```bash
# 1. The chip reports its security state (this works in Secure Download Mode):
esptool.py --chip esp32c6 -p /dev/ttyACM0 --no-stub get_security_info
#    → Secure Boot: Enabled, Flash Encryption: Enabled, plus the key purposes above

# 2. Reading the flash must FAIL now. This is the attack you've closed:
esptool.py --chip esp32c6 -p /dev/ttyACM0 --no-stub read_flash 0x20000 0x1000 should-fail.bin
```

Then the real-life test: **pair it, power-cycle it, and check it comes back paired** (Zigbee:
same device in ZHA, Matter: online in HA). That proves the encrypted NVS and the encrypted
Zigbee storage both survive a reboot. Do this on your spare board first.

## 6. Updating a hardened board

```bash
git pull
hardened/build.sh nanoc6 matter                              # same key, same stack
hardened/update.sh nanoc6-matter porch-pumpkin /dev/ttyACM0  # keeps the pairing
```

`update.sh` encrypts the new app **on your computer** with that board's own key, then
writes the ciphertext through Secure Download Mode. If you give it the wrong key or an
unsigned build, the board won't boot, but nothing is lost: run it again with the right
key and build. `--dry-run` encrypts without flashing.

Only the app is updated. The bootloader and partition table stay as provisioned, which is
the safe default with Secure Boot.

## Limits: what this doesn't fix

* **Bricking.** Someone with USB access can still erase or overwrite the flash. They
  can't steal anything or run their own code, but the light stops working. Keep the
  board and its USB port **inside the enclosure**.
* **The button.** Holding it for 5 s still decommissions the light, so someone who can
  touch the light *and* has its pairing code (or, on Zigbee, an open network nearby) can
  take over **that light**. They don't get into your network. Recess the button, and keep
  the pairing label indoors.
* **The serial console** still prints the pairing QR code while the light is unpaired.
  It's on the same USB port, so the enclosure advice applies here too.
* **Radio-level attacks.** The Zigbee join still uses the standard default link key
  (install codes aren't possible with the Arduino Zigbee library yet). Keep permit-join
  windows short.
* **Lab attacks** (voltage or EM fault injection, power analysis) are outside what this
  is meant to stop. The C6's built-in DPA protection is on at its default level.
* **Matter attestation** still uses the test vendor ID and certificate. Commissioners
  will keep calling it "uncertified".

## Physical hardening that's worth the effort

* **An enclosure** with the board, its USB-C port and the button inside. Bring 5 V in
  through a gland, not the USB port on the outside.
* **A resettable fuse** (~1.1 A hold) on the 5 V feeding the Grove string, and **TVS
  diodes** (5 V, low capacitance) on Grove CLK/DATA for long outdoor runs. The firmware
  limits current, but only the fuse protects you if the firmware is wrong or a cable shorts.
* **The pairing label** in the house, not on the light.

## If something goes wrong

| Symptom | What it means / what to do |
|---|---|
| `provision.sh` says eFuses are already burned | This board was hardened, or half-hardened, before. Don't force it. Check `espefuse.py -p PORT summary`. |
| First boot never shows the banner, keeps rebooting | Power-cycle once and wait a full minute. If it's still looping, capture the bootloader log: a signature error means the image wasn't signed with the key whose hash was burned. |
| `update.sh` ran but the board won't boot | Wrong board key or build. Re-run with the right `device-name`, or a fresh `build.sh`. |
| Pairing is lost on every power cycle | Encrypted storage isn't being kept. Please open an issue with the log (pairing codes blanked out). |
| Lost `~/.spooky-keys/devices/<name>/flash_encryption_key.bin` | That board can no longer be updated over USB. It keeps working as it is. |
| Lost the signing key(s) | No new firmware will boot on any of your hardened boards. That's why the backup key exists. |

## Status

CI builds the hardened project for every board and stack, checks that all security
options are in the final configuration and that the images are signed, and dry-runs
`provision.sh` on **virtual** eFuses and `update.sh` without flashing. **It hasn't been run
on a real board yet.** Use a spare board first, and please report how it went.

## The commands, without the scripts

For reference, here's what `provision.sh` runs, in order (C6 shown; use `esp32h2` for a
NanoH2):

```bash
esptool.py  --chip esp32c6 -p PORT --after no_reset erase_flash
esptool.py  --chip esp32c6 -p PORT --after no_reset write_flash --flash_mode keep --flash_freq keep --flash_size keep \
            0x0 bootloader.bin 0xC000 partition-table.bin 0x20000 spooky_lights.bin
espsecure.py generate_flash_encryption_key fe_key.bin
espefuse.py --chip esp32c6 -p PORT burn_key        BLOCK_KEY0 fe_key.bin    XTS_AES_128_KEY
espefuse.py --chip esp32c6 -p PORT burn_key        BLOCK_KEY5 nvs_hmac.bin  HMAC_UP          # 32 random bytes, then delete
espefuse.py --chip esp32c6 -p PORT burn_key_digest BLOCK_KEY1 secure_boot_signing_key.pem SECURE_BOOT_DIGEST0
espefuse.py --chip esp32c6 -p PORT burn_efuse      SECURE_BOOT_KEY_REVOKE1 1 SECURE_BOOT_KEY_REVOKE2 1
# reset → first boot encrypts and locks
```

and `update.sh`:

```bash
espsecure.py encrypt_flash_data --aes_xts --keyfile fe_key.bin --address 0x20000 --output app.enc.bin spooky_lights.bin
esptool.py --chip esp32c6 -p PORT --no-stub write_flash -u --flash_mode keep --flash_freq keep --flash_size keep 0x20000 app.enc.bin
```

Espressif's references: [Secure Boot V2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/security/secure-boot-v2.html) ·
[Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/security/flash-encryption.html) ·
[NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/storage/nvs_encryption.html) ·
[Security overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/security/security.html)
