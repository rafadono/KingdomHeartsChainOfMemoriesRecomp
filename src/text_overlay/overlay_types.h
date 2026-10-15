#pragma once
// Contract of the ENGINE side (captures what the game is showing and hides the engine's own dialogue glyphs).
// The RENDER side is the generic text engine of gba-improvements (gbaimp/text_*.h): text_adapter.h turns a
// PageSnapshot into a gbaimp::text::TextPage and owns the host's settings object. Namespace khcom::overlay.

#include "gbaimp/text_fade.h"
#include "gbaimp/text_types.h"

#include <cstdint>
#include <string>
#include <vector>

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
    // BLDCNT/BLDALPHA/BLDY of the text's scanline (gbaimp/text_fade.h): 0 none, 1 alpha (fade = 1 - EVA/16),
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

// The generic types of the text engine, under the names the engine side has always used.
using gbaimp::text::DrawInfo;
using gbaimp::text::TextSettings;
using gbaimp::text::TextStyle;
using gbaimp::text::WrapMode;
using gbaimp::text::FadeMode;
using gbaimp::text::FadeState;
using gbaimp::text::fade_from_regs;
using gbaimp::text::fade_from_text_colours;
using gbaimp::text::fade_rgb;
using gbaimp::text::kPpuBlendsNormalObj;

} // namespace khcom::overlay
