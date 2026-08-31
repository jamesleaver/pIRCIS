#!/usr/bin/env bash
# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
# and is not covered by this notice.

#
# Build the flashable artifacts for a release and hash them.
#
#   tools/release.sh              build into dist/
#   tools/release.sh --publish    build, then create the GitHub release
#
# One archive per panel, each holding the three images esptool writes and the
# offsets they go to. The SHA256SUMS file covers every image, so someone who
# downloads a build can check it is the one this script made -- and the
# version and build stamp inside the binary are on the device's own ABOUT
# THIS DEVICE page, so a board in your hand can be matched to a release
# without taking anyone's word for it.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(sed -n 's/.*PIRCIS_VERSION "\([^"]*\)".*/\1/p' "$ROOT/src/Version.h")"
[ -n "$VERSION" ] || { echo "no PIRCIS_VERSION in src/Version.h"; exit 2; }

BUILD="${SK_BUILD:-$HOME/.cache/pircis/build}"
DIST="$ROOT/dist"
PUBLISH=0
[ "${1:-}" = "--publish" ] && PUBLISH=1

# Offsets come from the ESP32 Arduino layout; huge_app.csv puts the app at
# 0x10000. Written into the archive so the flash command is not folklore.
BOOTLOADER_OFFSET=0x1000
PARTITIONS_OFFSET=0x8000
APP_OFFSET=0x10000

# macOS ships shasum, Linux (and so CI) ships sha256sum; same output format.
if command -v sha256sum >/dev/null; then SHA=(sha256sum); else SHA=(shasum -a 256); fi

rm -rf "$DIST"
mkdir -p "$DIST"

for env in st7796 ili9488; do
  echo "==> building $env"
  ( cd "$ROOT" && pio run -e "$env" >/dev/null )

  out="$DIST/pircis-$VERSION-$env"
  mkdir -p "$out"
  for f in bootloader.bin partitions.bin firmware.bin; do
    cp "$BUILD/$env/$f" "$out/$f"
  done

  cat > "$out/flash.sh" <<FLASH
#!/usr/bin/env bash
# pIRCIS $VERSION -- $env
# Needs esptool (pip install esptool). Put the board in download mode if it
# does not reset on its own: hold BOOT, tap EN, release BOOT.
set -eu
PORT="\${1:-}"
if [ -z "\$PORT" ]; then
  echo "usage: ./flash.sh <port>"
  echo
  echo "The serial ports on this machine:"
  ls /dev/cu.* 2>/dev/null | grep -v -e Bluetooth -e debug-console -e wlan-debug \\
    | sed 's/^/  /' || echo "  none found -- is the board plugged in?"
  exit 2
fi
# esptool 5 renamed most of these; the old spellings still work but print a
# wall of deprecation warnings before the flash, which looks like a fault.
# Fall back to the old names on esptool 4 and earlier.
ESPTOOL=esptool
command -v esptool >/dev/null 2>&1 || ESPTOOL=esptool.py
if "\$ESPTOOL" version 2>/dev/null | grep -qE '^esptool v[5-9]'; then
  MODE=(--flash-mode dio --flash-freq 40m --flash-size detect)
  RESET=(--before default-reset --after hard-reset)
  WRITE=write-flash
else
  MODE=(--flash_mode dio --flash_freq 40m --flash_size detect)
  RESET=(--before default_reset --after hard_reset)
  WRITE=write_flash
fi
cd "\$(dirname "\$0")"
"\$ESPTOOL" --chip esp32 --port "\$PORT" --baud 460800 \\
  "\${RESET[@]}" "\$WRITE" -z "\${MODE[@]}" \\
  $BOOTLOADER_OFFSET bootloader.bin \\
  $PARTITIONS_OFFSET partitions.bin \\
  $APP_OFFSET firmware.bin
FLASH
  chmod +x "$out/flash.sh"

  cat > "$out/README.txt" <<TXT
pIRCIS $VERSION -- $env panel

  ./flash.sh <port>

Run it with no arguments and it lists the serial ports it can see. On a Mac
the board is usually /dev/cu.usbserial-something; ls /dev/cu.* shows them all.

or by hand:

  esptool.py --chip esp32 --port <PORT> --baud 460800 write_flash -z \\
    $BOOTLOADER_OFFSET bootloader.bin \\
    $PARTITIONS_OFFSET partitions.bin \\
    $APP_OFFSET firmware.bin

The 4" Freenove boards ship with one of two display controllers. If the
screen stays blank or the colours look inverted, flash the other archive.
Check what you have flashed on the device itself: SYS > ABOUT THIS DEVICE,
last page, shows the version, the build stamp and the panel this binary
drives.

Verify the download against SHA256SUMS before flashing:

  shasum -a 256 -c SHA256SUMS     (or: sha256sum -c SHA256SUMS)

That checks the three images in this directory. To check the archive you
downloaded, run the same command against the SHA256SUMS published beside it
on the releases page.
TXT

  # A manifest INSIDE the archive, covering the three images, so the check
  # still works once someone has unzipped it.
  ( cd "$out" && "${SHA[@]}" bootloader.bin partitions.bin firmware.bin > SHA256SUMS )

  ( cd "$DIST" && zip -qr "pircis-$VERSION-$env.zip" "pircis-$VERSION-$env" )
done

# The release manifest covers exactly what is PUBLISHED -- the archives, and
# nothing else. It used to list the loose images as well, which are only ever
# inside an archive: anyone following the README got two OK lines and six
# "FAILED open or read" for files that were never uploaded, which reads like a
# corrupt release rather than a manifest describing the wrong thing.
( cd "$DIST" && "${SHA[@]}" *.zip > SHA256SUMS )

echo
echo "pIRCIS $VERSION"
cat "$DIST/SHA256SUMS"
echo
echo "artifacts in dist/"

if [ "$PUBLISH" = 1 ]; then
  command -v gh >/dev/null || { echo "gh not installed"; exit 2; }
  echo "==> creating release v$VERSION"
  gh release create "v$VERSION" \
    "$DIST"/*.zip "$DIST/SHA256SUMS" \
    --title "pIRCIS $VERSION" \
    --notes-file "$ROOT/RELEASE_NOTES.md"
fi
