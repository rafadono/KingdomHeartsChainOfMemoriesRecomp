# Technical Assessment: In-Game Dialogue Font Resizing

## Overview

This document analyzes the feasibility, technical complexity, and architectural requirements for resizing the dialogue font in *Kingdom Hearts: Chain of Memories* (GBA Recompiled).

The original game renders dialogue within a fixed-width window (typically 3 to 4 lines of text) using 8x8 or 8x12 1bpp/2bpp tile-based glyphs dynamically uploaded into GBA VRAM BG Character blocks.

---

## Technical Constraints in GBA Architecture

### 1. Hardcoded Line-Wrap and Paging Control Codes
GBA text engines do not perform dynamic word-wrapping like modern CSS or rich text layout engines. Instead, the dialogue script bytecode contains explicit control codes embedded directly within the dialogue string tables:
- `[WAIT_KEY]`: Halts printing until the user presses A.
- `[CLEAR_PAGE]`: Flushes the dialogue text box.
- `[NEWLINE]`: Advances the tile cursor by a fixed vertical offset (e.g. 16 pixels).

Reducing the glyph font size (e.g. from 8x12 to 6x8) without altering the compiled script tables would result in short lines prematurely breaking and early page clears, leaving large portions of the text box empty rather than accommodating more text.

### 2. Tilemap and VRAM Allocation
The dialogue window uses Background Layer 0 or 1 configured in standard text mode (32x32 tilemap entries). The GBA VRAM map allocates a fixed region of Character Base Blocks for glyph rendering. Resizing the font requires:
- Replacing the 1bpp/2bpp font bitmap tables.
- Adjusting the line spacing and glyph blitter logic.
- Ensuring the variable-width font (VWF) blitter routine correctly calculates horizontal kerning offsets.

---

## Required Implementation Phases for Future Work

### Phase 1: Dialogue Script Disassembly and Re-encoding
- Extract all text table pointers and message binary scripts from the ROM.
- Decompile dialogue scripts into editable text files.
- Recalculate word wrapping and remove premature `[NEWLINE]` / `[CLEAR_PAGE]` tokens.
- Recompile binary text scripts with adjusted coordinate metrics.

### Phase 2: Engine VWF Hooking
- Identify the ROM subroutines responsible for drawing text into the BG VRAM scratch buffer.
- Replace or patch the proportional font width table.
- Modify the glyph unpacking routine to render scaled glyphs (e.g., 6x8 instead of 8x12).

### Phase 3: Dialogue Box UI Scaling
- Adjust the border window sprites and nine-slice background tilemap to fit the revised text layout.
- Validate compatibility across all supported language localizations (English, Japanese, European multi-5).

---

## Recommended Solution

For wide-display presentations, Dynamic Widescreen HUD Anchoring ([src/hud_anchoring.h](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/src/hud_anchoring.h)) provides immediate screen decluttering without breaking dialogue synchronization or requiring binary script re-encoding.
