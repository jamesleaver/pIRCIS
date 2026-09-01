#!/usr/bin/env bash
# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
# and is not covered by this notice.

#
# Regenerate every screenshot and GIF in shots/ from the emulator, so the
# pictures in the README are never a build or two behind the device.
#
#   tools/make_shots.sh            everything
#   tools/make_shots.sh hello      just the ones whose name matches
#
# Frames come from the emulator's own `shot` command through the regression
# harness, which already knows how to drive a scene; ffmpeg turns a run of
# them into a GIF with a per-clip palette, which matters because the panel
# uses flat colours that dither badly against a generic one.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SHOTS="$ROOT/shots"
BIN="${SK_BIN:-$HOME/.cache/pircis/build/emulator/program}"
ONLY="${1:-}"

[ -x "$BIN" ] || { echo "build first: pio run -e emulator"; exit 2; }
command -v ffmpeg >/dev/null || { echo "needs ffmpeg (brew install ffmpeg)"; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Run a scene and leave its shots in $1.
drive() {
  local out="$1" scene="$2"
  mkdir -p "$out"
  SK_SCENE="$scene" SK_GOLD="$out" bash "$ROOT/tools/ui_regression.sh" --update >/dev/null 2>&1
}

want() { [ -z "$ONLY" ] || case "$1" in *"$ONLY"*) return 0;; *) return 1;; esac; }

# ---------------------------------------------------------------------------
# GIFs: load a program, press play, and film until after it has finished, so
# the clip ends on the output rather than cutting away mid-run.
# ---------------------------------------------------------------------------
gif() {
  local name="$1" program="$2" speed="$3" frames="$4" gap="$5" dest="$6"
  want "$name" || return 0
  echo "==> $name.gif"
  local dir="$WORK/$name"
  local scene="$WORK/$name.scene"
  {
    echo "1    tap 240 160"          # dismiss the welcome
    echo "     progload $program"
    echo "     speed $speed"
    echo "0.3  tap 48 306"           # the RUN tab is play
    for i in $(seq -f "%03g" 1 "$frames"); do
      echo "$gap  shot f$i.ppm"
    done
    echo "1    quit"
  } > "$scene"
  drive "$dir" "$scene"

  ffmpeg -y -loglevel error -framerate 12 -i "$dir/f%03d.ppm" \
    -vf "palettegen=stats_mode=full" "$dir/pal.png"
  ffmpeg -y -loglevel error -framerate 12 -i "$dir/f%03d.ppm" -i "$dir/pal.png" \
    -lavfi "paletteuse=dither=none" -loop 0 "$dest/$name.gif"
  echo "    $(ls -lh "$dest/$name.gif" | awk '{print $5}'), $frames frames"
}

# ---------------------------------------------------------------------------
# Stills.
# ---------------------------------------------------------------------------
still() {
  local name="$1" dest="$2"; shift 2
  want "$name" || return 0
  echo "==> $name.png"
  local dir="$WORK/still_$name"
  local scene="$WORK/still_$name.scene"
  { echo "1    tap 240 160"; printf '%s\n' "$@"; echo "1    quit"; } > "$scene"
  drive "$dir" "$scene"
  ffmpeg -y -loglevel error -i "$dir/shot.ppm" "$dest/$name.png"
}

mkdir -p "$SHOTS/gifs"

# The banner first: motto draws IRCIS by walking it, which is the one clip
# that explains the language and the device in the same breath.
gif motto     Talking/Motto           medium 64 0.30 "$SHOTS"
gif hello     Counting/Hello-World    medium 46 0.16 "$SHOTS"
gif spiral    Watching/Spiral         medium 60 0.16 "$SHOTS"
# These run for a few hundred steps. At FULL the run is over before the first
# frame lands and the clip is a still of the output; at MEDIUM with wider
# spacing the whole run fits and the last frames are the result.
gif clock     Deciding/Dumb-Clock     medium 64 0.20 "$SHOTS/gifs"
gif dice      Showing-off/Dice-Roll   medium 64 0.20 "$SHOTS/gifs"
gif race      Showing-off/Race        medium 72 0.20 "$SHOTS/gifs"
gif racetrack Showing-off/Racetrack   medium 80 0.22 "$SHOTS/gifs"

still hello_out  "$SHOTS" "     progload Counting/Hello-World" "     speed full" "0.3  tap 48 306" "3    tap 144 306" "1    shot shot.ppm"
still programs   "$SHOTS" "     tap 336 306" "1    shot shot.ppm"
# and one level in, so the folders are shown doing something
still folder     "$SHOTS" "     tap 336 306" "0.6  tap 240 115" "1    shot shot.ppm"
still editor     "$SHOTS" "     tap 240 306" "1    shot shot.ppm"
still editor_abc "$SHOTS" "     tap 240 306" "0.4  tap 240 306" "1    shot shot.ppm"
still system     "$SHOTS" "     tap 432 306" "1    shot shot.ppm"
# A grid with no readable text in it at all.
still insult     "$SHOTS" "     progload Showing-off/Insult-Machine" "1    shot shot.ppm"
# The run history: three programs, then step back through them.
still history    "$SHOTS" "     progload Counting/Countdown" "     speed full" "0.3  tap 48 306" \
                          "1.5  progload Deciding/Coin-Flip" "     speed full" "0.3  tap 48 306" \
                          "1.5  tap 144 306" "0.6  tap 18 279" "1  shot shot.ppm"

echo
echo "done -- shots/ and shots/gifs/ rewritten"
