# Prompt Phase 2: High Refresh Rate Engine & Widescreen Viewport

## Prompt Objective
Implement a decoupled high refresh rate presentation engine and dynamic widescreen viewport expansion pipeline without modifying original gameplay speed or physics.

---

## Instructions to Agent / LLM

```text
Act as a real-time graphics and display systems engineer. Implement a high refresh rate presentation engine and true widescreen viewport expansion for the recompiled GBA game.

### Mandatory Rules
1. Never alter the game simulation cadence. The GBA hardware clock (59.7275 Hz) must remain locked to preserve physics, combat timings, and sound pitch.
2. The gbarecomp/ submodule must remain 100% immutable.
3. No emojis in code or documentation.

### Step-by-Step Execution Plan

1. Decoupled Presentation Engine:
   - Create src/frame_interpolator.h and src/frame_interpolator.cpp.
   - Implement framerate targets: 60 FPS (Original), 120 FPS (2x High Rate), 144 FPS, Display Native (auto-detected via SDL_GetCurrentDisplayMode), and Uncapped.
   - Decouple the display render loop from the GBA CPU thread: the simulation continues executing at 59.7275 Hz, while the presentation thread delivers frames at the target monitor refresh rate.

2. GPU Temporal Motion Smoothing:
   - Cache the previously presented frame texture.
   - Calculate the fractional sub-frame phase between the last two simulation ticks.
   - Apply GPU-accelerated temporal blending (using SDL_SetTextureAlphaMod or custom Direct3D11/OpenGL shader) to smoothly blend between simulation states, eliminating judder and stutter during camera panning.

3. Two-Tier Display Architecture:
   - Level 1: Viewport Expansion (True Aspect Ratio Adjustment):
     - Configure game.toml with resize_driven_view = true, max_resize_view_width = 576.
     - Implement presets: 3:2 (Classic 240px), 16:10 (Wide 256px), 16:9 (Standard 284px), 16:9 (High-Density 480px), 16:9 (Full Arena 576px), and Adaptive View (dynamically matching window resize).
     - Ensure horizontal field of view expands natively without stretching or distorting pixel aspect ratios.
   - Level 2: Integer Pixel Zooming:
     - Independent integer scale multipliers (1x to 8x) and exclusive fullscreen toggle (Alt + Enter).

4. Verification:
   - Compile and verify that running at 120 Hz or 144 Hz displays smooth motion without speeding up character movement or background music.
```
