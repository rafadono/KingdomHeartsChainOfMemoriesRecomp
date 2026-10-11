# Headless capture + scripted-input harness

Developer tooling to look at the game without a screen: run it headless (SDL dummy video driver, software
renderer), drive it with a deterministic input script, and save PNG frames that can be opened with any image
viewer (or an AI assistant's image reader). Everything is **env-gated**: with no `KHCOM_*` harness variable the
code paths cost one cached flag test per present / per texture update and nothing else.

Code: `src/capture_harness.{h,cpp}` (hooks in `src/sdl_intercepts.cpp`, `src/game_config.cpp`, `src/main.cpp`).
Runners: `tools/capture/run_capture.ps1` (Windows) and `tools/capture/run_capture.sh` (Linux / Docker).
Montage / crop tool: `tools/capture/montage.py` (Python stdlib only). Scenario: `tools/capture/scenarios/dialogue_study.ps1`.

## Frame numbers

Every frame number below is the **PPU frame counter** of the runtime (0 at boot, restored by a savestate load).
Captures arm when the counter reaches the number and are written at the next present (so the image is the frame
at, or at most one frame after, the requested number; with fast-forward up to `multiplier - 1` frames later).

## Environment variables

| Variable | Meaning |
|---|---|
| `KHCOM_CAPTURE_DIR=<dir>` | Output directory (created). Default `captures` when `KHCOM_CAPTURE_FRAMES` is set. |
| `KHCOM_CAPTURE_FRAMES=300,600-610,2000-3000/250` | Frames to capture. Ranges `a-b`, optional `/step`. |
| `KHCOM_CAPTURE_MODE=output\|native\|both` | `output` (default): the final composed window image (after filters, grid mask, overlays, letterbox bars), read back with `SDL_RenderReadPixels` right before the real present; its size is the renderer output size. `native`: the PPU framebuffer as passed to `SDL_UpdateTexture`, **before** colour correction, 284x160 (the 240x160 picture sits in the middle with 22 px black pillars in 3:2 mode; 16:9 battles fill all 284 px). |
| `KHCOM_CAPTURE_VARIANTS="852x480 screen_mask=1\|960x640 screen_mask=0"` | One `output` image per entry at each captured frame: window size `WxH` and/or live settings (`key=value`, khcom_config.ini keys `screen_mask`, `mask_intensity`, `color_profile`, `xbrz_scale`, `motion_smoothing`, `hdtext_*` (integer keys of `[HdText]`, see docs/TEXT_OVERLAY_RENDER.md) plus `hdtext_family` (family by index), `perf_mode`) are applied, the harness waits for the size to settle (>= 4 presents) and grabs. Use it on a static frame. File name `f<frame>_output_<entry sanitized>.png`. |
| `KHCOM_EXIT_AFTER_CAPTURE=1` | Quit cleanly (SDL_QUIT) once every requested image has been written. (The runners set it.) |
| `KHCOM_EXIT_FRAME=<n>` | Quit at that frame even without captures. |
| `KHCOM_WINDOW_SIZE=WxH` | Window / output size. Applied with `SDL_SetWindowSize` on the first presents (gbarecomp's own `--scale` only gives `base * k`); the log prints `window/output size is now WxH`. The game keeps 3:2 inside the window (pillar/letter boxes), exactly as in a resized real window. |
| `KHCOM_INPUT_SCRIPT=<file or inline>` | Scripted input, see below. |
| `KHCOM_PUSH_KEYS="9000:Escape;9200:F2"` | Host key presses: `<frame>:<SDL key name>` entries separated by `;`, pushed as SDL key events at that emulated frame (for example `Escape` opens the settings menu, `F2` the conversation log). Unknown names are reported on stderr. Not translated to GBA buttons (use `KHCOM_INPUT_SCRIPT` for those). |
| `KHCOM_SET="screen_mask=0;hdtext_enabled=1;hdtext_size=100"` | Settings overrides at start, same keys as `khcom_config.ini`, applied after the file. A harness run never writes `khcom_config.ini`. |
| `KHCOM_SAVESTATE_LOAD=<file>` | Load a savestate at start (translated to gbarecomp's `--load-state`). |
| `KHCOM_SAVESTATE_SAVE=<frame>:<slot>` | Save into slot `<slot>` (file `<rom name>.state<slot>` next to the ROM) at that frame (translated to `GBARECOMP_ASSIST_SCRIPT=<frame>:save<slot>`; the pump index equals the frame number to within a few frames). |
| `KHCOM_FAST=1` | 4x fast-forward (`GBARECOMP_ASSIST_SCRIPT=1:fast_on`). The game stays deterministic; only wall clock changes. |
| `KHCOM_CAPTURE_*`/`SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy` | Set by the runners. Without the dummy driver the normal window opens. |

## Input script

Text file (or the inline string itself); entries separated by newlines or `;`; `#` starts a comment.

```
120-125: A                   # hold A for frames 120..125 (inclusive)
200: START                   # a single frame N is held for frames N and N+1
1500-6000 every 100 hold 3: A      # press A for 3 frames at the start of every 100-frame period
300-340: UP+A                # several buttons
```
Buttons: `A B L R START SELECT UP DOWN LEFT RIGHT`. The script is expanded into a
`frame,0xKEYINPUT` trace (active-low) and handed to gbarecomp's deterministic **input replay**
(`GBARECOMP_INPUT_REPLAY=<capture dir>/input_trace.txt`), so the same script always yields the same run. (Writing
KEYINPUT from the per-frame hook is not enough: the host pump overwrites it before the guest reads it.) No devices are
needed. Do not set `GBARECOMP_INPUT_REPLAY` yourself at the same time.

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

Battles use the 16:9 view (`[video] view_width = 284` in game.toml): the field is a 3:2 picture with pillar bars, the
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
