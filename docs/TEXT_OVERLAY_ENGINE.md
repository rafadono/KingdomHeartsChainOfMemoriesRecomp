# Dialogue text overlay - ENGINE side (capture, stand-down, hide, present wiring)

Code: `src/text_overlay/engine_hooks.{h,cpp}` (this side), contract `src/text_overlay/overlay_types.h`,
render side `src/text_overlay/overlay_renderer.cpp` (see [TEXT_OVERLAY_RENDER.md](TEXT_OVERLAY_RENDER.md)),
tests `tests/test_overlay_engine.cpp`. (The original design/analysis notes are not part of the repository.) Confidence tags: H =
verified in a real run/capture, M = consistent but not directly observed.

## What it does

1. **Capture** (function-entry hook, only while the overlay is on). Three watched addresses, everything else is one
   compare-and-return:
   * `0x08064EF4` layout(x0, y0, text*, &continuation): R0/R1 = origin in 8.8 fixed point (72 px = 0x4800, NOT plain px),
     R2 = page start pointer, R3 = &W+0x40 (so W = R3-0x40). Stored: page start, origin, W, `page_id++`.
   * `0x08073238` window update (R0 = W), once per emulated frame while a dialogue window exists: reads W (+0x20 placement,
     +0x24 revealed, +0x26 total, +0x27 page index, +0x28 type, +0x2B wait state, +0x3C page table, +0x40 continuation), the
     page entry kind, the page words `[start, continuation)` (to the 0 terminator when continuation == 0), the glyph pool
     (pointer at EWRAM `0x02034A84`, 128 x 24 bytes; count byte `0x02034A90`) and OBJ palette bank 0 colours; builds the
     `PageSnapshot`, evaluates the predicate once, and keeps the decision as "pending".
   * `0x080658B8` draw(count): if the decision is "overlay", the caller is the window update (LR == `0x0807325B`) and
     R4 == the window we decided for, then `R0 := 0` (nothing else is touched) and the snapshot is committed into a ring of
     8 frames keyed by the vblank counter. Any other caller, or another window object, is never modified.
2. **Predicate** (`engine::evaluate`, pure, unit tested; ONE evaluation per frame, so hide and draw cannot disagree). Overlay
   iff: mode On; not battle (`khcom_is_battle_active`); a game viewport is known; page kind != 0x3E; the layout call of THIS
   window was captured (otherwise: hook installed mid-page / savestate load -> stand down until the next page start);
   start pointer inside the cutscene tables `[0x08FBD378, 0x08FFE504)`; range readable and <= 400 words; all words in
   {0x20..0xFF, 0x0A, 0xE000, 0x4079, 0x4000}; `total == drawable words == pool count byte`, `revealed <= total`; every pool
   entry in use and its glyph (ROM source pointer of the tile allocation -> `(src-0x090CBFB2)/128`) equals the word's glyph
   (spaces: no tile); layout origin == first pool entry == one of the 4 placements (x in {16,72}, y in {12,108}); and finally
   `OverlayRenderer::can_draw()` (render side may refuse: fonts, fit). Otherwise the engine draws normally. Reasons are
   counted in `engine::stats()` and logged (below). Discontinuities: if the PPU frame counter (restored by a savestate load)
   stops advancing in step with the vblank counter (not restored) or the vblank counter regresses, all captured state is
   dropped.
3. **Authentic mode / inertness.** With `overlay::settings().enabled == false` (and no debug env) the hook chain is the
   original one: `widescreen_adapter.cpp` keeps TWO chain links, `khcom_fn_entry_hook` (original) and
   `khcom_fn_entry_hook_overlay` (original + `engine::on_fn_entry`), and `install_runtime_hooks()` (run every frame/poll) swaps
   between them; nothing of this module runs per function entry or per frame when off (present path: one branch). Whether the
   overlay is on or off, the game lays out and draws its authentic text: there is no in-engine text enhancement any more (the
   old FontResizer / DialogueEnhancer ROM-read overrides were removed). Verified: authentic captures (native + output
   852x480, frames 8985/9000/9110) are byte-identical between the pre-change build and this build (H).
4. **Present** (`khcom_render_copy_intercept` in `src/sdl_intercepts.cpp`, right after the game image copy and the screen mask, so the settings menu that gbarecomp draws afterwards stays above the text; the perf HUD and backlog are drawn later at present): `engine::on_present(renderer, dst)` takes the snapshot of tag
   `V - lag` (V = `g_runtime_vblank_starts`), **lag = 2** (calibrated, below), and calls
   `OverlayRenderer::draw(snapshot, settings(), DrawInfo{renderer, dst of the game image})`.
   `global_alpha` is left at 1 (fade: see open items).

## Calibration findings

Evidence references such as `captures/...`, `qa_fade/...` and `eng_tmp/...` point at local scratch captures and helper scripts of the original investigation; they are not stored in this repository.

| Item | Finding | Evidence | Conf. |
|---|---|---|---|
| Origin args | layout R0/R1 are 8.8 fixed point | log `x0=18432 y0=27648` = 72/108 px; pool entry 0 == (72,108) | H |
| Placements | W+0x20: 0 -> (16,12), 1 -> (16,108), 2 -> (72,12), 3 -> (72,108); y is the FIRST line's pen y (line i = y0+12 i) | all four seen in logs (types 0,1,2 / kinds 0x00,03,06,18,19) | H |
| Fixed point | `px = (int32)(v<<8)>>16`, kept 1/256 in `gx/gy` | decode matches pool/origin | H |
| Pairing lag | presented frame P shows the glyph set of the window update made at vblank-count P-2. Typewriter steps (1 glyph / 2 frames) in the log vs. native captures: log rev=5 ('s') at P=8985, first 's' pixels visible in native 8987; 'Y' (rev 1, log 8977) visible 8979; box open/close frames: text presence of authentic run vs overlay output identical frame by frame over 8976-8992, 9000, 9100-9125 | `captures/l3` (+`eng_tmp/lag2.py`), `captures/a2` vs `captures/o2` (`eng_tmp/cmp.py`: 0 mismatches except box-reopen residue) | H |
| Hide | `R0:=0` at 0x080658B8 removes ALL engine glyph pixels, box/portrait/arrow intact | `captures/o2/*_native_*` (box only), montage `eng_tmp/m5.png`, `m3.png`, `m4.png` | H |
| Text colour | body = OBJ palette bank 0 entry 1 = 0x008C (99,33,0); shadow = bank 0 entry 2 = 0x1E7A (208,152,56) (same box-orange tone, drawn E/S/SE by the ROM rule). Bank 0 in every page type seen (0,1,2; kinds 0,3,6,18,19) | `KHCOM_OVERLAY_DUMP` log, `captures/d1`, `captures/o4/run.log.err`; authentic native sample (99,33,0) | H |
| Colour flag 0x4079 / alt resource | no page of the intro/tutorial run used it (entry+16 resource was 0); semantics not observed | - | L (unverified) |
| Box bounds | bottom placements: box x ~ x0-11 .. x0+164, y ~ 100..152 GBA px (portrait overlaps the far side). Top placements not measured (box fields stay 0 there) | native frames 9110 (Goofy, x0=16: x 6..) and 13600 (Sora, x0=72: x 61..234) | M |
| Name plate | no separate plate on any captured page: speaker = portrait only (not glyph text) | all captures | M |
| Wait-key arrow | engine sprite, keeps showing with the overlay (visible in overlay outputs `captures/o2/f009100*`, `captures/o3/f013600*`). `page_complete = revealed >= total && W+0x2B != 0` (W+0x2B becomes 1 when typing finished; log `st=1`) | logs + captures | H |
| Box open/close | window update starts ~30 frames after the layout call (box opening animation), glyph 1 at +30, then 1 glyph / 2 frames (entry delay). At close W+0x2B returns to 0 and the pool is already released: predicate -> `pool-mismatch`, engine owns those ~10 frames (authentic behaviour) | logs | H |
| Typewriter skip | pressing A completes the page at once (`rev 22 -> 31` in one frame); the overlay follows the counter (reveal = prefix) | log of "Why, Jiminy?" page | H |
| `0xE000` pause mark | normal drawable word (pool entry with glyph 25 tile); present as the last glyph of "Why, Jiminy? Don't tell me that<E000>". The stub draws "..."; no extra waiting observed | logs | M |
| Hiding cost | draw call is skipped -> fewer guest cycles for that frame; no guest memory change. Savestates unaffected | design + runs | M |
| 16:9 / battle | the story path stands down while `khcom_is_battle_active()`; battle boxes have their own path (section "Battle dialogue"). Battle tutorial boxes ("B Button: Jump...", coloured words) never enter the 0x08073238 path (no log lines) and stay authentic | `captures/o4/f026000` (engine text, cyan words) | H for tutorial, L for battle dialogue |
| Menus / cards / subtitles | no menu text ever reached the hooks in the runs; page kind 0x3E stands down; card HUD intact in battle captures. Pause menu / card screen / movie subtitle frames could NOT be reached with the scripts (START in the castle hall did not open the pause menu) -> **not directly verified** | `captures/m1`, `captures/o4` | L |
| Fade | investigated and implemented, see "Screen fades" below (palette fades are followed; BLDCNT does not apply to the glyph OBJs in this runtime) | `qa_fade/pf_*`, `qa_fade/fd_*` | H |

## Contract additions (source compatible)

`PageSnapshot::engine_text_rgba`, `engine_shadow_rgba` (0xRRGGBBAA sampled from OBJ palette bank 0 entries 1/2, 0 = unknown).
Box fields `box_x/y/w/h` are filled for bottom placements only. Nothing renamed or removed.

Fade additions: `PageSnapshot::fade_mode` (0 none, 1 alpha, 2 brighten, 3 darken; `PageSnapshot::fade` = strength 0..1),
`DrawInfo::fade_mode`, `DrawInfo::fade_level`, `DrawInfo::fade_from_palette` (the render side tints its colours, alpha-blend
fades use `global_alpha = 1 - fade`), `OverlaySettings::text_rgba == 0` / `shadow_rgba == 0` = "match the game's own colour".
New files: `overlay_fade.h` (pure maths + `kPpuBlendsNormalObj`), `fade_sampler.{h,cpp}` (per-scanline BLDCNT/BLDALPHA/BLDY table).

## Screen fades

**How this game fades.** Logged per present over the first ~30000 frames of a new game (`KHCOM_FADE_LOG=1`, see test switches):

* Scene transitions (fade to black between the castle title card and the hall, fade to/from white, the fade-out after the intro
  dialogues) are **palette fades**: the engine rewrites palette RAM every frame (BG, OBJ and backdrop; backdrop `0000 -> 7FFF`;
  OBJ palette bank 0 entries 1/2, i.e. the engine text body/shadow, `008C/1E7A -> 0000/0000` or `-> 7FFF/7FFF` over ~12 frames)
  while BLDCNT stays 0. The mean luminance of the latched frame goes 104 -> 0 -> 209 in ~30 frames (`qa_fade/auth_s4`, sheets
  `c_auth_s6.png`, `c_auth_s7.png`, `c_auth_s8.png`, `c_auth_s9.png`).
* BLDCNT / BLDALPHA are used for alpha effects on sprites and logos: the logos (`3F5F`, EVA 0..16), a hooded figure fading out
  (`0F40`, BLDALPHA `0010 -> 0A06`, semi-transparent OBJ), `0F10`; BLDY is non-zero exactly once (`3FBF/0010/01`, logo).
* In ~10000 frames of castle dialogue surveyed, **no dialogue box was on screen while a fade ran** (the engine closes the box
  before it fades; the page counters keep their last value, so "text visible" cannot be derived from the counters alone: the fade
  log prints `txt=` for that reason). The overlay draws nothing in those frames (`qa_fade/ov_s6`).

**What the PPU does to the engine text (measured).** `KHCOM_FORCE_BLD=BF:6` / `FF:12` forces BLDCNT = `0x00BF` (brighten, all
layers incl. OBJ) / `0x00FF` (darken) and BLDY on every dialogue frame: box and scene fade exactly, but the engine glyphs keep
their exact output colours (`qa_fade/fd_auth_*`, sheet `s_fade_forced.png`). Cause: gbarecomp's compositor marks an OBJ pixel as a
BLDCNT first target only for semi-transparent OBJ (`gba_ppu.cpp`: `t1 = blend_enabled(ux) && obj_mode == 1`); BLDCNT bit 4 is
ignored for normal OBJ (a difference from hardware; gbarecomp is not edited here). So with the bundled gbarecomp the register path
cannot fade the text and the overlay must not either: `kPpuBlendsNormalObj = false` (`overlay_fade.h`) makes `fade_from_regs()`
inert and the per-scanline observer (`fade_sampler.cpp`, installed with `GbaPpu::set_presentation_observer`, host side only) is not
attached by the overlay. The mapping (mode 2 brighten `c + (255 - c) * t`, mode 3 darken `c * (1 - t)`, t = BLDY/16; alpha: text
alpha EVA/16, only when OBJ is a first target) and its unit tests stay, and flipping the constant switches it on if the PPU is
ever corrected. The observer's `scanline()` sees the registers each line is rendered with and `frame_ready()` publishes the table
of the completed frame, i.e. the frame in `latched_framebuffer()` that gets presented, so that lookup would be frame-consistent
without a lag constant.

**What the overlay follows: palette fades.** The engine fades the text's own OBJ palette (bank 0, entries 1 body / 2 shadow),
sampled every frame into `engine_text_rgba` / `engine_shadow_rgba` at engine time and paired with the presented frame through the
same 2-frame lag as the page. `apply_fade()` (called by `on_present` right before `OverlayRenderer::draw`; it does not depend on
where in the present sequence the draw happens) compares those colours with the ones the page started with
(`fade_from_text_colours`): all channels down -> Darken with `t = 1 - sum(cur)/sum(ref)`, all up -> Brighten with
`t = (sum(cur) - sum(ref)) / (765 - sum(ref))`; body and shadow must agree within 0.2 and the reference must be known, otherwise
no fade (colour-code words or another box palette are not fades). The renderer applies the equivalent tint to text / shadow /
outline colours (glyph edges then blend correctly with the already faded scene). With "Match game" colours the colours already
carry the fade and are not tinted twice (`DrawInfo::fade_from_palette`).
Limits: a page that starts during a fade has a faded reference and is not attenuated (unobserved, the box closes before fades);
BG-only palette fades (OBJ bank 0 untouched, e.g. the fade in from white at PPU frame 19819) leave the text at full colour, like
the engine text.

**Proof.** `KHCOM_FORCE_PALFADE=d:14:10400:40` darkens the WHOLE palette RAM (ramp to 14/16 over 40 frames from PPU frame 10400,
written from the dialogue draw call; `b:` brightens), so the real engine text fades with the scene during the first castle
dialogue. Same frames with the overlay (Match colours, and white) are in `qa_fade/pf_auth`, `pf_ov`, `pf_ovw`, sheet
`qa_fade/s_pf.png`: the box luminance is identical (166 -> 21), the text ink luminance follows (authentic 87/80/60/22/20/17,
overlay-match 70/65/49/38/25/17, overlay-white 85/78/55/37/26/16 at frames 10396..10436 step 8; 10 levels per 40 frames) and the
typewriter is in step ("just kne" at 10444 in both).

## Pairing check (consecutive frames 10560..10612, page "Well, whaddaya know!")

With lag 2, 19 of 20 reveal steps land on the same frame as the engine's own (the 20th is the end-of-page flush `rev 22 -> 52`,
shown one frame early); with lag 3 only 6 of 24. The calibrated lag stays 2.

## Battle dialogue

Code: `src/text_overlay/battle_text.{h,cpp}` (decision + PPU observer), present wiring in `engine_hooks.cpp`
(`draw_battle_text`), tests `tests/test_overlay_engine.cpp` ("battle message decode", "battle observer flow"). Confidence: H =
verified in real runs / captures, M = consistent but not directly observed.

### Where the battle boxes come from (findings)

| Finding | Evidence | Conf. |
|---|---|---|
| The story window path (layout `0x08064EF4`, update `0x08073238`, draw `0x080658B8`, pool `0x02034A84`) is never used by battle boxes: no entry logged in PPU frames 22500-26400 | `KHCOM_OVERLAY_TRACE` over the whole ROM range at frames 22800-22802 and 22640-22900 | H |
| Battle boxes use a CLONE of the text library: layout `0x0806BB44(x0, y0, text*, &cont)` (same 8.8 origin, same wrap `0x9B00` = 155 px, same stop after 3 lines `0x1800`, same glyph table `0x08F7D438` / tiles `0x090CBFB2`), pool pointer at EWRAM `0x02034A88` (128 x 24 bytes, same entry layout), pool init `0x0806BA74`, draw `0x0806C2C0` (called only from `0x080A4A50`). The entry count byte `0x02034A90` is shared with the story library | disassembly of the recompiled code; layout call seen once per page: `P=22659 r0=0x1000 r1=0x0C00 r2=0x0903C656` = origin (16,12), text "The laws of this castle<0A>require that your friends be<0A>transformed into cards." in ROM `0x0903xxxx` (not the cutscene range) | H |
| The per-frame code that types and submits the glyphs (task `0x080A3848` and everything it calls) is NOT in the static recompilation: gbarecomp bridges it through the interpreter ("bridged 0x080A3848 x166", also 0x08099180, 0x080A3640, 0x080A3A04). The bridge interprets the whole callee subtree, so no function-entry hook fires for the draw / sprite-put calls although glyph sprites are in OAM every frame | run log `self_heal_coverage=NOT_STATIC ... bridged`; 80 glyph records in the sprite list at the OAM builder (`0x08002F50`) while only ~15 `0x080023E0` puts are logged | H |
| Therefore the story hook approach (R0 := 0 at the draw call) cannot work for battle; the stand-down for battle in the story path stays | - | H |
| Glyph sprites in OAM: 16x16 square, 4bpp, normal mode, priority 0, palette bank 5 (colour-code words: bank 6), tile index in the glyph allocation, OAM x/y = pool entry (x, y) in the 240 px frame; the PPU adds the 22 px margin in the 16:9 view (only HUD sprites are moved by the OBJ provider) | OAM dump `KHCOM_OVERLAY_OAMDUMP`, 74 sprites for the 74-entry page; text x = 16 + 22 in the 284 wide frame | H |
| VRAM tile bytes of a glyph sprite == the ROM glyph (`0x090CBFB2 + 128 g`) | the decision matches every sprite of all pages seen | H |
| Typewriter: 1 glyph per ~3 frames; `A` completes the page at once; boxes: orange (1-3 lines, origin (16,12)), dark tutorial box (origin x 46, lines padded with spaces to centre them, colour-code words in cyan = another palette bank) | frames 22600-26400 | H |
| Pause menu inside the tutorial battle (START) did not open; card menu / other battle screens were not reachable with the scripts | `qa_batt/p_b` | not verified |

### Design (what is implemented)

No function hooks. A `gba::GbaPpu::PresentationObserver` (attached only while the overlay is On and nobody else owns the observer slot,
e.g. not together with `KHCOM_FADE_LOG`) sees, at scanline 0 of every frame, the OAM / VRAM / OBJ palette the PPU is about to render
and reads the pool (pointer `0x02034A88`, count `0x02034A90`) from guest memory:

1. pool entries -> cells (word from the tile's ROM source pointer, spaces have no tile; icon / non Latin glyphs -> stand down);
2. every glyph cell is matched with exactly one candidate OAM sprite (plain 16x16, no flip / rotation / blend / 256 colours) at the same
   (x, y) whose VRAM bitmap equals the ROM glyph; matched sprites must be a PREFIX of the page (typewriter) and no unmatched
   glyph-shaped sprite may share their palette banks; two sprites on one cell, a bitmap mismatch, an unsupported glyph, a bad pool, or the
   story window path being active (`engine::story_window_recent()`) -> stand down: the engine draws everything, nothing is drawn;
3. otherwise the decision is a `PageSnapshot` (`source = 1`): text with synthesised newline words (rows), glyph cells, `revealed` = prefix
   length, wrap 155 / pitch 12 / 3 lines, engine colours sampled from the OBJ palette of the sprites' banks (entry 1 body, 2 shadow;
   colour-code glyphs: `engine_highlight_*`), and the hide set;
4. HIDE: the OAM "disabled" bit of the matched entries is set for the frame being rendered and restored when the PPU latches the frame
   (`frame_ready`, before the guest's vblank code rewrites the whole OAM; entries the guest rewrote meanwhile are not touched).
   The OBJ x provider of `gba_ppu` cannot be used: the unextended (3:2) PPU path never calls it (measured: 0 calls with `--view-width 240`);
5. PAIRING: the snapshot is committed at `frame_ready` only if it belongs to the frame being latched (PPU frame counter), i.e. it is paired
   with exactly the latched framebuffer that is presented. No lag constant: the typewriter is frame-exact (consecutive captures
   22672-22703: glyph steps at the same frames in authentic and overlay).

Hide and draw are ONE decision per frame; any refusal leaves all engine glyphs. Residual cases: at a page change the pool is rewritten while
the old sprites are still in OAM for ~1 frame: bitmap mismatch / not-prefix -> that frame is authentic (one frame of engine font per page,
visible in the log as `bitmap-mismatch` / `bad-entry`); a view change between a frame and its presentation drops that text for the frame
(`presents_missing`). Savestate load: `restored()` forgets everything.

### Geometry

`DrawInfo::gba_w` = width of the presented image in GBA px: 240, or 240 + both margins in the 16:9 battle view (284). Snapshot x
coordinates (glyphs, origin) include the left margin (+22), so `dst` of the whole image maps 1:1 (contract note in `overlay_types.h`). Wide
boxes need no extra width: the engine itself wraps at 155 px from the origin (same constant as the story library); the box is wider only as
decoration. Lines the engine indents with spaces (dark tutorial boxes) are reproduced by the render side from the engine glyph x (see
`TEXT_OVERLAY_RENDER.md`).

### Contract additions (source compatible)

`PageSnapshot::source`, `engine_highlight_rgba`, `engine_highlight_shadow_rgba`; battle page ids start at `0x80000000`. Render side: per-line
indent in Keep mode, engine highlight colour.

### Verification (PNG evidence under `qa_batt/`)

* "laws of this castle" (`b2`, `mx_A_*`), "Cards are the hearts" (`mx_B_*`), dark tutorial box with cyan words and centred lines (`b4`, `mx_C_*`):
  authentic vs overlay, 16:9 and `--view-width 240` (`v32_*`, `s32b.png`), 852x480 / 960x640 / 1536x864, Keep and Reflow, families 0/3/6.
* Typewriter in step (`tw_a`, `tw_o`); page changes in the `[battle]` log lines (`KHCOM_OVERLAY_LOG=1`).
* Authentic runs byte-identical to the pre-change build (battle 16:9 / 3:2 frames and 132 story PNGs); overlay-on story captures identical to the
  pre-change build.
* The settings menu (Esc) draws over the battle text.
* Not verified: battle pause / card menu, a second box at the same time (the pool holds one page), fades during a battle box, non-battle uses of the
  same library (field tutorials) beyond the strict match rules.

Investigation switches (harness only): `KHCOM_OVERLAY_TRACE="lo-hi,..."` + `KHCOM_OVERLAY_TRACE_FRAMES="a-b"` (function entries, pool dump at every
`0x080658B8`, sprite lists at the OAM builder), `KHCOM_OVERLAY_OAMDUMP=<ppu frame>`, `KHCOM_OVERLAY_OAMSUM="a-b"`, `KHCOM_OVERLAY_LOG=1` (`[battle]` lines).

## Test switches (harness only, documented as temporary)

`KHCOM_SET="hdtext_enabled=1;hdtext_size=100;hdtext_family=0;hdtext_style=1"` (the settings of the render side, same keys as the
`[HdText]` section of `khcom_config.ini`, see TEXT_OVERLAY_RENDER.md; legacy text keys are ignored). Environment: `KHCOM_OVERLAY_LOG=1` (per-frame log of layout/window/present,
stderr), `KHCOM_OVERLAY_ENGINE_LOG=1` (hook installed in LOG-ONLY mode: capture + decisions, no hide, no draw),
`KHCOM_OVERLAY_NOHIDE=1` (draw but never hide: both copies visible, for alignment checks), `KHCOM_OVERLAY_LAG=n`
(override the pairing lag), `KHCOM_OVERLAY_DUMP=1` (pool/palette dump per page, with the log).
Fade aids: `KHCOM_FADE_LOG=1` (per present: PPU frame, `dlg`/`txt`, mean luminance of the latched frame, backdrop, OBJ palette 1/2,
DISPCNT and BLDCNT/BLDALPHA/BLDY of lines 0/100/159, printed on change; attaches the scanline observer, works with the overlay off;
combine with `KHCOM_OVERLAY_ENGINE_LOG=1` for `dlg`/`txt`), `KHCOM_FORCE_BLD=<hex BLDCNT>:<BLDY>` and
`KHCOM_FORCE_PALFADE=<b|d>:<max 1..16>:<start PPU frame>:<ramp frames>` (write BLDCNT/BLDY / the whole palette from the dialogue draw
call so authentic and overlay runs can be compared under a fade; need the hook: overlay on or `KHCOM_OVERLAY_ENGINE_LOG=1`).
Never set in normal use.

## How to re-run the verification

Savestate shortcut (a boot to dialogue is ~4 min even at 4x; the state is a one-off, taken under the same settings):
```powershell
$S="perf_mode=0;screen_mask=0;hdtext_size=100"
.\tools\capture\run_capture.ps1 -Dir captures/s1 -Script tools/capture/scripts/first_dialogue.txt -Set $S -Fast -SaveState "8900:1" -ExitFrame 8940
# lag / single copy (consecutive frames cannot be fast-forwarded: -Fast presents only every 4th frame)
$env:KHCOM_OVERLAY_LOG="1"
.\tools\capture\run_capture.ps1 -Dir captures/o2 -Frames "8976-8992,9000,9100-9125" -Mode both -Window 852x480 `
   -Set "$S;hdtext_enabled=1" -LoadState "roms/<rom>.state1" -Script tools/capture/scripts/first_dialogue.txt   # overlay
.\tools\capture\run_capture.ps1 -Dir captures/a2 ... -Set $S ...                                                 # authentic
```
Overlay output frames show only our text; `native` captures of the overlay run show the box with NO engine glyphs; the
authentic run's native captures show the engine text at exactly the same frames the overlay output shows ours.
Hook-flow tests: `khcom_unit_tests` ("Overlay engine: ..." lines).

## Known limitations / open items

* Fades: palette fades are followed (proved with forced fades); no natural frame has dialogue text on screen during a fade, and a
  page that starts mid-fade is not attenuated. The BLDCNT path is inert on the bundled PPU (see "Screen fades").
* The end-of-page flush (`rev` jumps to the total) is shown one frame before the engine shows it (once per page, ~16 ms).
* Pause/card/subtitle screens not reachable in the scripted runs; reasoning (hook scope + kind/range/pool predicate) says
  they cannot be affected, not observed.
* Types/pages with Shift-JIS or icon words, non-cutscene text: stand down (engine text). Battle boxes: see "Battle dialogue".
* `0x4079` colour pages: unobserved; the colour flag is passed in `PageGlyph::color` only.
* The overlay is drawn only from the render-copy intercept, i.e. when the game image is copied in that present; a present without that copy
  draws no overlay text.
* Savestate loaded exactly on a frame where the counters stay consistent is guarded only by the pool/page integrity checks.
