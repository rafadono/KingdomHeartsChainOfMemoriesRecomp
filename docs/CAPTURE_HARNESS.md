# Headless capture + scripted-input harness

Developer tooling to look at the game without a screen: run it headless (SDL dummy video driver, software
renderer), drive it with a deterministic input script and save PNG frames that can be opened with any image
viewer. Everything is **env-gated**: with no `KHCOM_*` harness variable set the hooks cost one cached flag test
and nothing else.

The harness is the generic `gbaimp::capture` module of gba-improvements. Its reference documentation (every
variable, the input script syntax, frame numbering, limitations) is
[external/gba-improvements/docs/CAPTURE_HARNESS.md](../external/gba-improvements/docs/CAPTURE_HARNESS.md); this page
only describes what is specific to this game. The variable prefix is `KHCOM` (`KHCOM_CAPTURE_DIR`,
`KHCOM_CAPTURE_FRAMES`, `KHCOM_CAPTURE_MODE`, `KHCOM_CAPTURE_VARIANTS`, `KHCOM_EXIT_AFTER_CAPTURE`,
`KHCOM_EXIT_FRAME`, `KHCOM_WINDOW_SIZE`, `KHCOM_INPUT_SCRIPT`, `KHCOM_PUSH_KEYS`, `KHCOM_SET`,
`KHCOM_SAVESTATE_LOAD`, `KHCOM_SAVESTATE_SAVE`, `KHCOM_FAST`).

Wiring in this repository: `khcom::configure_capture_harness()` (`src/game_config.cpp`, called first thing in
`main()` in `src/main.cpp`), the SDL hooks in `src/sdl_intercepts.cpp` (`wants_native_frame` / `on_native_frame` /
`before_present`) and the per-frame hook in `create_run_options()`.
Runners: `tools/capture/run_capture.ps1` (Windows) and `tools/capture/run_capture.sh` (Linux / Docker).
Montage / crop tool: `tools/capture/montage.py` (Python standard library only). Scenario: `tools/capture/scenarios/dialogue_study.ps1`.

## Game-specific settings

* `KHCOM_SET="screen_mask=0;hdtext_enabled=1;hdtext_size=100"`: overrides applied over `khcom_config.ini` at start (the
  keys of the file, plus `hdtext_family` = family by index). A harness run never writes `khcom_config.ini`.
* `KHCOM_CAPTURE_VARIANTS="852x480 screen_mask=1|960x640 screen_mask=0"`: the live settings of a variant are applied by this
  game's callback: `screen_mask`, `mask_intensity`, `color_profile`, `xbrz_scale`, `motion_smoothing`, `perf_mode`
  and the `hdtext_*` keys (see `docs/TEXT_OVERLAY_RENDER.md`, plus `hdtext_family`). Other keys are ignored.
* `native` captures have the logical size of the active view (284x160 with the default `view_width` of `game.toml`; the 240x160 picture sits in the
  middle with black pillars in 3:2 mode; wide battles fill the whole width). Pass `--view-width N` (240, 256, 284, 373, 569) to pick another preset.
* The `output` image of a run with `fps_target` above 60 or motion smoothing on has been seen to differ by one presented frame between
  runs of the same executable; for a bit-exact comparison pass `KHCOM_SET="fps_target=0;motion_smoothing=0"`. `native` captures are always exact.

## Running

Windows (from the repository root, after `.\build.ps1 -BuildDir build-vs`):
```powershell
.\tools\capture\run_capture.ps1 -Dir captures\t1 -Frames "10500" -Mode both -Window 852x480 `
    -Set "perf_mode=0;screen_mask=0;hdtext_enabled=1;hdtext_size=100" `
    -Script tools\capture\scripts\first_dialogue.txt -Fast
```
Linux / Docker (after building into `build/`):
```bash
docker run --rm -v "<repo>:/src" -w /src khcom-builder \
    tools/capture/run_capture.sh -d captures/t1 -f 10500 -m both -w 852x480 \
    -S "perf_mode=0;screen_mask=0;hdtext_enabled=1;hdtext_size=100" \
    -s tools/capture/scripts/first_dialogue.txt -F
```
Both runners start the game with `--rom/--bios` (so no launcher), kill it on timeout, and list the PNGs.

Runner parameters. `run_capture.ps1`: `-Dir -Frames -Script -Set -Window -Variants -Mode -LoadState -SaveState -Fast -Sharp -ExtraArgs -ExitFrame -TimeoutSec -Exe -Rom -Bios -Log`
(defaults: `-Exe KHCOMRecomp.exe`, `-Bios "bios\gba (W).gba"`, `-Rom` = the ROM file name used in this checkout's `roms\` folder; pass `-Rom` for yours).
`run_capture.sh`: `-d DIR -f FRAMES -s SCRIPT -S SET -w WxH -m MODE -V VARIANTS -l STATE -v FRAME:SLOT -F -x FRAME -t SEC -e EXE -H` (`-e` default `build/KHCOMRecomp`;
the ROM and BIOS paths are written inside the script, edit them for your file names). The header of each runner has the full usage text.

`perf_mode=0` removes the FPS overlay from `output` captures (it is on in a typical `khcom_config.ini`).

Contact sheets: `python3 tools/capture/montage.py sheet.png --cols 3 --crop 268,317,497,106 --zoom 2 a.png b.png ...`
(`--crop` in pixels or as fractions of the image; each tile gets an index number, the legend is printed).

## Reaching the first dialogue

`tools/capture/scripts/first_dialogue.txt`: START at 1300, then A every 100 frames from 1500 and no input after
10350. Timeline (PPU frames; identical with or without fast-forward): logos 0-1000, title 1200 (PRESS START), file
select ~1650, opening movie ~1900-6300 (Sora/Donald/Goofy on the road, Namine drawing ~5900), castle title card
~6300-8100, castle hall without text ~8400-8700, first text pages ~9000+ (Goofy "You sure we...", Donald "We have to
if we're gonna find the king...", ...). **Sora's page "One look at this castle, and I just knew: They're here." is on
screen and fully typed from ~10450 on; capture frame 10500.** With `tools/capture/scripts/explore_a.txt` (A every 100
frames up to 30000) later pages are at 12300 (Sora "To check it out! What's the matter, scared?"), 13500 (Sora "Who is
THAT"), 13800 (Donald "It must be a Heartless!..."), and the first (tutorial) battle is running from ~24000 on
(dark tutorial box "B Button: Jump ...", card HUD at the bottom left): capture 26000.

Battles use the wide view (`[video] view_width = 284` in game.toml; other presets via `--view-width`): the field is a 3:2 picture with pillar bars, the
battle fills the whole 16:9 window. `ExtraArgs`/`--view-width 240` (runner `-ExtraArgs "--view-width 240"`) gives the
3:2 battle for comparison. A bare `--rom/--bios` run uses the nearest-neighbour scaler; pass `-Sharp` / `-H`
(`--sharp-filter 1`: integer prescale + linear finish) to reproduce the launcher's default scaler.

## Scenarios

`tools/capture/scenarios/dialogue_study.ps1 [-Sharp] [-Only <cfg,...>] [-Parallel 4]`: one boot-to-dialogue
run per configuration (several in parallel), each producing the native frame plus output images for the
window-size x screen-mask variants into `captures\dialogue_study\<cfg>\`. **Stale:** its configurations are still written with the removed in-engine text keys
(`text_mode`, `font_family`, `font_size`), which are now parsed and ignored, so every configuration currently renders the original text. Rewrite the
configurations with the `hdtext_*` keys (see `docs/TEXT_OVERLAY_RENDER.md`) before using it for text studies. `tools/capture/stroke_runs.py` prints the
run-length histogram of 1-source-pixel strokes of an output crop (integer scale k: every run is a multiple of k;
5.4x: runs of 5 and 6 px).

## Limitations / caveats

* **Savestates and the HD text overlay:** the overlay stands down for a page whose layout call it did not see (a state loaded in the middle of a page
  shows the original text until the next page starts), see `docs/TEXT_OVERLAY_ENGINE.md`. For text captures prefer a state saved shortly before a
  page start or a run from boot.
* With `-Fast`/`KHCOM_FAST` only every 4th frame is presented, so a capture can lag the requested frame by up to 3.
* The `output` capture needs a renderer that supports readback; the software renderer (dummy driver) does. On Windows
  the D3D11 renderer request fails on the dummy driver and gbarecomp falls back to the software renderer.
* `native` frames are pre colour-correction; `output` frames include colour profile, grid mask, perf HUD, backlog.
* Output pixels are whatever the renderer produced for the (virtual) window; the dummy driver has no real display
  scaling, so DPI scaling of a real desktop is not modelled.
