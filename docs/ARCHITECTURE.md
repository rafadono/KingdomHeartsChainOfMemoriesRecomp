# Architecture map

A short map of the game-specific code in this repository and of its main data flows. Everything generic (video/audio/input
enhancements, SDL interposition) lives in the `gba-improvements` library; the GBA runtime itself is `gbarecomp`. See
[GBA_IMPROVEMENTS_INTEGRATION.md](GBA_IMPROVEMENTS_INTEGRATION.md) for the wiring details.

## Layers

```
KHCOMRecomp (this repo)          src/*  config/*  generated/  generated_bios/
 |-- gba-improvements (MIT)      external/gba-improvements   namespace gbaimp: filters, upscaler, interpolator, audio, input, HUD, SDL interposition
 |-- gbarecomp (PolyForm NC)     gbarecomp/                  CPU/PPU/APU runtime, run_game(), RunOptions, assist tools (save state, rewind, fast-forward)
 `-- recomp-ui (MIT)             external/recomp-ui          launcher + in-game ImGui settings menu
```

`main()` (`src/main.cpp`) installs the SDL interposers, installs the RAM-overlay dispatcher, builds `RunOptions` with
`khcom::create_run_options()` (`src/game_config.cpp`) and calls `gbarecomp::run_game()` (through the recomp-ui launcher when it is
compiled in). `run_game()` resets every game-owned hook at start, so hooks are (re)installed from callbacks that run afterwards.

## Source tree (`src/`)

| File | Role |
|---|---|
| `main.cpp` | entry point (above); injects the capture-harness arguments |
| `game_config.{h,cpp}` | `RunOptions` (view modes 3:2 / 16:9, assist tools, UI callbacks), the settings schema (menu items, `khcom_config.ini` load/save, legacy keys), live setting changes for the harness |
| `gbaimp_integration.{h,cpp}` | registers the library hooks (`save_settings`, `is_input_blocked`, `frame_counter`) and the two HUD zones |
| `sdl_intercepts.cpp` | the six SDL handlers (`PollEvent`, `UpdateTexture`, `RenderCopy`, `RenderPresent`, `QueueAudio`, `OpenAudioDevice`) and their registration |
| `runtime_ui_standard.{h,cpp}` | rewrites the stock settings-menu config (view-mode labels, no RESUME row) and keeps the menu pointer |
| `runtime_ui_imgui_enhanced.cpp` | replacement of recomp-ui's ImGui menu renderer (draggable window); compiled instead of `runtime_ui_imgui.cpp` |
| `widescreen_adapter.{h,cpp}` | battle detection from mode-switch function entries, view-mode policy (`apply_view_policy()`), PPU providers (tilemap, BG X, OBJ X), effective viewport, function-entry hook chain |
| `music_hook.{h,cpp}` | m4a song start/stop observation that drives `gbaimp::HdAudioPlayer` |
| `dialogue_backlog.{h,cpp}` | conversation log: bus-read observer that captures dialogue text, sidebar renderer, hotkeys |
| `text_overlay/` | HD dialogue text (below) |
| `capture_harness.{h,cpp}` | env-gated headless capture and scripted input ([CAPTURE_HARNESS.md](CAPTURE_HARNESS.md)) |
| `ram_overlay_dispatch.{h,cpp}` | generated dispatcher: native code for the routines the game runs from IWRAM/EWRAM |
| `text_fonts.h`, `text_fonts_data.cpp` | embedded PT Sans / Narrow / Serif subsets (generated, see `third_party/fonts/README.md`) |
| `cutscene_dialogue_data.inl`, `scaffold_stub.cpp`, `msvc_compat.h` | data table for the backlog, link stub when `generated/` is absent, MSVC forced include |

## Data flows

### SDL interposition
gbarecomp and recomp-ui call SDL directly, so the game observes those calls: Linux uses `-Wl,--wrap=SDL_X` (flags in `CMakeLists.txt`,
`GBAIMP_DEFINE_LD_WRAPS()` in `sdl_intercepts.cpp`), Windows patches the Import Address Table at startup
(`gbaimp::install_sdl_interposers`, called from `khcom_install_sdl_interposers()` first in `main()`). Handlers call SDL only through
`gbaimp::sdl_real()`.

- `PollEvent`: refreshes widescreen state, offers events to the backlog and the perf HUD (consumed events are dropped).
- `UpdateTexture`: native-frame capture hook, then colour-profile correction of the 240x160 / 284x160 RGB24 frame.
- `RenderCopy` (game image only): computes the viewport (`khcom_compute_effective_viewport`), clears the bars, copies the image,
  draws the screen mask, then the **HD text overlay** (`engine::on_present`), so the settings menu that gbarecomp draws afterwards stays above the text.
- `RenderPresent`: frame interpolation, perf HUD, backlog sidebar, harness capture, then the real present.
- `OpenAudioDevice` / `QueueAudio`: audio path (below).

### View mode policy (3:2 vs 16:9)
The user picks the view in the recomp-ui menu; gbarecomp rewrites `g_ws_active/extra_left/extra_right/view_width`. The function-entry hook
(`khcom_fn_entry_hook`) watches mode-switch/handler addresses and sets a battle flag. `apply_view_policy()` (every frame, battle change and SDL poll) derives
the PPU globals from two inputs only, "view expanded?" and "in battle?": in 3:2 everything is reset to the faithful defaults; in 16:9 the providers are
installed and the margins show real arena content only in battle (black pillars otherwise). The HUD zones are shifted by the real extra width per side while
the wide picture is presented. There is no cached state, so mode switches and savestate loads cannot leave stale values.

### HD dialogue text overlay
Settings: `overlay::settings()` (`hdtext_*`). Engine side `text_overlay/engine_hooks.cpp` (installed in the function-entry chain only while the overlay is
enabled): captures the dialogue window state once per frame into a `PageSnapshot` (`overlay_types.h`), evaluates a stand-down predicate, and when the overlay owns the
text makes the engine's glyph draw submit nothing. In-battle dialogue boxes use a dedicated observer path (`src/text_overlay/battle_text.{h,cpp}`): a PPU presentation observer
decodes OAM glyph sprites, disables their rendering for the matched frame, and commits the snapshot at frame latch. At `RenderCopy` it hands the snapshot of the presented frame (2-frame pairing lag for cutscenes, frame-latched for battle) to the render side:
`overlay_layout` (wrap, auto-fit, placement), `overlay_raster` + `overlay_fonts` (stb_truetype glyphs, shadow/outline), `overlay_renderer` (atlas, drawing),
`fade_sampler` + `overlay_fade.h` (follows palette fades). Details: [TEXT_OVERLAY_ENGINE.md](TEXT_OVERLAY_ENGINE.md), [TEXT_OVERLAY_RENDER.md](TEXT_OVERLAY_RENDER.md).


### Audio
gbarecomp opens a mono S16 65536 Hz SDL device with a pull callback. `khcom_open_audio_device_intercept` forwards to `gbaimp::open_audio_device_processed`, which wraps the
callback: after gbarecomp fills the buffer, `process_audio_block` mixes the HD track, applies volumes and the DSP chain on SDL's audio thread. `music_hook.cpp` (called from the
function-entry hook on the emulation thread) calls `HdAudioPlayer::trigger_song` when the game starts a mapped m4a song. The original music is not muted.

### Config and UI
`kExtraItems` in `game_config.cpp` describes the menu items; `ui_get/ui_set/ui_action` callbacks read and write the library singletons and `overlay::settings()`. Every change saves
`khcom_config.ini` (sections `[Video] [Audio] [Dialogue] [HdText] [Controls] [Gameplay] [Diagnostics]`); key bindings are saved by the library in `keybinds.ini`.
Legacy keys of removed features (`hud_anchoring`, `text_mode`, `font_*`, `line_capacity`, `soft_word_wrap`) are parsed and ignored. Harness runs (`KHCOM_*`) never write the file.

## Tests and tools

`tests/test_all_modules.cpp` (+ `test_overlay_render.cpp`, `test_overlay_engine.cpp`) build into `khcom_unit_tests`; `tests/test_runtime_functions.py` checks the schema/algorithms;
`tests/verify_mp2k_audio.py` and `tests/benchmark_performance.py` are manual scripts that start the executable. Tools: `tools/capture/` (harness runners), `tools/overlay_dump.cpp`,
`tools/gen_font_data.py`, `tools/update_symbols.py`, `tools/import_decomp_symbols.py`, `tools/dialogue_text_tool.py` (offline dialogue-table extraction / re-wrap / compile to a binary blob; not used at run time).
