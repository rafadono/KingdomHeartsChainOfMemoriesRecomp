# Project Rules (Kingdom Hearts Chain of Memories Recomp)

The project must compile and run on **Linux** (built in a Docker container) and on **Windows** (MSVC). The two platforms use separate build directories so they do not overwrite each other's CMake cache: `build/` for Linux/Docker and `build-vs/` for Windows.

## 1. Mandatory Binary Update (`KHCOMRecomp`)

Whenever any change, fix or addition is made to the source code (C/C++ in `src/`, `generated/`, headers, `CMakeLists.txt`, etc.), or to the `external/gba-improvements` submodule:

### Build image (once)

The `khcom-builder` image is built from the repository's `Dockerfile`:
```bash
docker build -t khcom-builder docker/
```

In Git Bash on Windows, export `MSYS_NO_PATHCONV=1` before using `docker run` with mounted paths. In the examples, `<repo>` is the repository root path (for example `C:\path\to\KHR`).

### Linux (Docker container)

1. **Rebuild:**
   ```bash
   docker run --rm -v "<repo>:/src" -w /src khcom-builder bash -c "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release"
   ```
2. **Root binary:** the post-build step automatically copies `build/KHCOMRecomp` to `./KHCOMRecomp`. Verify that it ended up identical (`cmp build/KHCOMRecomp ./KHCOMRecomp`).
3. **Tests:**
   ```bash
   docker run --rm -v "<repo>:/src" -w /src khcom-builder bash -c "./build/khcom_unit_tests; python3 tests/test_runtime_functions.py"
   ```
   `tests/test_runtime_functions.py` rewrites `keybinds.ini` (line endings); afterwards run `git checkout -- keybinds.ini`.

### Windows (MSVC)

1. **Rebuild:** from PowerShell, at the repository root:
   ```powershell
   .\build.ps1 -BuildDir build-vs
   ```
   This produces `KHCOMRecomp.exe` and `SDL2.dll` in the repository root (both ignored by git). The script exits with the exit code of the command that fails (configure, build or recompile), so a build error is detected through the exit code. To also build the test executable: `.\build.ps1 -BuildDir build-vs -Target khcom_unit_tests`.
2. **Tests:** from the repository root, `.\build-vs\Release\khcom_unit_tests.exe` (with `SDL2.dll` reachable, which the build already copies next to the executable).
3. **Headless smoke run (optional):** with `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy`, passing the paths in quotes: `.\KHCOMRecomp.exe --rom "roms\<file>.gba" --bios "bios\gba (W).gba"`. With `KHCOM_LOG_INTERPOSE=1` it prints the status of the SDL interceptions.

### Which platforms to verify

- Changes to the game's C++, to `CMakeLists.txt`, to the SDL interceptions or to the submodule: build and test **both** platforms.
- Documentation-only changes, or data changes that are not compiled: no rebuild needed.

### Completion rule

Never report a code task as finished without having built, updated the root binary (`./KHCOMRecomp`, and `KHCOMRecomp.exe` when building on Windows) and run the tests of the affected platforms. If a platform could not be built or tested, say so explicitly.

## 2. The `gba-improvements` library (submodule)

- It lives in `external/gba-improvements` as a git submodule and holds the reusable enhancements (filters, OSD, controls, audio, SDL interception). It must not contain references to this game.
- Library changes are made in its own repository: they are tested, committed and pushed there, and only then is the submodule pointer in this repository updated. A commit of this repository with the old pointer, or without the updated pointer, breaks the build of a clean checkout.
- Everything game-specific (HUD zones, hooks, widescreen, dialogue, music) lives in `src/`; the library is configured from `src/gbaimp_integration.cpp`.
- The build (executable, test executable, link options, post-build copies) is defined by `gbaimp_add_host()` from `external/gba-improvements/cmake/GbaimpHost.cmake`; `build.ps1`, `build.sh`, `docker/Dockerfile` and `.github/workflows/ci.yml` come from the templates in `external/gba-improvements/templates/host/` (with this repository's own `overlay_toolchain` copy and Node.js step).
- See `docs/GBA_IMPROVEMENTS_INTEGRATION.md`.

## 3. Documentation

- Code map and data flows: `docs/ARCHITECTURE.md`. Library integration: `docs/GBA_IMPROVEMENTS_INTEGRATION.md`. HD text overlay: `docs/TEXT_OVERLAY_ENGINE.md` and `docs/TEXT_OVERLAY_RENDER.md`. Headless capture: `docs/CAPTURE_HARNESS.md`.
- When changing settings (keys of `khcom_config.ini`, the menu), `KHCOM_*` environment variables, tools in `tools/` or the build flow, update `README.md` and the corresponding document in the same change. Do not document removed features as active (the old enhanced text inside the engine; the `hud_anchoring` option).
- The `external/gba-improvements` library must stay game-agnostic: its documentation (in English) does not mention any specific game.
- CI (`.github/workflows/ci.yml`) builds both platforms and runs only `tests/test_runtime_functions.py`; `khcom_unit_tests` is run locally as described in section 1.
