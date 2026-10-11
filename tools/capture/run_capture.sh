#!/usr/bin/env bash
# Headless capture run for the Linux build (build/KHCOMRecomp) -- see docs/CAPTURE_HARNESS.md.
# Usage: tools/capture/run_capture.sh [options]
#   -d DIR      output dir (default captures/run)     -f FRAMES  e.g. "300,600-610,1200"
#   -s SCRIPT   input script file or inline text      -S SET     "key=v;key=v"
#   -w WxH      output size (e.g. 852x480)            -m MODE    output|native|both
#   -V VARIANTS "852x480 screen_mask=0|960x640" (one output image per entry at each captured frame)
#   -l STATE    load savestate file                   -v FRAME:SLOT  save state
#   -F          4x fast-forward                       -x FRAME   quit at emulated frame
#   -t SEC      timeout (default 240)                 -e EXE     (default build/KHCOMRecomp)
#   -H          pass --sharp-filter 1 (integer prescale + linear finish; the launcher default)
#   -r ROM      ROM path (default: $KHCOM_ROM or the first roms/*.gba)   -b BIOS  BIOS path (default: $KHCOM_BIOS or bios/gba (W).gba)
# Run it directly on a Linux box, or inside the builder image from the repo root:
#   docker run --rm -v "<repo>:/src" -w /src khcom-builder tools/capture/run_capture.sh -f 300 -d captures/x
set -euo pipefail
cd "$(dirname "$0")/../.."
SHARP= DIR=captures/run VARIANTS= FRAMES= SCRIPT= SETS= WIN= MODE=output LOAD= SAVE= FAST= EXITF= TMO=240 EXE=build/KHCOMRecomp
ROM=${KHCOM_ROM:-} BIOS=${KHCOM_BIOS:-}
while getopts "d:f:s:S:w:V:m:l:v:Fx:t:e:Hr:b:" o; do
  case $o in
    H) SHARP="--sharp-filter 1";; V) VARIANTS=$OPTARG;; d) DIR=$OPTARG;; f) FRAMES=$OPTARG;; s) SCRIPT=$OPTARG;; S) SETS=$OPTARG;; w) WIN=$OPTARG;;
    m) MODE=$OPTARG;; l) LOAD=$OPTARG;; v) SAVE=$OPTARG;; F) FAST=1;; x) EXITF=$OPTARG;; t) TMO=$OPTARG;; e) EXE=$OPTARG;; r) ROM=$OPTARG;; b) BIOS=$OPTARG;;
  esac
done
if [ -z "$ROM" ]; then ROM=$(ls roms/*.gba 2>/dev/null | head -n 1 || true); fi
[ -z "$BIOS" ] && BIOS="bios/gba (W).gba"
[ -n "$ROM" ] || { echo "no ROM found: put one in roms/ or pass -r / set KHCOM_ROM" >&2; exit 2; }
mkdir -p "$DIR"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
export KHCOM_CAPTURE_DIR="$PWD/$DIR" KHCOM_CAPTURE_MODE="$MODE"
[ -n "$FRAMES" ] && { export KHCOM_CAPTURE_FRAMES="$FRAMES" KHCOM_EXIT_AFTER_CAPTURE=1; }
[ -n "$SCRIPT" ] && { if [ -f "$SCRIPT" ]; then export KHCOM_INPUT_SCRIPT="$PWD/$SCRIPT"; else export KHCOM_INPUT_SCRIPT="$SCRIPT"; fi; }
[ -n "$SETS" ] && export KHCOM_SET="$SETS"
[ -n "$WIN" ] && export KHCOM_WINDOW_SIZE="$WIN"
[ -n "$VARIANTS" ] && export KHCOM_CAPTURE_VARIANTS="$VARIANTS"
[ -n "$LOAD" ] && export KHCOM_SAVESTATE_LOAD="$PWD/$LOAD"
[ -n "$SAVE" ] && export KHCOM_SAVESTATE_SAVE="$SAVE"
[ -n "$FAST" ] && export KHCOM_FAST=1
[ -n "$EXITF" ] && export KHCOM_EXIT_FRAME="$EXITF"
set +e
# shellcheck disable=SC2086
timeout "$TMO" "$EXE" --rom "$ROM" --bios "$BIOS" $SHARP \
  >"$DIR/run.log" 2>"$DIR/run.log.err"
echo "exit code: $?  (logs: $DIR/run.log, $DIR/run.log.err)"
ls -1 "$DIR"/*.png 2>/dev/null || true
