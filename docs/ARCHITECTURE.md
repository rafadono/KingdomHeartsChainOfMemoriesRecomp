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

`main()` (`src/main.cpp`) configures the capture harness and fills a `gbaimp::HostMainConfig` (title, banner, the SDL-interposer and RAM-dispatch
install callbacks, the harness arguments, `khcom::create_run_options()`); `gbaimp::run_gbarecomp_host()` then installs the SDL interposers, defaults
`GBARECOMP_RAM_OVERLAY_HEAL=1`, installs the RAM-overlay dispatcher, prints the banner and calls `gbarecomp::run_game()` (through the recomp-ui launcher
when it is compiled in). `run_game()` resets every game-owned hook at start, so hooks are (re)installed from callbacks that run afterwards.

## Source tree (`src/`)

| File | Role |
|---|---|
| `main.cpp` | entry point (above): a `HostMainConfig` and `run_gbarecomp_host()` |
| `game_config.{h,cpp}` | `RunOptions` (aspect presets from `gbaimp/view_aspect.h`, assist tools), this game's `gbaimp::SettingsRegistry` (wording and ranges over the standard settings, the `hdtext.*` and `dialogue.*` rows, `khcom_config.ini`, legacy keys), the `RuntimeUiBinding` that turns it into the runtime menu, capture-harness configuration |
| `gbaimp_integration.{h,cpp}` | registers the library hooks (`save_settings`, `is_input_blocked`, `frame_counter`) and the two HUD zones; sets up `gbaimp::M4aMusicHook` (HD music trigger) |
| `sdl_intercepts.cpp` | this game's callbacks for the library's six generic SDL handlers (`PollEvent`, `UpdateTexture`, `RenderCopy`, `RenderPresent`, `QueueAudio`, `OpenAudioDevice`; `gbaimp/game_host.h`) and their registration |
| `runtime_ui_standard.{h,cpp}` | view-mode labels for the stock settings menu (the rewrite is `gbaimp/runtime_ui_glue.h`, no RESUME row) and the menu pointer |
| `runtime_ui_imgui_enhanced.cpp` | replacement of recomp-ui's ImGui menu renderer (draggable window); compiled instead of `runtime_ui_imgui.cpp` |
| `widescreen_adapter.{h,cpp}` | the game's profile of `gbaimp::Widescreen`: battle detection (mode descriptors, mode-function addresses, IWRAM mode words), the HUD-aware sprite provider, and the per-function-entry work (music trigger, battle tracking, overlay capture) |
| `dialogue_backlog.{h,cpp}` | conversation log observer: watches the guest bus reads that type cutscene dialogue and pushes `{speaker, text}` into `gbaimp::TextHistory` (panel, hotkeys, drawing and entry store are the library's) |
| `text_overlay/` | HD dialogue text (below) |
| `ram_overlay_dispatch.{h,cpp}` | generated dispatcher: native code for the routines the game runs from IWRAM/EWRAM |
| `text_fonts.h`, `text_fonts_data.cpp` | embedded PT Sans / Narrow / Serif subsets (generated, see `third_party/fonts/README.md`) |
| `cutscene_dialogue_data.inl`, `scaffold_stub.cpp`, `msvc_compat.h` | data table for the backlog, link stub when `generated/` is absent (`gbaimp/scaffold_stub.h`), MSVC forced include (`gbaimp/msvc_compat.h`) |

The headless capture harness is `gbaimp::capture` ([CAPTURE_HARNESS.md](CAPTURE_HARNESS.md)); the RAM dispatcher hook in `ram_overlay_dispatch.cpp` is one `GBAIMP_DEFINE_RAM_DISPATCH_N` line after the generated routines.

## Data flows

### SDL interposition
gbarecomp and recomp-ui call SDL directly, so the game observes those calls: Linux uses `-Wl,--wrap=SDL_X` (flags in `CMakeLists.txt`,
`GBAIMP_DEFINE_LD_WRAPS()` in `sdl_intercepts.cpp`), Windows patches the Import Address Table at startup
(`gbaimp::install_sdl_interposers`, called from `khcom_install_sdl_interposers()` first in `main()`). Handlers call SDL only through
`gbaimp::sdl_real()`.

- `PollEvent`: refreshes widescreen state, offers events to `gbaimp::TextHistory` and the perf HUD (consumed events are dropped).
- `UpdateTexture`: native-frame capture hook, then colour-profile correction of the RGB24 frame (any logical size from 240x160 up; `gbaimp::is_gba_game_texture`).
- `RenderCopy` (game image only): computes the viewport (`gbaimp::widescreen_compute_viewport`), clears the bars, copies the image,
  draws the screen mask, then the **HD text overlay** (`engine::on_present`), so the settings menu that gbarecomp draws afterwards stays above the text.
- `RenderPresent`: frame interpolation, perf HUD, conversation log sidebar (`TextHistory::render_sidebar`), harness capture, then the real present.
- `OpenAudioDevice` / `QueueAudio`: audio path (below).

### View mode policy (3:2 vs wide presets)
The user picks the view in the recomp-ui menu; gbarecomp rewrites `g_ws_active/extra_left/extra_right/view_width`. The function-entry hook
(chained by `gbaimp::Widescreen`, handler `on_fn_entry` in `widescreen_adapter.cpp`) watches mode-switch/handler addresses and sets the wide context (= in a battle); the
same flag is also polled from the IWRAM mode words. `gbaimp::Widescreen::apply_policy()` (every frame, context change and SDL poll) derives
the PPU globals from two inputs only, "view expanded?" and "in battle?": in 3:2 everything is reset to the faithful defaults; in a wide preset the providers are
installed and the margins show real arena content only in battle (black pillars otherwise, `gbaimp::MarginFill` stays on its default `Pillar` mode). The HUD zones are shifted by the real extra width per side while
the wide picture is presented. There is no cached state, so mode switches and savestate loads cannot leave stale values.

### HD dialogue text overlay
Settings: `overlay::settings()` (`hdtext_*`). Engine side `text_overlay/engine_hooks.cpp` (installed in the function-entry chain only while the overlay is
enabled): captures the dialogue window state once per frame into a `PageSnapshot` (`overlay_types.h`), evaluates a stand-down predicate, and when the overlay owns the
text makes the engine's glyph draw submit nothing. In-battle dialogue boxes use a dedicated observer path (`src/text_overlay/battle_text.{h,cpp}`): a PPU presentation observer
decodes OAM glyph sprites, disables their rendering for the matched frame, and commits the snapshot at frame latch. At `RenderCopy` it hands the snapshot of the presented frame (2-frame pairing lag for cutscenes, frame-latched for battle) to the render side:
`text_adapter` turns it into a `gbaimp::text::TextPage` and calls the library's `TextRenderer` (layout: wrap, auto-fit, placement; stb_truetype glyphs, shadow/outline; atlas and drawing),
`fade_sampler` + `gbaimp/text_fade.h` (follows palette fades). Details: [TEXT_OVERLAY_ENGINE.md](TEXT_OVERLAY_ENGINE.md), [TEXT_OVERLAY_RENDER.md](TEXT_OVERLAY_RENDER.md).


### Audio
gbarecomp opens a mono S16 65536 Hz SDL device with a pull callback. the library's `open_audio_device` handler forwards to `gbaimp::open_audio_device_processed`, which wraps the
callback: after gbarecomp fills the buffer, `process_audio_block` mixes the HD track, applies volumes and the DSP chain on SDL's audio thread. `gbaimp::M4aMusicHook` (fed from the
function-entry hook on the emulation thread) calls `HdAudioPlayer::trigger_song` when the game starts a mapped m4a song. The original music is not muted.

### Config and UI
The standard settings come from `gbaimp::register_standard_settings()` (`gbaimp/host_settings.h`); `game_config.cpp` applies this game's labels, tooltips and ranges (`kRows`), adds the `hdtext.*`
rows (backed by `overlay::settings()`), the conversation log rows (`gbaimp::register_text_history_settings`: `dialogue.backlog_enable` and `dialogue.open_backlog`, `[Dialogue]` in the ini), the font-by-name ini line and the legacy keys, and orders the menu sections. `gbaimp::RuntimeUiBinding` exposes the registry to the runtime menu. Every change saves
`khcom_config.ini` (sections `[Video] [Audio] [Dialogue] [HdText] [Controls] [Gameplay] [Diagnostics]`); key bindings are saved by the library in `keybinds.ini`.
Legacy keys of removed features (`hud_anchoring`, `text_mode`, `font_*`, `line_capacity`, `soft_word_wrap`) are parsed and ignored. Harness runs (`KHCOM_*`) never write the file.

## Tests and tools

`tests/test_all_modules.cpp` (+ `test_text_adapter.cpp`, `test_overlay_engine.cpp`) build into `khcom_unit_tests` (it links the real `game_config.cpp`, so the registry, the menu rows and the `khcom_config.ini` round trip are tested); `tests/test_runtime_functions.py` checks the schema/algorithms;
`tests/verify_mp2k_audio.py` and `tests/benchmark_performance.py` are manual scripts that start the executable. Tools: `tools/capture/` (harness runners), `tools/overlay_dump.cpp`,
`tools/dialogue_text_tool.py` (offline dialogue-table extraction / re-wrap / compile to a binary blob; not used at run time).
