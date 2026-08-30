#!/usr/bin/env bash
# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
# and is not covered by this notice.

#
# UI regression: drive the emulator through a fixed set of screens, capture
# each framebuffer, and compare byte-for-byte against tests/ui_golden/.
#
#   tools/ui_regression.sh            check against the golden images
#   tools/ui_regression.sh --update   re-bless the golden images
#
# Only deterministic screens are captured. SYS is deliberately excluded: it
# shows heap, elapsed time and steps/sec, none of which repeat run to run.
#
# The screens driven here are the ones the device shows as it ships. A scene
# file can drive a different sequence instead:
#
#   SK_SCENE=path/to/scene.txt SK_GOLD=path/to/goldens tools/ui_regression.sh
#
# Each scene line is "<delay> <command>", or just "<command>" for the default
# delay; blank lines and # comments are ignored.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Must match build_dir in platformio.ini (deliberately outside Dropbox).
BIN="${SK_BIN:-$HOME/.cache/pircis/build/emulator/program}"
GOLD="${SK_GOLD:-$ROOT/tests/ui_golden}"
SCENE="${SK_SCENE:-}"
UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

[ -x "$BIN" ] || { echo "build first: pio run -e emulator"; exit 2; }

WORK="$(mktemp -d)"
FIFO="$WORK/in"
mkfifo "$FIFO"
cd "$WORK"                      # fresh NVS + shots land here

"$BIN" < "$FIFO" > "$WORK/log.txt" 2>&1 &
PID=$!
exec 3> "$FIFO"

say() { printf '%s\n' "$1" >&3; sleep "${2:-0.6}"; }

sleep 2.5

# Coordinates come from the button rects in src/Ui.cpp, not from eyeballing.
# Both tab sets are six wide, so the bar geometry is the same either way:
#   w = 80 -> centres 40 120 200 280 360 440
#   locked    RUN OUT EDIT PROG SAVE SYS
#   PROG rows y = 26 + 26*i + 11       ^ v < > at y 203, x 371/401/431/461
#   modal row y 301, x 62+118i         done row y 253
#
# NOTE: tapping the tab you are already on is the play shortcut, so every tap
# below moves to a DIFFERENT tab. Do not "re-select" the current one.
default_scene() {
  cat <<'SCENE'
# The welcome dialog is up at power-on; capture it, then tap it away.
1    shot 00a_splash.ppm
     tap 240 160
     speed slow
1    shot 00_locked_run.ppm
     tap 200 306
1    shot 00b_locked_edit.ppm
     tap 280 306
1    shot 00c_locked_prog.ppm
     tap 360 306
1    shot 00d_locked_save.ppm
     tap 440 306
1    shot 00e_locked_sys.ppm
     tap 359 37
1    shot 00e2_night_sys.ppm
     tap 40 306
1    shot 00e3_night_run.ppm
     tap 440 306
     tap 359 37
     tap 40 306
     speed full
     run 5
1    shot 00f_locked_done.ppm
     tap 120 306
1    shot 00g_locked_out.ppm
1.5  quit
SCENE
}

# Each line is "<delay> <command>" or just "<command>".
run_scene() {
  while IFS= read -r line; do
    case "$line" in ''|'#'*) continue ;; esac
    local first rest
    first=${line%% *}
    rest=${line#* }
    case "$first" in
      [0-9]*) say "$rest" "$first" ;;
      *)      say "$line" ;;
    esac
  done
}

if [ -n "$SCENE" ]; then
  [ -r "$SCENE" ] || { echo "cannot read scene $SCENE"; exit 2; }
  run_scene < "$SCENE"
else
  default_scene | run_scene
fi

exec 3>&-

# The emulator exits on `quit`. If it does not, something is wedged -- most
# likely a nested lock, which deadlocks on the board too (the platform mutex is
# non-recursive on both targets precisely so this shows up here).
hung=1
for _ in $(seq 1 30); do
  kill -0 $PID 2>/dev/null || { hung=0; break; }
  sleep 0.2
done
if [ $hung -eq 1 ]; then
  echo "FAIL: emulator did not exit on quit -- possible deadlock"
  kill -9 $PID 2>/dev/null
  sed -n '$p' "$WORK/log.txt"
  exit 1
fi
wait $PID 2>/dev/null

shopt -s nullglob
shots=(*.ppm)
if [ ${#shots[@]} -eq 0 ]; then
  echo "FAIL: the emulator produced no screenshots"
  sed -n '1,20p' "$WORK/log.txt"
  exit 1
fi

status=0
if [ $UPDATE -eq 1 ]; then
  mkdir -p "$GOLD"
  for f in "${shots[@]}"; do cp "$f" "$GOLD/$f"; echo "blessed $f"; done
else
  for f in "${shots[@]}"; do
    if [ ! -f "$GOLD/$f" ]; then
      echo "  MISSING golden $f  (run with --update)"
      status=1
    elif cmp -s "$f" "$GOLD/$f"; then
      echo "  ok    $f"
    else
      echo "  FAIL  $f differs"
      cp "$f" "$GOLD/../ui_actual_$f"
      status=1
    fi
  done
  [ $status -eq 0 ] && echo "UI regression passed (${#shots[@]} screens)" \
                    || echo "UI regression FAILED -- actual images kept as tests/ui_actual_*.ppm"
fi

rm -rf "$WORK"
exit $status
