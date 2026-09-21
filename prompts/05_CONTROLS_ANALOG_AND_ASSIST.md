# Prompt Phase 5: Controls, Analog Movement & Performance HUD

## Prompt Objective
Implement true 360 analog movement with walk/run velocity modulation, turbo dialogue skip, save state visual thumbnails, and an interactive draggable Performance HUD.

---

## Instructions to Agent / LLM

```text
Act as a gameplay systems and input engineer. Implement advanced controller enhancements, assist tools, and performance telemetry for the recompiled GBA game.

### Mandatory Rules
1. Submodule gbarecomp/ must remain strictly untouched.
2. GBA KEYINPUT registers are active-low (0 = pressed, 1 = released). Handle bitwise operations accordingly.
3. No emojis in code or documentation.

### Step-by-Step Execution Plan

1. True 360 Analog Movement & Velocity Modulation:
   - Create src/input_enhancements.h and src/input_enhancements.cpp.
   - Implement normalized vector coordinates with configurable circular deadzone (default 18%).
   - Map 360-degree stick angles into 8-directional or smooth sector inputs.
   - Implement duty-cycle velocity modulation: tilting the stick below a threshold (<55%) drops movement on alternate frames (cutting speed by 50% for natural walking), while full tilt maintains 100% running speed.
   - Modes: Digital Original (8-Way), Analog 8-Way (Smooth), and True 360 Walk & Run.

2. Turbo Dialog & Cutscene Skip:
   - Create src/dialog_turbo.h and src/dialog_turbo.cpp.
   - Monitor holding of a dedicated button (Tab key or Gamepad Y / Triangle).
   - Pulse Button A and Button B bits at 60Hz on alternate frames to rapidly advance dialogue boxes and cutscenes.

3. Save State Visual Thumbnails:
   - Create src/savestate_thumbnails.h and src/savestate_thumbnails.cpp.
   - On each save state operation, capture the active 240x160 viewport into a bitmap image saved in saves/thumbnails/slot_X.bmp.
   - Store timestamps and provide slot metadata queries for visual save state selection.

4. Performance HUD & Real-Time Frametime Graph:
   - Create src/perf_hud.h and src/perf_hud.cpp.
   - Render directly to the window surface before present:
     - Real-time FPS counter with 60 FPS target indicator ([LOCKED] vs [VAR]).
     - Precise frame delivery duration in milliseconds (ms) and rolling average.
     - 120-frame rolling pacing history graph with 16.67 ms target line.
     - Dynamic color-coding: Emerald Green (locked 60 FPS), Amber Yellow (minor jitter), Rose Red (drops).
     - Movable & Black-Bar Docking: freely draggable with the mouse, with presets to dock directly into letterbox/pillarbox black bars without obscuring the game.
     - Hotkey F10 to cycle HUD display modes.
```
