# KHCOMRecomp — Kingdom Hearts: Chain of Memories (GBA) Static Recompilation

Native static recompilation of **Kingdom Hearts: Chain of Memories** for Game Boy Advance to modern PC platforms, built on the `gbarecomp` framework.

---

## ROM and BIOS Requirements

### Game ROM
The official target version for this project is the **USA release**:

- **Game Title / ID:** `B8CE`
- **Region:** USA (NTSC-U, 60 Hz)
- **SHA-1 Hash:** `10729bd884f8fdca7a310b6d606c52e46657aa48`
- **Size:** 33,554,432 bytes (32 MB / 256 Mbit)
- **Save Type:** EEPROM (8 KB / 64 Kbit)
- **Location:** Place your ROM file in `roms/B8CE.gba` (or any `.gba` file inside `roms/`).

To verify your ROM file:
```powershell
.\build.ps1 -Target verify_rom_hash
.\build\Release\verify_rom_hash.exe "roms\Kingdom Hearts - Chain of Memories (U)(Venom).gba"
```

### GBA BIOS
For authentic hardware execution of BIOS SWI calls and interrupt vectors:

- **File:** `bios/gba_bios.bin` (or `bios/gba (W).gba`)
- **Size:** 16,384 bytes (16 KB)
- **SHA-1 Hash:** `300c20df6731a33952ded8c436f7f186d25d3492`

---

## In-Game Configuration Overlay

The application features a real-time in-game configuration overlay built using `recomp-ui`. Open or close the overlay at any time during gameplay without pausing or restarting:

- **Keyboard:** Press `Escape`
- **Gamepad / Controller:** Press `Guide` / `Home` / `Menu` / `Options`

All adjustments made in the overlay take effect immediately in real time and are persisted across sessions.

---

## Key Features and Enhancements

### 1. High Refresh Rate Presentation Engine
- **Target Framerates:** `60 FPS (Original)`, `120 FPS (2x High Rate)`, `144 FPS`, `Display Native (Auto)`, and `Uncapped`.
- **Decoupled Simulation:** Game simulation ticks remain locked to authentic 59.7275 Hz GBA hardware cadence, keeping audio pitch, battle card cooldowns, and physics 100% stable without game acceleration.
- **GPU Temporal Motion Smoothing:** GPU-accelerated blending between simulation frames, eliminating judder and strobing during camera scrolling and card combat.

### 2. Authentic MiSTer FPGA Screen Filters
- **Hardware Color Profiles:**
  - `Raw (Uncorrected)`: Pure digital saturated RGB.
  - `AGB-001 (Reflective TFT)`: Desaturated, gamma 1.45 curve tailored to correct GBA games designed for unlit reflective screens.
  - `AGS-001 (SP Frontlit)`: Frontlit reflective simulation with subtle cool white point.
  - `AGS-101 (SP Backlit)`: High-contrast, rich-black backlit model (gamma 2.2).
  - `MiSTer Gamma 1.6 & 2.2`: Exact mathematical gamma curves from the MiSTer GBA core.
- **Screen Masks (Direct3D11 Multiplicative Blending):**
  - `MiSTer LCD Grid`: Inactive matrix grid between TFT liquid crystals.
  - `MiSTer Subpixel RGB`: Vertical Red, Green, Blue subpixel stripes.
  - `MiSTer Subpixel BGR`: Alternate subpixel striping.
  - `MiSTer Diffusion (AGS-001)`: Frontlight light-guide diffusion pattern.
  - `Game Boy Player Scanlines`: 240p CRT television scanlines.
  - `CRT Trinitron Aperture Grille`: Vertical phosphor stripes and scanlines.
- **Mask Intensity Slider:** Live adjustment from 10% to 100% opacity.

### 3. xBRZ High-Precision Pixel Art Upscaler
- Geometric pattern-recognition edge interpolation engine.
- Preserves clean diagonal vectors and curves without bilinear blurring.
- Modes: `Off (Original Pixels)`, `2x xBRZ`, `3x xBRZ`, `4x xBRZ`, `5x xBRZ`.

### 4. True 360 Analog Movement & Velocity
- Circular deadzone (configurable, default 18%) with full 360-degree angle resolution.
- Walk/run speed modulation: Tilting the analog stick below 55% threshold cuts movement speed by 50% for realistic character walking, while full tilt engages running.
- Modes: `Digital (Original 8-Way)`, `Analog 8-Way (Enhanced)`, and `True 360 Walk & Run`.

### 5. HD Orchestrated Soundtrack & Independent Mixer
- Replaces compressed GBA chiptune tracks with high-fidelity orchestral arrangements from PlayStation 2 *Re:Chain of Memories*.
- Configurable track mapping via [config/music_tracks.ini](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/config/music_tracks.ini).
- Seamless loop playback and automatic resampling to engine 65,536 Hz 16-bit stereo.
- **Independent Volume Controls:** Separate sliders for BGM (Music) and SFX (Effects / Combat Audio) from 0% to 150%.

### 6. Compact Dialogue & Grammar-Aware Soft Word-Wrapping
- **Font Density:** Adjust glyph advance width between `Original (100%)`, `Compact (80%)`, and `High-Density (65%)` to fit more text per line.
- **Grammar & Case-Aware Wrapping:** Analyzes surrounding punctuation and capitalization to merge artificial mid-sentence `\n` breaks into smooth, readable lines without premature wrapping.
- **Preserved Typewriter Cadence:** Dialogue progresses character-by-character along with original scene bytecode events (mugshots/portraits, pauses, sound effects). No artificial full text dump.

### 7. Conversation Log Sidebar (Backlog)
- Press **`L`** or **`F2`** on keyboard, or Gamepad **`Back` / `Select`** to toggle the story backlog sidebar.
- **Automatic Game Pause:** Simulation and audio pause while the backlog is open, allowing the player to review dialogue without missing story progression.
- **Left-Docked Glassmorphic Panel:** Translucent dark sidebar displaying chronological dialogue entries with speaker badges and timestamps.
- **Smooth Navigation:** Scroll up and down using the mouse wheel, keyboard arrow keys, or gamepad D-Pad.
- **Closing:** Press `Escape`, `L`, or Gamepad `B` / `Circle` to resume gameplay.

### 8. Dynamic Widescreen HUD Anchoring
- Repositions OAM sprite coordinates in widescreen viewports.
- Shifts the Health Gauge to the top-left screen corner (`X -= offset`).
- Shifts the Card Deck and card selector to the bottom-right corner (`X += offset`).
- Frees the central screen area for unobstructed combat visibility.

### 9. Performance HUD & Frametime Graph
- Real-time FPS counter with 60 FPS target indicator (`[LOCKED]` vs `[VAR]`).
- Precision frame duration in milliseconds and rolling average.
- 120-frame rolling pacing history graph with 16.67 ms (60 FPS) target line.
- Dynamic color coding (Emerald Green for 60 FPS lock, Amber Yellow for slight variance, Rose Red for frame drops).
- Movable positioning: Draggable with mouse anywhere on screen, including docking into the black letterbox / pillarbox bars.
- Hotkey **`F10`** to cycle HUD display modes.

### 10. Two-Tier Display Pipeline
- **Viewport Expansion (True Aspect Ratio Adjustment):**
  - `3:2 (Classic 240px)`: Authentic GBA frame.
  - `16:10 (Wide 256px)`: Extended field of view for 16:10 monitors and Steam Deck.
  - `16:9 (Standard 284px)`: Clean widescreen layout.
  - `16:9 (High-Density 480px)`: High-density viewport showing doubled horizontal arena.
  - `16:9 (Full Arena 576px)`: Complete battle arena horizontal visibility.
  - `Adaptive View`: Dynamically expands the GBA PPU rasterizer width to match arbitrary window resizing.
- **Pixel Zoom and Window Scaling:**
  Independent nearest-neighbor / integer scaling multiplier (1x to 8x, and Fullscreen toggle via `Alt + Enter`).

### 11. High-Fidelity Audio DSP Suite
- **MP2K Shadow Mixer:** High-sample-rate shadow voice mixer eliminating GBA hardware audio quantization noise.
- **DAC Anti-Aliasing Filter:** Biquad low-pass filter targeting ultrasonic PWM/DAC switching hiss.
- **Parametric Equalizer Profiles:** *Flat (Authentic)*, *Warm Retro*, *Crisp Modern*, *Bass Boost*.
- **Stereo Width Expansion:** Adjustable from 0% (mono) to 200% (expanded stereo).
- **Soft-Knee Peak Limiter:** Prevents digital clipping on multi-card sleights.

### 12. Assist Tools & Save State Thumbnails
- **Save States & Load States:** 10 independent slots with visual thumbnail capture (`saves/thumbnails/slot_X.bmp`) and timestamps.
- **Fast-Forward:** Uncaps framerate with a customizable multiplier from **2x to 10x** (default: 4x).
- **Rewind:** Real-time rewind buffer capturing up to 60 seconds of continuous gameplay.
- **Turbo Dialog & Skip:** Hold **`Tab`** or Gamepad **`Y` / `Triangle`** to auto-advance dialogue boxes at 60Hz.

---

## Default Controls

Key mappings can be configured in [keybinds.ini](file:///c:/Users/RafaelInostroza/Desktop/KHCOMR/keybinds.ini) or via the in-game overlay:

| Function | Keyboard | Gamepad (Xbox / PlayStation / Switch) |
|---|---|---|
| D-Pad / Movement | Arrow Keys | D-Pad / Left Analog Stick (True 360 Walk & Run) |
| A | X | A / Cross / B |
| B | Z | B / Circle / A |
| L | C | Left Bumper (LB / L1 / L) |
| R | V | Right Bumper (RB / R1 / R) |
| Start | Enter (Return) | Start / Options / + |
| Select | Right Shift | Back / Share / - |
| Conversation Backlog | L / F2 | Back / Select / Touchpad |
| Turbo Dialog Skip | Tab | Y / Triangle / X |
| Fast-Forward | 2 | Right Trigger (RT / R2 / ZR) |
| Rewind | 1 | Left Trigger (LT / L2 / ZL) |
| Performance HUD | F10 | Via In-game Overlay |
| Overlay Menu | Escape | Guide / Home / Menu |

### Save / Load Hotkeys
- **Save State (Slots 1-9):** `Shift + F1` to `Shift + F9`
- **Load State (Slots 1-9):** `F1` to `F9`
- **Performance HUD Cycle:** `F10`
- **Conversation Backlog:** `L` or `F2`
- **Fullscreen:** `Alt + Enter`

---

## Build Workflow

### 1. Static Recompilation
Generates native C++ sources from the ARM/Thumb instructions in the ROM:
```powershell
.\build.ps1 -Recompile
```

### 2. Build the Game Executable
Compiles the generated C++ shards and links them with the runtime, overlay, enhancement modules, and SDL2:
```powershell
.\build.ps1 -Target KHCOMRecomp -Config Release
```
The resulting binary is generated at `build\Release\KHCOMRecomp.exe`.

### 3. Execution
```powershell
.\build\Release\KHCOMRecomp.exe
```
