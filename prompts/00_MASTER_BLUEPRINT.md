# GBA Static Recompilation & Modernization Master Blueprint

## Architecture Overview

This blueprint governs the complete static recompilation of a Game Boy Advance game into a native modern PC application using the `gbarecomp` ecosystem.

A modular, phased approach is strongly recommended over a single monolithic prompt. Dividing the process into sequential phases prevents context truncation, ensures that each foundational layer compiles and runs before dependent features are built on top, and allows isolated testing at each milestone.

---

## Phase Execution Roadmap

| Phase | Prompt File | Objective |
|---|---|---|
| **Phase 1** | [01_CORE_STATIC_RECOMPILATION.md](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/prompts/01_CORE_STATIC_RECOMPILATION.md) | ROM hash verification, symbol extraction, BIOS recompilation, and self-healing JIT integration. |
| **Phase 2** | [02_HIGH_REFRESH_AND_WIDESCREEN.md](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/prompts/02_HIGH_REFRESH_AND_WIDESCREEN.md) | High refresh rate decoupled simulation (60/120/144/Native), GPU temporal blending, and widescreen viewport expansion. |
| **Phase 3** | [03_MISTER_FILTERS_AND_XBRZ.md](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/prompts/03_MISTER_FILTERS_AND_XBRZ.md) | MiSTer FPGA hardware color profiles (AGB/AGS/Gamma), multiplicative screen masks, and xBRZ 2x-5x upscaler. |
| **Phase 4** | [04_AUDIO_DSP_AND_HD_SOUNDTRACK.md](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/prompts/04_AUDIO_DSP_AND_HD_SOUNDTRACK.md) | 65,536 Hz MP2K shadow voice mixer, DSP parametric EQ, anti-aliasing, and HD orchestrated music replacement with independent volume sliders. |
| **Phase 5** | [05_CONTROLS_ANALOG_AND_ASSIST.md](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/prompts/05_CONTROLS_ANALOG_AND_ASSIST.md) | True 360 analog movement with walk/run velocity, turbo skip, save state visual thumbnails, and draggable Performance HUD. |
| **Phase 6** | [06_DIALOGUE_AND_BACKLOG.md](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/prompts/06_DIALOGUE_AND_BACKLOG.md) | Compact proportional font spacing, grammar/case-aware soft word-wrapping, and auto-pausing conversation backlog sidebar. |
| **Phase 7** | [07_OVERLAY_UI_AND_CICD.md](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/prompts/07_OVERLAY_UI_AND_CICD.md) | In-game configuration overlay (`recomp-ui`), MSVC/Ninja build automation, and GitHub Actions CI/CD pipeline. |

---

## Universal Rules for All Prompts

1. **Submodule Immutability:** Never modify any file inside `gbarecomp/`. All customizations must reside in the game project tree (`src/`, `config/`, `symbols/`, `tools/`).
2. **Simulation Integrity:** Never accelerate or decelerate the game simulation logic (locked strictly to 59.7275 Hz). All visual frame rates above 60 FPS must be achieved through presentation decoupling and GPU interpolation.
3. **No Automatic Text Dumps:** Typewriter character cadence and in-game scene event bytecode must always remain synchronized.
4. **Style Guidelines:** No emojis anywhere in code or documentation. Minimal comments. Modern, cohesive UX/UI aesthetics.
