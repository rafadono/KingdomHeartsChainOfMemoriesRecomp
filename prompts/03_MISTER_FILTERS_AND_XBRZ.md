# Prompt Phase 3: Screen Filters, xBRZ Upscaling & HUD Anchoring

## Prompt Objective
Implement a suite of authentic hardware display filters (based on MiSTer FPGA GBA core research), an xBRZ geometric pixel art upscaler, and dynamic widescreen HUD repositioning.

---

## Instructions to Agent / LLM

```text
Act as a retro hardware shader and image-processing specialist. Implement authentic MiSTer FPGA screen filters, xBRZ pixel art scaling, and widescreen HUD repositioning for the recompiled GBA game.

### Mandatory Rules
1. Submodule gbarecomp/ must remain strictly untouched.
2. No emojis in code or documentation.
3. Keep comments concise and minimal.

### Step-by-Step Execution Plan

1. MiSTer FPGA Hardware Color Profiles:
   - Create src/screen_filters.h and src/screen_filters.cpp.
   - Implement precomputed 256-entry lookup tables (LUTs) for real-time color grading:
     - Raw (Uncorrected digital RGB).
     - AGB-001 (Reflective TFT): Desaturates high-key colors and corrects gamma (1.45 curve) designed for original unlit GBA displays.
     - AGS-001 (Frontlit): Subtle elevated black floor with cool white point simulation.
     - AGS-101 (Backlit): High contrast, deep blacks, rich saturation (gamma 2.2 curve).
     - MiSTer Gamma 1.6 & 2.2: Authentic mathematical transfer functions from MiSTer FPGA.

2. Direct3D11/OpenGL Multiplicative Screen Masks:
   - Implement tileable mask textures rendered over the viewport using multiplicative blending (SDL_BLENDMODE_MOD):
     - MiSTer LCD Grid: Inactive matrix lines between TFT liquid crystals.
     - MiSTer Subpixel RGB & BGR: Authentic vertical subpixel stripe matrices.
     - MiSTer Diffusion: Simulates the light-guide diffusion layer of frontlit screens.
     - Game Boy Player Scanlines: Authentic 240p CRT television scanlines.
     - CRT Trinitron Aperture Grille: Phosphor stripes with horizontal scanline dampening.
   - Provide an intensity slider adjustable from 10% (subtle) to 100% (pronounced).

3. xBRZ High-Precision Geometric Upscaler:
   - Create src/xbrz_filter.h and src/xbrz_filter.cpp.
   - Implement pattern recognition edge detection algorithms for 2x, 3x, 4x, and 5x factors.
   - Interpolate diagonal contours and sharp vectors cleanly without blur, maintaining crisp pixel clarity on 1440p and 4K displays.

4. Dynamic Widescreen HUD Anchoring:
   - Create src/hud_anchoring.h and src/hud_anchoring.cpp.
   - Intercept OAM sprite attributes during rendering:
     - Shift top-left HUD elements (health bars, character gauges) to the far left margin in widescreen modes (X -= offset).
     - Shift bottom-right HUD elements (deck, cards, ammo, minimaps) to the far right margin (X += offset).
     - Liberate the central screen area for unobstructed gameplay and combat visibility.
```
