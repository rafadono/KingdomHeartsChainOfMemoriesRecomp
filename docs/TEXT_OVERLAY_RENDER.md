# HD dialogue text overlay - render side

Scope: everything that turns a `PageSnapshot` (what the engine shows, see `src/text_overlay/overlay_types.h`) into
text drawn at OUTPUT resolution: fonts, layout, glyph rasteriser, atlas and drawing, settings/UI/persistence. The engine side
(capturing the page, hiding the engine glyphs) is a separate module; the original text is untouched while the overlay is off
(`hdtext_enabled=0`, the default). The old in-engine "enhanced text" system has been removed (see "Legacy keys" below).

## Files

| File | Role |
|---|---|
| `src/text_overlay/overlay_fonts.{h,cpp}` | `FontFace` / `FontLibrary`: built-in families, user fonts, metrics, glyph bitmaps (all stb_truetype use), TrueType validator |
| `src/text_overlay/overlay_fonts_data.{h,cpp}` | generated C arrays of the five extra families (`tools/gen_font_data.py --extra`) |
| `src/text_overlay/overlay_layout.{h,cpp}` | page layout: tokenising, entry mapping, wrap, auto-fit, vertical placement (pure CPU) |
| `src/text_overlay/overlay_raster.{h,cpp}` | glyph coverage, shadow and outline bitmaps (pure CPU) |
| `src/text_overlay/overlay_renderer.cpp` | `OverlayRenderer`, `settings()`, layout cache, SDL atlas and drawing |
| `src/text_overlay/overlay_synth.h` | synthetic `PageSnapshot`s for tests / the dump tool |
| `src/game_config.cpp` | UI items `hdtext.*`, callbacks, `[HdText]` persistence, legacy handling |
| `tests/test_overlay_render.cpp` | unit tests (built into `khcom_unit_tests`) |
| `tools/overlay_dump.cpp`, `tools/overlay_png_to_ppm.py` | PNG dump tool |

## Algorithm

**Scale.** `sx = dst_w / gba_w`, `sy = dst_h / gba_h` (output px per GBA px; 6.1875 for a 6.19x request). The em in output px is
`size_em * sx` quantised to 1/32 px. All caches are keyed by it, so a scale (DPI / window) or size change simply misses.

**Glyph rasteriser (strategy chosen in a prototype whose design notes are not part of the repository).**
stb_truetype analytic coverage; *light vertical hint*: the vertical scale is chosen so that the x-height is a whole number of
output pixels, the baseline of every line is an integer output row; the pen x stays fractional and is bucketed into quarter-pixel
phases (4 bitmaps per glyph); coverage is boosted with `c^0.85` (mild stem darkening); blending is plain sRGB `SDL_BLENDMODE_BLEND`.
No supersampling, no kerning (the subsets carry none; a user font with a legacy `kern` table is kerned). Some families have an
optical `size_adjust` (Pixel Terminal 1.2, Pixel Arcade 0.72) so that "size 10" looks comparable across families.

**Shadow / outline.** Shadow = union of the glyph shifted E, S and SE by `shadow_offset * sx` output px (the game's own rule,
fractional shifts resampled bilinearly) drawn before the glyphs; default 0.6 GBA px. Outline = glyph dilated by a disc of radius
`outline_width * sx` (anti-aliased edge). Both are cached bitmaps (variant + 1/8 px parameter in the key). Colour and alpha are
applied with the texture colour/alpha mod (white RGBA atlas, alpha = coverage), so one atlas serves every colour.
Opacity < 1 multiplies the alpha of shadow and text separately (the shadow shows through the glyph slightly; a true group
opacity would need a render target, deliberately avoided for renderer portability).

**Atlas.** One RGBA32 static SDL texture (1024 x 1024, grows to 2048 / 4096), shelf-packed, filled lazily with
`SDL_UpdateTexture`; drawing is `SDL_RenderCopy` of 1:1 integer rectangles (works on D3D11, OpenGL and the software renderer; no
state of the renderer is changed). All glyphs of a frame are made resident BEFORE the first quad is queued; if the atlas is full it
is flushed/grown at that point, never in the middle of a frame. Texture properties (RGBA, STATIC, not 240/284 x 160 streaming/target)
make the SDL intercepts in `src/sdl_intercepts.cpp` pass the atlas through untouched. A failed `SDL_RenderCopy` drops the atlas
and rebuilds it next frame. Call `OverlayRenderer::release_gpu()` before destroying the renderer (a changed renderer pointer is
also detected).

**Layout cache.** One page layout is kept, keyed by a 64-bit hash of (page id, words, glyph rows/colours, origin, wrap, pitch,
lines, box, every layout-relevant setting, output size, font generation). `revealed` (typewriter) and `frame` are NOT part of the key:
the whole page is laid out once and the reveal is a mask, so words never jump while typing. Warm draws allocate nothing and rasterise
nothing (asserted by the tests).

**Text area.** Width = `wrap_px` (155) minus `2 * box_padding`; left edge = `origin_x + padding`, so the text never extends
beyond the engine's own wrap limit (no slack: the portrait sits right at the edge for left placements). Height =
`(max_lines - 1) * line_pitch + 16` (40 px: the 16-row glyph cell of the last line) minus padding. If the snapshot carries a
`box_*` rectangle that contains the text origin, the area is intersected with it. Shadow/outline extents are subtracted so the
ink, not only the pen box, stays inside.

**Vertical placement.** The first baseline is the engine's (`origin_y + 11`); line pitch is `em * line_spacing`
rounded to whole output pixels (uniform lines). It is shifted only as much as needed to keep the real glyph ink (per-glyph
extents, not font-wide metrics) inside the area. In Reflow, when the page ends up with fewer lines than the engine's own
layout, the block is centred on the engine block instead.

**Wrapping.** *KeepEngineBreaks*: the lines are the engine's (from the glyph rows `gy` when the snapshot has glyph entries and
they are consistent, otherwise from the 0x0A words): same words per line, only font/size/spacing change, so the page never changes
line count. *Reflow*: words (split on spaces and newlines, a lone newline is a soft break, a blank line is a paragraph break) are
greedily wrapped to the area width at the current size, never splitting a word; a one-word last line is avoided when the previous
line can give its last word away. Proportional fonts typically turn 3-line pages into 2 lines.

**Auto-fit.** From `size_em` downwards in 0.25 GBA px steps to `min_size_em`, the first size where every line fits the width and the
ink fits the height is used. If none fits, the layout fails and `can_draw` returns false (the engine stands down and the original
text shows). With auto-fit off the requested size is used even if it overflows.

**Typewriter.** `PageSnapshot.revealed` counts glyph entries. Entries are the engine glyph entries in order: every word except
newline (0x0A) and the colour words (0x4079 / 0x4000); a space is an entry exactly when the snapshot's next glyph entry is a space
(so both "spaces are entries" and "spaces are not" engines work), and when `glyphs` is empty all non-control words count. A
drawn glyph is shown iff its entry index < `revealed`; letters of a partly revealed word appear one by one.

**Words.** 0x20..0x7E direct; 0x80..0x9F through the Windows-1252 table (0x85 ellipsis, 0x91-0x94 quotes, ...); 0xA1..0xFF
Latin-1; 0xA0 is a space; 0xE000 (the engine's pause glyph) draws an ellipsis. Substitutes when a family lacks a glyph: curly quotes -> ASCII
quotes, dashes -> hyphen, ellipsis -> three dots, then the first built-in family. Any other word (Shift-JIS, undefined 0x81/0x8D/..., icons)
makes the page unrenderable: `can_draw` is false. Colour words toggle a highlight colour (warm yellow, fixed) for the following glyphs;
a non-zero `PageGlyph.color` does the same.

## Settings (`khcom::overlay::settings()`, all live)

| UI label | INI key (`[HdText]`) | Range / encoding | Default | Meaning |
|---|---|---|---|---|
| Dialogue Text | `hdtext_enabled` | 0 Original, 1 HD overlay | 0 | overlay off = nothing hooked, original text |
| Font Family | `hdtext_font` (name) | built-ins, then `fonts/*.ttf` | Humanist Sans | stored by NAME, so adding fonts does not shift it |
| Font Size | `hdtext_size` | 30..200 (tenths of a GBA px) | 100 | em; 10 px is about the original cap height |
| Line Spacing % | `hdtext_line_spacing` | 80..200 | 120 | pitch = em * value (120 -> 12 px at size 10, the engine pitch) |
| Letter Spacing | `hdtext_letter_spacing` | -10..30 tenths of a px | 0 | extra advance per letter |
| Wrapping | `hdtext_wrap` | 0 keep breaks, 1 reflow | 0 | see above |
| Text Style | `hdtext_style` | 0 plain, 1 shadow, 2 outline | 1 | |
| Shadow Offset | `hdtext_shadow_offset` | 2..30 tenths | 6 | game uses 1 px |
| Outline Width | `hdtext_outline_width` | 2..30 tenths | 8 | |
| Text / Shadow / Outline Colour | `hdtext_color` / `_shadow_color` / `_outline_color` | preset index 0..7 / 0..6 / 0..3 | 7 / 6 / 0 (match game) | white...near black, 7 = match the game's own colour; black soft...white glow, 6 = match; black/navy/brown/white |
| Text Opacity % | `hdtext_opacity` | 20..100 | 100 | multiplied with the alpha-fade attenuation (`global_alpha`) |
| Box Padding | `hdtext_padding` | 0..80 tenths | 0 | free space inside the text area |
| Auto-Fit | `hdtext_auto_fit` | 0/1 | 1 | |
| Minimum Size | `hdtext_min_size` | 30..120 tenths | 50 | auto-fit lower bound |

The menu section is "Dialogue Text (HD)"; the conversation log items stay in "Dialogue & Story". The family list shown in the menu
is read at start-up (adding a `.ttf` needs a restart; `OverlayRenderer::rescan_fonts()` rescans the folder but the menu list is not refreshed).

**Legacy keys.** `text_mode`, `font_family`, `font_size`, `font_density`, `font_scale`, `font_style`, `line_capacity`,
`soft_word_wrap` (and the matching `dialogue.*` UI keys) still parse without error but are ignored: they do not turn the overlay or
any in-engine enhanced path on, and they are never written back (the first save drops them from `khcom_config.ini`). The old
"non-default legacy values imply Enhanced" migration was removed. The capture harness (`KHCOM_SET`, variants) accepts the `hdtext_*` keys instead of
the old text keys.

## User fonts

Any `.ttf` in `fonts/` (relative to the working directory) is listed after the built-ins as `<file name> (fonts/)`. At most 8 fonts,
each at most 8 MB, TrueType outlines only (`glyf`; CFF/OTF is ignored). Every file goes through `validate_truetype` first (table
directory in bounds, required tables, `loca` monotonic and inside `glyf`, every glyph's contour/flag/coordinate data and composite
references inside its own bytes, no composite cycles) because stb_truetype does not bounds-check malformed fonts; broken files are
silently skipped. Fonts are read into private memory, never executed. Glyphs a user font lacks fall back to Humanist Sans.
There is no "upscaled original ROM font" family (the ROM font cannot be used as a vector font; not implemented).

## Fonts provenance

`third_party/fonts/README.md` (URLs, SHA-256 of the originals and subsets, licence texts) and `NOTICE`. Families: Humanist Sans /
Narrow / Serif (PT Sans, PT Sans Narrow, PT Serif), Legible Sans (Atkinson Hyperlegible), Rounded Sans (Varela Round),
Monospace (Cousine), Pixel Terminal (VT323), Pixel Arcade (Press Start 2P); all SIL OFL 1.1, Latin subsets.
`src/text_fonts_data.cpp` / `src/text_fonts.h` hold the three PT faces and are used by the overlay.

## Tests and the PNG dump

* `./build/khcom_unit_tests` (Linux) / `.\build-vs\Release\khcom_unit_tests.exe` (Windows) run `run_overlay_render_tests`: every family renders
  every printable ASCII glyph with ink; Keep keeps the engine line structure and never changes the line count; Reflow never breaks
  or reorders words and everything stays inside the area at scales 3, 4, 5.4, 6.19 and em 6..14; auto-fit shrinks / fails / is
  bypassed; reveal prefix is a subset of the full page and ink grows monotonically; unsupported words and disabled settings give
  `can_draw` false; drawing on SDL's software renderer stays inside the box bounds for all styles and wraps; warm draws rasterise nothing,
  scale / size changes rebuild, `invalidate()` flushes; fades dim; user fonts (valid / truncated / garbage / non-ttf) and a
  mutation fuzz of the validator.
* PNG dump (no game needed), e.g. in the Docker image or any g++ with SDL2:
  `g++ -O2 -std=c++17 -o overlay_dump tools/overlay_dump.cpp src/text_overlay/overlay_{fonts,fonts_data,layout,raster,renderer}.cpp src/text_fonts_data.cpp src/capture_harness.cpp $(sdl2-config --cflags --libs)`
  then `SDL_VIDEODRIVER=dummy ./overlay_dump --out out --scales 3,4,5.4,6.19 [--bg frame.ppm --bg-x 22] [--fonts dir]`.
  `--bg` takes a P6 PPM of a game frame (`python tools/overlay_png_to_ppm.py frame.png frame.ppm`; `--bg-x 22` for 284x160 captures);
  the engine's own text area is painted over with the box colour. Output per scale: `families_<s>x.png` (every family, em 10),
  `sizes_<s>x.png` (em 6..14, Reflow, auto-fit), `styles_<s>x.png` (plain/shadow/outline x keep/reflow), `pages_<s>x.png` (typewriter reveal).

## Limitations

No kerning in the embedded subsets; no group opacity; the highlight colour of colour words is fixed; the text is left-aligned; the
family list of the menu is fixed at start-up; the interaction with the engine (hiding glyphs, pairing, fades) is the engine side's job
(`global_alpha` / `fade_mode` carry the fade).

## Fades and colours (render side)

* `DrawInfo::global_alpha` carries alpha-type fades; brightness fades (`fade_mode` 2 / 3, `fade_level` t) pull the text, shadow, outline
  and highlight colours towards white (`c + (255 - c) * t`) or black (`c * (1 - t)`) with `fade_rgb()` (`overlay_fade.h`); glyph
  coverage stays the alpha, so edges blend with the already faded scene. Tests: darken 0.5 dims, darken 1.0 is black, brighten 1.0 of
  a dark text is white.
* **Colour defaults.** The engine draws dark brown (99,33,0) with a tan shadow (208,152,56) on its orange box. Measured on
  `qa_fade/s_col.png` / `s_styles.png`: white with a soft black shadow reads fine but has only ~2.3:1 contrast on the orange and
  looks foreign; near-black is heavy; **brown matches the authentic look and has ~5:1 contrast**, and on the dark tutorial boxes
  the engine's own light colours are the right ones. New default (`settings()`): `text_rgba = 0`, `shadow_rgba = 0` = "Match game"
  (the colour the engine uses for the page, `PageSnapshot::engine_text_rgba/engine_shadow_rgba`, fallback brown / tan when unknown).
  UI: Text Colour index 7 "Match game (brown)", Shadow Colour index 6 "Match game (tan)"; the 7 / 6 fixed presets are unchanged
  (white is still index 0). The INI keeps the same keys (`hdtext_color=7`, `hdtext_shadow_color=6`); the unset default is match.
  Outline style with a matched (brown) text and a black outline merges into a heavy blob; pick a lighter outline or fixed colours.

## Visual QA (final renderer, Windows exe, captures under `C:\Users\RafaelInostroza\khr-work\qa_fade\`)

| Area | Evidence | Result |
|---|---|---|
| Pages 1 / 2 / 3 lines, speaker left and right, top placement | `s_a.png`, `s_pages1.png` (authentic | overlay pairs, pages Goofy/Donald/Sora, top box "Well, whaddaya know!") | text inside the box, clear of portrait and key arrow, same line breaks (Keep), right colours; no defect |
| 8 families x Keep / Reflow, size 100 | `s_fonts_keep.png`, `s_fonts_reflow.png`, `s_fonts_reflow2.png` | all legible, everything inside the box; Pixel Arcade Reflow uses 3 lines and just fits above the arrow |
| Sizes 60..180, Keep / Reflow | `s_sizes.png` | auto-fit keeps every size inside; Reflow with fewer lines is centred on the engine block |
| Styles Plain / Shadow / Outline, text colour presets 0..7, outline colours | `s_styles.png`, `s_col.png` | all fine; the only ugly combination is Match-brown text with a black Outline |
| Output scales 852x480 (3x), 960x640 (4x), 1536x864 (5.4x), `--sharp-filter`, grid mask 0/1 | `s_scales.png` | crisp at every scale; the grid mask is not drawn over the text (the overlay is drawn after the mask in the render-copy intercept: if the draw is moved before `render_mask`, the mask will cover the text; re-check) |
| Typewriter, consecutive frames (9020-9215, 10500-10612) | `tw_*`, `tw2_*`, `tw3_*`, `tw4_*`, `s_tw1.png`, `s_tw3.png` | single copy at every frame, in step (19/20 reveal steps same frame), no missing frame outside the engine's close frames, no flicker at page change; one cosmetic defect: the end-of-page flush shows one frame early (not fixed, 16 ms) |
| Battle (16:9 and `--view-width 240`), HUD, tutorial box | `s_bt.png`, `s_bt32.png` | identical to authentic with the overlay on (the overlay never runs in battle) |
| Pause menu / card screen / movie subtitle | `mn_*` (START in the castle hall) | not reachable with the scripted input (START does not open the pause menu while a dialogue is active; the loaded state stands down with `no-layout`, engine text intact); not verified |
| Fades | `pf_*`, `s_pf.png`, `fd_*`, `s_fade_forced.png` | see docs/TEXT_OVERLAY_ENGINE.md "Screen fades" |

Defects found: none that needed a code fix in layout / drawing; the engine-side items are the one-frame-early end-of-page flush and the
black-outline-on-brown look (user choice). The harness gained `hdtext_family` (index) in `KHCOM_SET` / variants (it only accepted names via the
config file), used by the font matrix.

## Battle message boxes (additions)

* Keep mode: a line whose first glyph is right of the page origin (the engine pads centred tutorial boxes with spaces) starts at the same engine
  offset (`(gx_first - origin_x) * scale`); the indent counts in the fit check. Pages without leading spaces are unchanged; Reflow ignores it.
* Colour-code words use `PageSnapshot::engine_highlight_rgba` (the engine's own colour, e.g. cyan) when given, else the fixed warm yellow; a
  palette-derived fade is not applied twice to it.
* `DrawInfo::gba_w` may be 284 (16:9 battle image); snapshot x already includes the margin offset.
* Tests: `test_battle_additions` in `tests/test_overlay_render.cpp`.
