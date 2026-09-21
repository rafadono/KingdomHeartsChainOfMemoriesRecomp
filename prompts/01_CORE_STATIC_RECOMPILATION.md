# Prompt Phase 1: Core Static Recompilation & Engine Foundation

## Prompt Objective
Establish the core static recompilation pipeline for the target Game Boy Advance game using `gbarecomp`. At the end of this phase, all ARM and Thumb instructions from the ROM and GBA BIOS must be statically compiled into native C++ translation units, with zero runtime interpreter fallback.

---

## Instructions to Agent / LLM

```text
Act as a low-level compiler and reverse-engineering specialist. Your task is to set up the foundational static recompilation pipeline for [GAME_NAME] (Game ID: [GAME_ID], SHA-1: [ROM_SHA1]) using the gbarecomp framework.

### Mandatory Rules
1. The gbarecomp/ directory is an external Git submodule and MUST remain completely untouched. All game-specific code, headers, symbols, and configs must reside in src/, config/, symbols/, or tools/.
2. Do not use emojis in any file or documentation.
3. Keep comments concise and minimal.

### Step-by-Step Execution Plan

1. ROM Verification Tool:
   - Create tools/verify_rom_hash/main.cpp using gbarecomp_gba to validate the target ROM SHA-1 hash against the official clean No-Intro dump.

2. Symbol Ingestion & Address Mapping:
   - Create symbols/imported_symbols.tsv mapping known function entry points, jump tables, and labels.
   - Configure config/[GAME_ID].toml defining the entry point (typically 0x08000000 or 0x080000C0), load address, and known ARM/Thumb mode switches.

3. Static BIOS Recompilation:
   - Recompile the official GBA BIOS dump (bios/gba_bios.bin) statically using gba_recompile.exe --bios into generated_bios/.
   - Statically compile and link the resulting C++ dispatch tables and function shards into the project executable. This eliminates performance bottlenecks by preventing SWI calls (CpuFastSet, LZ77UnComp) from bridging to the software interpreter.

4. Dynamic Self-Healing Toolchain:
   - Stage a lightweight C compiler (TinyCC / TCC) into overlay_toolchain/tcc/.
   - Configure the runtime self-healing hooks to compile and cache dynamic RAM code executed in EWRAM/IWRAM on the fly, ensuring 0 interpreted instructions during gameplay.

5. Build Script & Native Binary:
   - Create build.ps1 to automate tool discovery (MSVC x64, CMake, Ninja), SDL2 staging, and compilation.
   - Compile the base executable and verify that it boots into the game window at 60 FPS with Direct3D11/OpenGL presentation.
```
