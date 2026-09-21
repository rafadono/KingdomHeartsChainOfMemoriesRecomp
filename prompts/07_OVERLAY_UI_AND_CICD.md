# Prompt 07: In-Game Configuration Overlay, Build Automation, and CI/CD Pipeline

## Target Objective
Unify all modernization features into a cohesive, user-friendly in-game settings overlay (`recomp-ui` or Dear ImGui), establish an automated local multi-threaded build script (`build.ps1`), and create a robust, production-ready continuous integration and release pipeline via GitHub Actions (`.github/workflows/ci.yml`).

---

## Architectural Requirements

### 1. Unified Modernization Settings Overlay
- Integrate an in-game configuration menu accessible at runtime (default hotkey: `F1` or `Home` on keyboard, `Menu` / `Start + Select` on gamepad):
  - **Display & Viewport**:
    - Aspect Ratio (Native 3:2, 16:9, 16:10, 21:9, Dynamic Ultrawide).
    - Refresh Rate Target (Native 59.73 Hz, 60 Hz, 120 Hz, 144 Hz, 240 Hz, Unlocked).
    - Frame Interpolation (Disabled, Linear, Hermite Cubic Spline).
    - V-Sync & G-Sync/FreeSync Compatibility mode.
  - **Video Filtering & Upscaling**:
    - Pixel Scale / Upscaling (Nearest Neighbor, Bilinear, xBRZ 2x, 4x, 6x).
    - Screen Filter Preset (None, GBA SP 101 LCD Grid, Trinitron CRT Aperture Grille, PVM Shadow Mask, Scanlines 25%/50%/75%).
    - Color Correction (GBA Hardware Gamma, Modern sRGB, Dynamic Saturation).
    - HUD Element Anchoring (Centered 4:3/3:2 Pillar, Dynamic Outer Corners).
  - **Audio & Sound**:
    - Master, BGM, and SFX Volume Sliders.
    - Audio Source (GBA DirectSound Hardware, DSP Stereo Enhanced, HD Orchestrated FLAC/OGG).
    - Reverb & Acoustic Spatialization toggle.
  - **Controls & Accessibility**:
    - Full Gamepad & Keyboard Rebinding UI.
    - True 360 Analog Movement toggle with Deadzone and Sensitivity calibration.
    - Fast Forward Speed (2x, 4x, 8x, Uncapped).
    - Action Assist / Auto-Mash Toggle.
  - **Dialogue & UI**:
    - Font Density (Native 100%, Modern 80%, Compact 65%).
    - Grammar-Aware Soft Word-Wrapping toggle.
    - Conversation Backlog Keybind.
  - **Diagnostics**:
    - Performance HUD (FPS, Frame Time, GBA Tick Rate, Audio Buffer Latency).
- Persistent Configuration:
  - Save all user choices to a clean, human-readable configuration file (e.g., `config.ini` or `config.json`) in the application root directory or standard OS user preferences path.

### 2. Multi-Threaded Local Build Automation (`build.ps1`)
- Create an automated PowerShell build script `build.ps1` that:
  - Automatically locates Visual Studio 2022 / Build Tools and invokes `vcvarsall.bat x64`.
  - Detects `cmake` and `ninja`.
  - Clones or initializes submodules (`git submodule update --init --recursive`) if absent.
  - Executes CMake configuration with `-G Ninja -DCMAKE_BUILD_TYPE=Release`.
  - Compiles the project using parallel threads (`ninja -j $env:NUMBER_OF_PROCESSORS`).
  - Verifies binary output and copies required runtime assets (shaders, fonts, soundbank definitions) into the build output folder.

### 3. Continuous Integration and Automated Releases (`.github/workflows/ci.yml`)
- Implement a GitHub Actions workflow with the following specifications:
  - Trigger on push to `main` and pull requests.
  - Runner environment: `windows-latest`.
  - Fetch submodules recursively:
    ```yaml
    - uses: actions/checkout@v4
      with:
        submodules: recursive
    ```
  - MSVC 2022 environment setup:
    ```yaml
    - uses: ilammy/msvc-dev-cmd@v1
    ```
  - Ninja availability: Use the preinstalled Ninja executable natively available on `windows-latest` (do not rely on obsolete or unverified third-party setup actions).
  - CMake configure & compile:
    ```powershell
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    ```
  - Release Packaging:
    - Collect the executable, compiled shaders, UI assets, and licensing documentation into a zip archive (e.g., `<ProjectName>-windows-x64.zip`).
    - Exclude proprietary GBA ROM binaries or copyrighted audio dumps to ensure full legal compliance.
    - Publish the artifact via `actions/upload-artifact@v4`.

---

## Expected Code Artifacts
1. `src/ui/settings_menu.c` / `src/ui/settings_menu.h` (In-game settings menu UI and config state persistence).
2. `build.ps1` (Comprehensive Windows compilation script).
3. `.github/workflows/ci.yml` (Robust GitHub Actions workflow).
4. `.gitignore` (Strict filtering of binaries, build outputs, and proprietary assets).
