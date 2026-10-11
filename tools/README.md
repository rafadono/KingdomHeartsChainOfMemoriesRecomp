# Tools

Run everything from the repository root. The generic recompilation helpers (control-flow tracing, coverage audit, RAM-dispatch and symbol generation) are in the
`gba-improvements` submodule: `external/gba-improvements/tools/` (see [../docs/GBA_IMPROVEMENTS_INTEGRATION.md](../docs/GBA_IMPROVEMENTS_INTEGRATION.md), "Tool invocations").

| Tool | Purpose | Usage |
|---|---|---|
| `capture/run_capture.ps1`, `capture/run_capture.sh` | Headless capture / scripted-input runs (Windows / Linux) | [../docs/CAPTURE_HARNESS.md](../docs/CAPTURE_HARNESS.md) |
| `capture/montage.py` | Contact sheets and crop/zoom of captured PNGs (stdlib only) | `python tools/capture/montage.py OUT.png [--cols N] [--crop X,Y,W,H] [--zoom K] [--gap PX] img1.png img2.png ...` |
| `capture/stroke_runs.py` | Run-length histogram of text strokes in a capture crop | `python tools/capture/stroke_runs.py IMG.png X,Y,W,H [threshold]` |
| `capture/scenarios/dialogue_study.ps1` | Boot-to-dialogue study over several configurations (stale: still uses the removed text keys, see the harness doc) | `.\tools\capture\scenarios\dialogue_study.ps1 [-Out dir] [-Parallel 4] [-Sharp] [-Only cfg,cfg]` |
| `capture/scripts/*.txt` | Input scripts (`first_dialogue.txt`, `explore_a.txt`) | used with `-Script` / `KHCOM_INPUT_SCRIPT` |
| `overlay_dump.cpp` | PNG dump of the HD text overlay without the game | build line and options in [../docs/TEXT_OVERLAY_RENDER.md](../docs/TEXT_OVERLAY_RENDER.md) ("Tests and the PNG dump") |
| `overlay_png_to_ppm.py` | Converts a capture PNG (8-bit RGB/RGBA) to the PPM background of `overlay_dump` | `python tools/overlay_png_to_ppm.py in.png out.ppm` |
| `gen_font_data.py` | Regenerates the embedded font C arrays (stdout) | `python tools/gen_font_data.py [--extra] subset1.ttf ... > file.cpp`, details in [../third_party/fonts/README.md](../third_party/fonts/README.md) |
| `update_symbols.py` | Merges a few seed symbols and `symbols/task_mode_symbols.txt` into `symbols/imported_symbols.tsv` | `python tools/update_symbols.py` |
| `import_decomp_symbols.py` | Imports function names from a matching decompilation build (`symbols/khcom_syms.txt`, `khcom_sections.txt`, `khcom.map`) through `gbarecomp/tools/symbol_import` | `python tools/import_decomp_symbols.py [path\to\rom.gba]` (default ROM `roms/B8CE.gba`), see [../symbols/README.md](../symbols/README.md) |
| `dialogue_text_tool.py` | Offline dialogue-script helper: extract tables from the ROM to JSON, re-wrap to a character width, compile back to a binary blob. Not used by the game at run time | `python tools/dialogue_text_tool.py extract --rom ROM [--out json]` / `recalculate-wrap --in json --out json [--width N]` / `compile --in json --out blob` |

Related test scripts (not tools): `tests/test_runtime_functions.py` (schema and algorithm checks), `tests/verify_mp2k_audio.py` and `tests/benchmark_performance.py`
(manual scripts that start the executable).
