# Tools

Run everything from the repository root. The generic helpers (control-flow tracing, coverage audit, RAM-dispatch, symbol generation and import, font data, PNG to PPM) are in the
`gba-improvements` submodule: `external/gba-improvements/tools/` (its `README.md` lists the options) (see [../docs/GBA_IMPROVEMENTS_INTEGRATION.md](../docs/GBA_IMPROVEMENTS_INTEGRATION.md), "Tool invocations").

| Tool | Purpose | Usage |
|---|---|---|
| `capture/run_capture.ps1`, `capture/run_capture.sh` | Headless capture / scripted-input runs (Windows / Linux) | [../docs/CAPTURE_HARNESS.md](../docs/CAPTURE_HARNESS.md) |
| `capture/montage.py` | Contact sheets and crop/zoom of captured PNGs (stdlib only) | `python tools/capture/montage.py OUT.png [--cols N] [--crop X,Y,W,H] [--zoom K] [--gap PX] img1.png img2.png ...` |
| `capture/stroke_runs.py` | Run-length histogram of text strokes in a capture crop | `python tools/capture/stroke_runs.py IMG.png X,Y,W,H [threshold]` |
| `capture/scenarios/dialogue_study.ps1` | Boot-to-dialogue study over several configurations (stale: still uses the removed text keys, see the harness doc) | `.\tools\capture\scenarios\dialogue_study.ps1 [-Out dir] [-Parallel 4] [-Sharp] [-Only cfg,cfg]` |
| `capture/scripts/*.txt` | Input scripts (`first_dialogue.txt`, `explore_a.txt`) | used with `-Script` / `KHCOM_INPUT_SCRIPT` |
| `overlay_dump.cpp` | PNG dump of the HD text overlay without the game (background PPM from `external/gba-improvements/tools/png_to_ppm.py`) | build line and options in [../docs/TEXT_OVERLAY_RENDER.md](../docs/TEXT_OVERLAY_RENDER.md) ("Tests and the PNG dump") |
| `dialogue_text_tool.py` | Offline dialogue-script helper: extract tables from the ROM to JSON, re-wrap to a character width, compile back to a binary blob. Not used by the game at run time | `python tools/dialogue_text_tool.py extract --rom ROM [--out json]` / `recalculate-wrap --in json --out json [--width N]` / `compile --in json --out blob` |

Related test scripts (not tools): `tests/test_runtime_functions.py` (schema and algorithm checks), `tests/verify_mp2k_audio.py` and `tests/benchmark_performance.py`
(manual scripts that start the executable).
