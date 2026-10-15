# gba-improvements Integration

For the overall source map see [ARCHITECTURE.md](ARCHITECTURE.md). KHCOMRecomp consumes the reusable `gba-improvements` library (C++17, namespace `gbaimp`,
headers under `include/gbaimp/`) instead of carrying its own copies of the generic
enhancement modules and recompilation helper scripts.

## What lives where

### In the library (`external/gba-improvements`)

| Module | Header |
|---|---|
| Pixel-art upscaler (Scale2x/EPX-style, **not** xBRZ; the host menu still labels it "xBRZ") | `gbaimp/pixel_art_upscaler.h` (`gbaimp/xbrz_filter.h` is a deprecated alias header, no longer used here) |
| Screen filters (color profiles, masks) | `gbaimp/screen_filters.h` |
| Frame interpolation / frame pacing | `gbaimp/frame_interpolator.h` |
| Audio DSP + audio block pipeline + SDL pull-callback wrapping | `gbaimp/audio_dsp.h`, `gbaimp/audio_pipeline.h`, `gbaimp/audio_device.h` |
| HD audio player | `gbaimp/hd_audio_player.h` |
| Dialogue turbo | `gbaimp/dialog_turbo.h` |
| Savestate thumbnails | `gbaimp/savestate_thumbnails.h` |
| Performance HUD | `gbaimp/perf_hud.h` |
| Input enhancements (gamepad profiles, analog, keyboard) | `gbaimp/input_enhancements.h` |
| HUD anchoring (configurable `HudZone`s) | `gbaimp/hud_anchoring.h` |
| Host integration points | `gbaimp/hooks.h` |
| Host start-up sequence, RAM dispatcher, MSVC compat, scaffold stub | `gbaimp/host_main.h`, `gbaimp/ram_dispatch*.h`, `gbaimp/msvc_compat.h`, `gbaimp/scaffold_stub.h` |
| Widescreen core (view policy, providers, viewport, hook chain) | `gbaimp/widescreen.h`, `gbaimp/widescreen_gbarecomp.h` |
| Settings registry, standard panel, `.ini` persistence, runtime menu binding | `gbaimp/host_settings.h`, `gbaimp/runtime_ui_settings.h` |
| Headless capture harness | `gbaimp/capture_harness.h` |
| m4a HD music trigger | `gbaimp/m4a_music_hook.h` |
| SDL interposition (Linux `--wrap` shims, Windows IAT patching) | `gbaimp/sdl_interpose.h` |
| Python tools + `verify_rom_hash` (the latter is not built by this repository) | `tools/` |

The library has no knowledge of this game. It does contain the generic, game-agnostic SDL interposition
layer (`gbaimp/sdl_interpose.h`, see "SDL intercepts" below).

### In KHCOMRecomp (`src/`)

- `game_config.*`: this game's settings registry (wording, `hdtext.*`, `dialogue.*`), runtime menu binding, `khcom_config.ini`, capture-harness configuration
- `gbaimp_integration.*`: registration of hooks and HUD zones (see below)
- `sdl_intercepts.cpp`: the game's callbacks for the library's generic SDL handlers (`gbaimp/game_host.h`) and their registration (see below)
- `runtime_ui_standard.*`: view-mode labels of the in-game settings menu and the menu pointer; the interposition itself is `gbaimp/runtime_ui_glue.h` (see below)
- HD music trigger glue (in `gbaimp_integration.*`, below), capture harness ([CAPTURE_HARNESS.md](CAPTURE_HARNESS.md)), `text_overlay/` (HD dialogue text, [TEXT_OVERLAY_ENGINE.md](TEXT_OVERLAY_ENGINE.md) / [TEXT_OVERLAY_RENDER.md](TEXT_OVERLAY_RENDER.md))
- `dialogue_backlog.*` (only the dialogue observer; the widget is `gbaimp::TextHistory`, below), `widescreen_adapter.*` (the game's profile of `gbaimp::Widescreen`), `runtime_ui_imgui_enhanced.cpp`, `ram_overlay_dispatch.*` (generated routines and one macro line), `main.cpp` (a `HostMainConfig`)
- Game-specific Python tools: `tools/dialogue_text_tool.py`, `tools/capture/*` (the symbol, font-data and PNG helpers are in the library, see "Tool invocations")

Gamepad API names are generic in the library (`pad_a/pad_b/pad_l/pad_r`,
`GamepadProfile::Triggers`). The KH UI labels, the menu keys (`input.pad_attack`, `input.pad_jump`, `input.pad_deck_l`, `input.pad_deck_r`) and the
`khcom_config.ini` keys (`pad_attack`, `pad_jump`, `pad_deck_l`, `pad_deck_r`) are unchanged for config compatibility.

## How the library is located

`CMakeLists.txt` defines the cache variable

```cmake
set(GBAIMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/gba-improvements" CACHE PATH ...)
add_subdirectory("${GBAIMP_ROOT}" gba_improvements_build EXCLUDE_FROM_ALL)
```

and links `gbaimp::gba_improvements` into `KHCOMRecomp` and `khcom_unit_tests`.
The library's own tests are not built when it is used as a subproject
(`GBAIMP_BUILD_TESTS` defaults to OFF). If the directory is missing, configuration fails with
"run git submodule update --init". To use another checkout, pass `-DGBAIMP_ROOT=<path>`.

The library's `verify_rom_hash <file> <sha1>` tool is only defined when `GBAIMP_BUILD_TOOLS` is ON; it is OFF for a subproject and this
repository does not turn it on, so the `if(TARGET verify_rom_hash)` block in `CMakeLists.txt` is inactive (and `GBAIMP_GBARECOMP_INCLUDE`,
which it still sets, is deprecated and ignored by the library). Use `Get-FileHash -Algorithm SHA1` / `sha1sum` instead, or configure with `-DGBAIMP_BUILD_TOOLS=ON`.

### Submodule

The submodule is declared in `.gitmodules` as
`external/gba-improvements` (`https://github.com/rafadono/gba-improvements.git`).
After a fresh clone: `git submodule update --init --recursive`.
If it has to be added again on a clean checkout:
`git submodule add https://github.com/rafadono/gba-improvements.git external/gba-improvements`.

## Registration code

`src/gbaimp_integration.cpp`, function `khcom::register_gbaimp_integration()`, called at the
start of `khcom::create_run_options()` (`src/game_config.cpp`). It is idempotent (zones are
cleared and re-added, so exactly two exist afterwards).

- Hooks: `save_settings` -> `save_khcom_config`; `is_input_blocked` ->
  `is_runtime_menu_open() || gbaimp::TextHistory::instance().is_open()`; `frame_counter` ->
  `gbaimp::Widescreen::instance().frame_counter()` (presented frames).
- HUD zones (reproduce the previous hard-coded behavior):
  - Health bar, top-left: `direction=-1`, coordinates `x<=90, y<=45`; OAM `x 0..85, y 0..40`, clamp `x >= -40`.
  - Card deck, bottom-right: `direction=+1`, coordinates `x>=130, y>=95`; OAM `x 135..240, y 100..160`, clamp `x <= 300`.
  - HUD anchoring is **not a user option**. The only user choice is the view mode ("3:2 (Native)" /
    "16:9 (Battles only)", chosen in gbarecomp's overlay). `gbaimp::Widescreen` (profile in `src/widescreen_adapter.cpp`) derives everything from the
    live host state (`g_ws_active`, `g_ws_extra_left/right`) and the battle flag in `apply_policy()`:
    while a battle arena is shown in the 16:9 view, the player HUD is shifted by `extra_left` and the enemy HUD by
    `extra_right` (the HUD keeps its original margin to the real screen edge) and `gbaimp::HudAnchoring` is set to
    `WidescreenAnchored` with `horizontal_offset = max(extra_left, extra_right)`; in every other situation (3:2 view,
    field, menus, cutscenes) it is `OriginalCentered` / offset 32 and the OAM provider does not move anything.
    The zones above remain registered for the library API; the game's OAM provider uses the exact extra widths.
    An old `hud_anchoring=` line in `khcom_config.ini` is still parsed and ignored (never written back).
- Library defaults that already match the old behavior and are left untouched:
  `HdAudioSettings::target_sample_rate = 65536`, thumbnails directory `saves/thumbnails`.

`tests/test_all_modules.cpp` (`test_gbaimp_integration`) checks the registered zones against a copy of the
old algorithm over a full coordinate grid.

## Returning to the original 3:2 behavior

gbarecomp rewrites `g_ws_active`, `g_ws_extra*` and `g_ws_view_width` whenever the user switches the view mode
(`apply_runtime_view_geometry()`), then calls `extended_view_frame` = `khcom_update_widescreen_state()`.
The widescreen module keeps no copy of the mode: `gbaimp::Widescreen::apply_policy()` recomputes the PPU policy globals from
(view expanded?, in battle?) on every battle-flag change, every frame and every SDL poll. In the 3:2 view all
`g_ws_*` policy globals (providers, `g_ws_authored_margin_layers`, `g_ws_pillarbox*`, `g_ws_obj_native_clip`) are
reset to the values `run_game()` gives a faithful run, the providers decline when called directly, and the
`HudAnchoring` bookkeeping is back to its defaults. `tests/test_all_modules.cpp`
(`test_view_mode_return_to_original`) drives boot, battle entry/exit, 3:2 <-> 16:9 switches during a battle and
asymmetric extras, asserting after every return to 3:2 that the state equals the boot snapshot.

Helpers for other modules (`gbaimp::Widescreen::instance()`): `view_active()` (16:9 view selected),
`presentation_active()` (16:9 view and in a battle: the widened picture is really shown) and
`effective_extra_pixels(&left, &right)` (0/0 unless the presentation is active); the game's own `khcom_is_battle_active()` polls the mode words. Text/dialogue code must
use these instead of `DialogueBoxScaler::extra_horizontal_tiles` when deciding how wide a box may be.

## SDL intercepts

All enhancements (screen filters, perf HUD, frame interpolation, audio DSP, conversation log,
widescreen viewport/letterbox) work by intercepting six SDL calls that gbarecomp / recomp-ui make:
`SDL_PollEvent`, `SDL_UpdateTexture`, `SDL_RenderCopy`, `SDL_RenderPresent`, `SDL_QueueAudio`, `SDL_OpenAudioDevice`.
There is ONE design for both platforms, implemented by the library's `gbaimp/sdl_interpose.h`:

- `gbaimp::SdlHandlers` is a table of function pointers with SDL's own signatures.
- `gbaimp::sdl_real()` returns the original SDL implementations. Handlers call SDL **only** through it
  (never through the public `SDL_X` symbol, which would re-enter the handler).
- `gbaimp::set_sdl_handlers(h)` registers the handlers; a null entry means "do not interpose".

The six handlers (game image detection, colour correction, letterbox, mask, interpolation, performance HUD, audio) are generic and live in the library (`gbaimp/game_host.h`, `gbaimp::install_game_host()`). `src/sdl_intercepts.cpp` only fills a `gbaimp::GameHostConfig` with this game's callbacks (widescreen state, RAM dispatch, conversation log, capture harness, text overlay, music hook statistics; the viewport and the presented-frame counter are the widescreen module's). They are
identical on both platforms. Only the way SDL calls reach them differs:

| | Linux / GNU ld | Windows / MSVC |
|---|---|---|
| Mechanism | link-time `-Wl,--wrap=SDL_X` (flags in `CMakeLists.txt`) | run-time Import Address Table patching |
| Entry points | `GBAIMP_DEFINE_LD_WRAPS()` (library macro, expanded once in `sdl_intercepts.cpp`) generates `__wrap_SDL_X` and fills `sdl_real()` from `__real_SDL_X` | `gbaimp::install_sdl_interposers()` rewrites the IAT slots of the executable and of every loaded module that imports the six functions from `SDL2.dll`; `sdl_real()` holds SDL2.dll's original export addresses |
| Registration | static initializer in `sdl_intercepts.cpp` | `khcom_install_sdl_interposers()`, first statement of `main()` |
| Scope | every object in the link (including the library and gbarecomp) | every module that imports the functions through its IAT / delay-load IAT |

`khcom_install_sdl_interposers()` (`extern "C"`, in `sdl_intercepts.cpp`) is idempotent. On Windows
it prints a one-line warning to stderr if nothing could be patched or some slot failed/was foreign.

## Audio path

gbarecomp opens its SDL device (`HostWindow`, `SDL_OpenAudioDevice`) as **mono signed 16-bit, 65536 Hz** with an
SDL audio *pull callback* (`gba_audio_callback`) fed by a rate-adaptive bridge. `SDL_QueueAudio` is only used
when `GBARECOMP_AUDIO_DIRECT` is set, so an `SDL_QueueAudio` intercept alone never sees audio in normal play.

The library's `open_audio_device` handler (registered on both platforms; Linux link flag
`-Wl,--wrap=SDL_OpenAudioDevice` in `CMakeLists.txt` for both executables, Windows IAT slot) forwards to
`gbaimp::open_audio_device_processed`. It replaces the callback with a trampoline that calls gbarecomp's callback
first and then runs `process_audio_block(buffer, len/2, channels, obtained_freq)` on SDL's audio thread: HD
music mix, SFX/BGM/master volume and the DSP chain (EQ presets, anti-aliasing, limiter). The rate used is the
*obtained* device rate (it can differ from 65536 Hz when SDL cannot open that rate). In direct-queue mode
(`GBARECOMP_AUDIO_DIRECT=1`) the library's `queue_audio` handler calls `gbaimp::queue_audio_processed`, which uses the
channel count and rate the device was opened with (mono, not the old stereo assumption).

Real-time safety: the trampoline does not allocate, lock or touch files. Settings changed from the in-game menu
(game thread) are atomics read by the audio thread; the DSP coefficients are owned by the audio thread; an HD
track decoded by `trigger_song` on the game thread is handed over with an atomic pointer exchange. The library
test `test_audio_threads` runs this under ThreadSanitizer (`-DGBAIMP_SANITIZE=thread`).

## Conversation log (`gbaimp::TextHistory`)

The log panel (entry store, F2 / gamepad Back hotkeys, scrolling, drawing, bitmap font, input blocking) is `gbaimp::TextHistory`
(`gbaimp/text_history.h`). The game keeps only the observer in `src/dialogue_backlog.cpp`: it watches the guest bus reads in the
cutscene dialogue range, confirms a line once its second character is read, decodes it, and calls
`TextHistory::instance().push_entry(speaker, text)`. `sdl_intercepts.cpp` forwards `handle_event` and `render_sidebar`;
`register_gbaimp_integration()` sets the only look override (the colors of Sora and Marluxia); `game_config.cpp` adds the two
settings rows with `register_text_history_settings` (same ids, `.ini` key and menu section as before).

Behavior differences from the previous in-game widget: opening the panel always lands on the newest entry; the hotkeys are F2 and the
gamepad Back button (the old header comment also named L, but the code never handled it); the scroll bar, page keys, wheel, Esc / B
and outside-click closing are unchanged.

## HD music trigger (`gbaimp::M4aMusicHook`)

`HdAudioPlayer::trigger_song` is driven from the game's own MusicPlayer2000 ("m4a") calls, through the
function-entry hook that `gbaimp::Widescreen` chains (one owner, re-installed every frame
because gbarecomp resets `g_runtime_fn_entry_hook` at each run start); `widescreen_adapter.cpp` forwards `pc` and `R0` to
`M4aMusicHook::on_function_entry()`. `hd_music_setup()` (`gbaimp_integration.cpp`) locates the five entry points in the
cartridge image by their code shape once it is mapped, with the addresses below as the fallback, and gives the hook the guest bus
reader for the song table (the unit test checks that the locator finds exactly these addresses in the USA ROM).

| Address | Function (m4a library) | Argument |
|---|---|---|
| `0x0811FE70` | `m4aSongNumStart(u16 n)` | R0 = song id |
| `0x0811FE9C` | `m4aSongNumStartOrChange(u16 n)` | R0 |
| `0x0811FEE8` | `m4aSongNumStartOrContinue(u16 n)` | R0 |
| `0x0811FF3C` | `m4aSongNumStop(u16 n)` | R0 |
| `0x0811FFA4` | `m4aMPlayAllStop()` | none (loops 26 players) |

Evidence: these are Thumb functions whose disassembly matches the SDK `m4a.c` exactly: `lsls r0,r0,#16; ... lsrs r0,r0,#13`
(= `n * 8` into `gSongTable` at `0x09D6F744`, 8-byte entries `{header, u16 player, u16 me}`), `ldrh r2,[r0,#4]` then
`*12` into `gMPlayTable` at `0x09D6F60C`, and a call to `MPlayStart` (`0x08120650`); `OrChange` compares
`mplay->song` with the header, `Stop` calls `MPlayStop` (`0x08120734`), `AllStop` loops over 26 players. The song table holds
962 valid entries. The ROM's `'Smsh'` ident literals sit in the same library (`0x0811F1C0`..`0x08121144`). At run time the hook
fires on boot (`id=0`) and on the first music start (`id=6` in this build's title/intro flow), see the log sample under "Diagnostics".

Policy: a started song that has an entry in `config/music_tracks.ini` calls `trigger_song(id)`; an unmapped song that
starts on the *same player* the HD track was started for stops it (the game replaced that music); `m4aSongNumStop` of the
HD song and `m4aMPlayAllStop` stop it; everything else (sound effects on other players) leaves it alone.

**Limitations (documented, not worked around):**

- **The original music keeps playing underneath the HD track.** The mixed mono output is all the host sees, so it
  cannot mute only the BGM. gbarecomp does have a supported way to silence native PCM
  (`gba_mod_audio_stream_set_native_gain_percent`, `src/gba/mod_audio.h`), but it needs the capability returned by
  `gba_mod_audio_stream_register_s16_mono`, which is only granted to a *committed, hash-validated mod package*
  plugin during `mod_runtime_activate_plugins` (and fails closed otherwise). KHCOMRecomp has no such package, and
  skipping the guest's `m4aSongNumStart` via a `[[mod_function_hook]]` would break game logic that polls the
  player state. Third-party code is not modified. Practical consequence: use an HD track that is meant to sit on
  top of (or replace by convention) the original, or accept the double music; a song id *without* a mapping plays
  the original only.
- Fade-outs (`m4aMPlayFadeOut`), pause/continue and tempo changes of the original are not mirrored: the HD track keeps
  looping until a stop, an unmapped replacement on the same player, or another mapped song.
- Song ids in `config/music_tracks.ini` are indices into the ROM's `gSongTable` (962 entries, many are voices/sound
  effects). The ids shipped in that file (1..17) are placeholders; verify them with `KHCOM_LOG_INTERPOSE=1`, which
  prints `m4a start id=N player=P` for the first 40 song starts.
- The WAV files named in the ini do not ship with the repo. A mapped but missing file is silent, logs nothing and is
  remembered as failed until the mapping is reloaded (restart). `trigger_song` decodes on the game thread, so the first
  start of a long track can cost a frame hitch.

### Diagnostics

Set `KHCOM_LOG_INTERPOSE=1` to print, on stderr, the install report (modules scanned, slots patched,
which function in which module) and, at process exit, how many times each handler ran:

```
[gbaimp] sdl interposers: 3 module(s) scanned, 5 slot(s) patched, 0 already, 0 foreign, 0 failed
[gbaimp]   patched KHCOMRecomp.exe -> SDL_PollEvent
...
[khcom] sdl intercept calls: SDL_PollEvent=... SDL_UpdateTexture=... SDL_RenderCopy=... SDL_RenderPresent=... SDL_QueueAudio=... SDL_OpenAudioDevice=...
[khcom] audio: devices_wrapped=1 callbacks=1550 callback_frames_processed=1587200 queue_calls=0 level rms 1218.0 -> 1301.2 peak 19421 -> 21384 (over 1587200 samples)
[khcom] m4a song hook: starts=4 hd_mapped=1 hd_stops=0
[khcom] m4a start id=6 player=0 -> HD trigger (file unavailable, silent)     (first 40 song events only)
```

The `audio:` line is the proof that the audio enhancements run on the real audio path: `callbacks` and
`callback_frames_processed` count the SDL audio-thread callbacks that went through the gbaimp trampoline,
and the `level` figures (only measured while `KHCOM_LOG_INTERPOSE=1`) are RMS/peak of the same buffers
before and after the HD mix, volume and DSP chain.

The counters are relaxed atomics and are always maintained; only the printing is gated.

### Windows limitations

- Only IAT / delay-load imports are intercepted. A module that resolves SDL via `GetProcAddress`, or one
  that statically links its own SDL, is not intercepted. (Not the case for gbarecomp / recomp-ui here.)
- Modules loaded after `install_sdl_interposers` are not patched (call it again, it is idempotent and
  re-scans). No `LoadLibrary` hook is installed on purpose (it would mean patching the imports of every module).
- Slots that do not hold SDL's own export (for example an overlay's hook) are left alone and reported as `foreign`.
- Security software that write-protects import tables could make a patch fail; this is reported, never fatal.
- The Windows path is only active when `SDL2.dll` is loaded as a DLL named `SDL2.dll` (the repo ships it in
  `third_party/sdl2`).

## In-game settings menu (`recomp_runtime_ui_create_standard`)

gbarecomp's `run_game()` builds the settings menu with recomp-ui's `recomp_runtime_ui_create_standard()`.
KHCOMRecomp needs different view-mode labels ("3:2 (Native)", "16:9 (Battles only)"), no RESUME row, and the
menu pointer (`khcom::g_active_runtime_ui`) for its open/close hotkeys.

gbarecomp (`RunOptions`, `runtime.cpp`) and recomp-ui expose no hook for this: the labels, features and view
modes are filled in inside `run_game()` and the created `RecompRuntimeUi*` is only stored in a local. So the
call itself is interposed, with the same body on both platforms (`gbaimp::create_standard_ui` in the library's
`gbaimp/runtime_ui_glue.h`, configured by `khcom::runtime_ui_glue_options()` in `src/runtime_ui_standard.cpp`):

- Linux / GNU ld: `-Wl,--wrap=recomp_runtime_ui_create_standard` and the `GBAIMP_RUNTIME_UI_WRAP` macro in `runtime_ui_standard.cpp`.
- Windows / MSVC: this function is not a DLL import (it is compiled into the executable from recomp-ui), so
  IAT patching does not apply. `CMakeLists.txt` instead compiles recomp-ui's `recomp_runtime_settings.c` with
  `-Drecomp_runtime_ui_create_standard=khcom_real_recomp_runtime_ui_create_standard` (a per-source-file
  compile definition; no third-party file is edited) and `runtime_ui_standard.cpp` defines
  `recomp_runtime_ui_create_standard` itself, forwarding to the renamed original. If recomp-ui ever moves
  that function out of `recomp_runtime_settings.c` the build fails to link instead of silently losing the labels.

A cleaner upstream fix would be a `RunOptions` field (or a callback) letting the host adjust
`RecompRuntimeUiStandardConfig` before `run_game()` creates the menu; with it both mechanisms could be removed.

## Tool invocations (run from the repository root)

```sh
ROM="roms/B8CE.gba"   # path of your ROM

python external/gba-improvements/tools/trace_control_flow.py --rom "$ROM" --root . \
    --code-limit 0x08140000 --entry-point 0x080000C0

python external/gba-improvements/tools/merge_coverage_toml.py --root . --toml config/b8ce.toml \
    --rom "$ROM" --target 3650 --prologue-scan-limit 0x140000

python external/gba-improvements/tools/audit_code_coverage.py --root . --rom "$ROM" \
    --toml config/b8ce.toml --game-name "Kingdom Hearts: Chain of Memories (USA, B8CE)"

python external/gba-improvements/tools/gen_ram_dispatch.py --root . \
    --symbol-prefix khcom --game-name KHCOMRecomp

python external/gba-improvements/tools/gen_symbols.py "$ROM" -o symbols/task_mode_symbols.txt \
    --code-size 0x122000

# symbols/imported_symbols.tsv from the seed symbols and task_mode_symbols.txt
python external/gba-improvements/tools/update_symbols.py --root . \
    --seed 0x08000000:arm:start_vector --seed 0x080000C0:arm:crt0_entry \
    --seed 0x080D7014:thumb:sound_driver_init --seed 0x080D7160:thumb:sound_driver_step \
    --seed 0x080D71D4:thumb:sound_driver_sub --seed 0x0811F578:thumb:intr_manager_step

# names from a matching decompilation build (symbols/khcom_syms.txt, khcom_sections.txt, khcom.map)
python external/gba-improvements/tools/import_decomp_symbols.py --root . --id B8CE \
    --name "Kingdom Hearts: Chain of Memories (USA)" --syms symbols/khcom_syms.txt \
    --sections symbols/khcom_sections.txt --map symbols/khcom.map --rom "$ROM"

# PNG capture frame to the PPM background of tools/overlay_dump.cpp
python external/gba-improvements/tools/png_to_ppm.py capture/frame.png frame.ppm

# embedded font data (see third_party/fonts/README.md for the exact command lines)
python external/gba-improvements/tools/gen_font_data.py --namespace khcom::textfonts --include text_fonts.h ... > src/text_fonts_data.cpp
```
