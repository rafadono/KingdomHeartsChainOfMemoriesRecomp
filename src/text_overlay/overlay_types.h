#pragma once
// Shared contract between the ENGINE side (captures what the game is showing and hides the
// engine's own dialogue glyphs) and the RENDER side (lays out and draws the replacement text
// at output resolution). Both sides are developed in parallel against this header; keep it
// source-compatible (add fields, do not rename or remove). Namespace khcom::overlay.

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Rect;

namespace khcom::overlay {

// One glyph the ENGINE has laid out for the current dialogue page, in typewriter order.
struct PageGlyph {
    uint16_t word = 0;     // engine text word (ASCII code for normal characters)
    float gx = 0.0f;       // pen position of the glyph cell, GBA pixels, relative to the 240x160 frame
    float gy = 0.0f;
    uint8_t color = 0;     // engine colour flag (0 = default); see docs for the exact semantics
};

// What the engine is showing right now (engine side fills it once per presented frame).
struct PageSnapshot {
    bool valid = false;               // false => the overlay must not draw and nothing is hidden
    uint64_t frame = 0;               // emulated frame number this snapshot belongs to
    std::vector<uint16_t> text;       // page words in order, INCLUDING spaces and newline words (0x000A)
    std::vector<PageGlyph> glyphs;    // engine layout, one entry per drawn word, typewriter order
    int revealed = 0;                 // how many glyph entries are already typed (0..glyphs.size())
    bool page_complete = false;       // typewriter finished and the page is waiting for a key
    float origin_x = 0.0f;            // text origin in GBA pixels (first pen position of the page)
    float origin_y = 0.0f;
    int wrap_px = 155;                // engine wrap limit in GBA pixels
    int line_pitch = 12;              // engine line pitch in GBA pixels
    int max_lines = 3;                // engine lines per page
    float box_x = 0.0f, box_y = 0.0f; // optional: text box rectangle in GBA pixels (0 = unknown)
    float box_w = 0.0f, box_h = 0.0f;
    float fade = 0.0f;                // screen fade strength 0..1 for the layer the text lives on (see fade_mode), 0 = none
    uint32_t page_id = 0;             // increments when the engine starts a new page
    // ENGINE-SIDE ADDITIONS (source compatible): colours the engine itself draws this page with, sampled from the
    // OBJ palette (bank 0, entries 1 = body, 2 = shadow), 0xRRGGBBAA; 0 = unknown. The render side may use them to
    // match the authentic look or ignore them.
    uint32_t engine_text_rgba = 0;
    uint32_t engine_shadow_rgba = 0;
    // FADE ADDITION: how the PPU blends the OBJ layer (the engine's own text) for the presented frame, decoded from
    // BLDCNT/BLDALPHA/BLDY of the text's scanline (overlay_fade.h): 0 none, 1 alpha (fade = 1 - EVA/16),
    // 2 brighten to white, 3 darken to black (fade = BLDY/16). Filled at present time; `fade` carries the strength.
    int fade_mode = 0;
    // BATTLE DIALOGUE ADDITIONS (source compatible; see docs/TEXT_OVERLAY_ENGINE.md "Battle dialogue"):
    // 0 = story window (function hooks), 1 = battle message box (PPU observer: OAM + glyph pool). Coordinates of glyphs / origin /
    // box are in GBA pixels relative to the left edge of the PRESENTED game image, whose width is DrawInfo::gba_w: 240 for
    // the 3:2 picture, 284 (240 + the two margins) for the 16:9 battle view, where the 240 px picture is offset by the margin.
    int source = 0;
    // Colours of the engine's colour-code words (glyph sprites of PageGlyph::color != 0), sampled from the OBJ palette the
    // PPU renders with (entry 1 body, entry 2 shadow of the sprite's bank), 0xRRGGBBAA, 0 = unknown (the render side then
    // uses its own highlight colour).
    uint32_t engine_highlight_rgba = 0;
    uint32_t engine_highlight_shadow_rgba = 0;
};

enum class WrapMode : int {
    KeepEngineBreaks = 0,  // use the engine's line breaks (same words per line as the original)
    Reflow = 1             // re-wrap the page text to the box width at the chosen size
};

enum class TextStyle : int { Plain = 0, Shadow = 1, Outline = 2 };

// User-facing settings of the output-resolution text. All of them apply live.
struct OverlaySettings {
    bool enabled = false;          // false => original engine text, nothing hooked/hidden
    int family = 0;                // index into the font family list (built-in fonts, then fonts/ folder)
    float size_em = 10.0f;         // font size in GBA pixels (em); 10 ~ original cap height
    float line_spacing = 1.2f;     // multiplier of the font's natural line height
    float letter_spacing = 0.0f;   // extra advance in GBA pixels
    WrapMode wrap = WrapMode::KeepEngineBreaks;
    TextStyle style = TextStyle::Shadow;
    float shadow_offset = 0.6f;    // GBA pixels
    float outline_width = 0.0f;    // GBA pixels (Outline style)
    uint32_t text_rgba = 0xFFFFFFFFu;     // 0 = match the colour the engine draws this page with (PageSnapshot::engine_text_rgba)
    uint32_t shadow_rgba = 0x000000B0u;   // 0 = match the engine's shadow colour
    uint32_t outline_rgba = 0x000000FFu;
    float opacity = 1.0f;          // 0..1
    float box_padding = 0.0f;      // extra GBA pixels kept free inside the box
    bool auto_fit = true;          // shrink the size until the page fits inside the box height
    float min_size_em = 5.0f;      // auto-fit lower bound
};

// Where and how the overlay is drawn this frame.
struct DrawInfo {
    SDL_Renderer* renderer = nullptr;
    int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;   // output rectangle of the 240x160 game image
    int gba_w = 240, gba_h = 160;
    float global_alpha = 1.0f;                        // includes the alpha-blend fade attenuation (1 - fade for fade_mode 1)
    // FADE ADDITION: brightness fades (BLDCNT effect 2 / 3) pull the colours of what is drawn towards white / black, so
    // the renderer applies them to the text / shadow / outline colours (FadeMode values of overlay_fade.h; 0 = none).
    int fade_mode = 0;
    float fade_level = 0.0f;                          // 0..1 (BLDY / 16, or the equivalent of a palette fade)
    bool fade_from_palette = false;                   // the fade was derived from the engine text colours (already faded)
};

// Render side interface. The engine side only talks to this; the render side implements it.
class OverlayRenderer {
public:
    static OverlayRenderer& instance();
    // Returns true if something was drawn for this snapshot (the engine side uses this only for logging;
    // hiding the engine glyphs is decided by the engine side, see `should_overlay`).
    bool draw(const PageSnapshot& page, const OverlaySettings& settings, const DrawInfo& info);
    // Cheap check used by the engine side BEFORE hiding glyphs: true when the render side can draw
    // this page with these settings (fonts loaded, page fits or can be auto-fit, ...).
    bool can_draw(const PageSnapshot& page, const OverlaySettings& settings, const DrawInfo& info);
    void invalidate();                                // drop caches (settings/font/size/scale changed)
    // Font families available (built-in first, then user fonts found in the fonts/ folder).
    std::vector<std::string> family_names();
    // Statistics for logs/tests.
    uint64_t draws() const;

    // ---- additions by the render side (source-compatible) ----
    // Re-reads the fonts/ folder (user fonts are otherwise scanned once, lazily). Invalidates caches.
    void rescan_fonts();
    // Index of a family by its display name (case-insensitive), -1 when unknown.
    int family_index(const std::string& name);
    // Frees the GPU atlas. Call before destroying the SDL_Renderer that was used for drawing (optional:
    // a changed renderer pointer is detected and the stale atlas is simply forgotten).
    void release_gpu();
    // Counters for tests/logs.
    uint64_t layout_builds() const;      // page layouts computed (cache misses)
    uint64_t glyphs_rasterized() const;  // glyph bitmaps rasterised into the atlas
    uint64_t atlas_flushes() const;      // atlas resets (full / invalidate / scale change)
};

// Process-wide settings object (the render side owns it; UI/config write it; the engine side reads it).
OverlaySettings& settings();

} // namespace khcom::overlay
