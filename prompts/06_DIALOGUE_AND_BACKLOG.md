# Prompt 06: Dialogue System Modernization and Conversation Backlog

## Target Objective
Modernize in-game text rendering and dialogue presentation for a GBA statically recompiled title by implementing compact font typography options, grammar-aware soft word-wrapping (preventing mid-sentence breaks while respecting typewriter progression and event bytecode), and an interactive translucent conversation backlog sidebar.

---

## Architectural Requirements

### 1. Typography and Font Density Options
- Add configurable font scaling / horizontal density modes:
  - 100% (Native GBA spacing).
  - 80% (Compact modern readability).
  - 65% (Ultra-compact wide-buffer layout).
- Ensure character spacing adjustments scale glyph kerning and advance metrics cleanly without pixel distortion or clipping against text box bounding boxes.

### 2. Grammar-Aware Soft Word-Wrapping
- GBA games originally hardcode linebreaks (`\n` or bytecode delimiters like `0xFE`, `0xFF`) based on the 240-pixel screen width limitation.
- When widescreen viewport expansion or compact typography is enabled:
  - Intercept dialogue buffer preparation routines.
  - Implement a grammar-aware soft-wrap parser:
    - Identify sentence boundaries (period `.`, exclamation `!`, question mark `?` followed by space or capital letter).
    - If a hard break occurs mid-sentence (lowercase continuation without terminal punctuation), soft-merge the line to maximize horizontal fill.
    - If an explicit end-of-sentence or paragraph token is encountered, preserve line separation.
  - **CRITICAL**: Never bypass the typewriter printing cadence or drop synchronization with bytecode events (voice blips, screen shakes, portrait emotion swaps, delay timers). Do not dump the full text instantly unless fast-forward/skip is held.

### 3. Glassmorphic Conversation Backlog Sidebar
- Implement a backlog history system capturing all completed dialogue strings:
  - Store speaker names, dialogue text, and timestamp/sequence order in a ring buffer (e.g., last 100 dialogue exchanges).
- Provide overlay activation hotkeys:
  - Keyboard: `L` key or `F2`.
  - Gamepad: `Back` / `Select` or configurable trigger.
- Backlog Panel UX/UI Specifications:
  - Render as a slide-in sidebar on the right or left edge of the screen.
  - Follow sleek translucent glassmorphism aesthetics: dark semi-transparent background (`rgba(16, 20, 28, 0.85)`), subtle linear border gradient, and soft backdrop blur.
  - Automatically pause GBA hardware execution while the backlog is open.
  - Interactive mouse wheel and analog stick smooth scrolling.
  - Clear visual demarcation between past entries with speaker headers and high-contrast typography.
  - Dismiss immediately on pressing cancel / `B` / `Escape` / designated hotkey, cleanly resuming audio and emulation without desynchronization.

---

## Expected Code Artifacts
1. `src/dialogue_hook.c` / `src/dialogue_hook.h`:
   - Text rendering interceptor, font density scaling metrics, and grammar-aware wrap algorithm.
2. `src/backlog.c` / `src/backlog.h`:
   - Ring buffer storage, input hook for pause/resume, and UI rendering pipeline for the conversation history overlay.
3. Integration with the main configuration state for font mode selection and backlog activation.
