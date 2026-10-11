#pragma once
// Layout of one dialogue page for the HD overlay: word wrap, auto-fit and typewriter entry mapping.
// Pure CPU (no SDL); positions are in OUTPUT pixels relative to the origin of the 240x160 game image
// (add DrawInfo::dst_x / dst_y). See docs/TEXT_OVERLAY_RENDER.md.
#include "overlay_fonts.h"
#include "overlay_types.h"

#include <cstdint>
#include <vector>

namespace khcom {
namespace overlay {

// Engine metrics the overlay is aligned with (ROM glyph cell: 16 rows, baseline below row 10).
constexpr int kEngineBaselineRow = 11;
constexpr int kEngineCellH = 16;

struct LayoutItem {
    uint32_t cp = 0;        // Unicode code point actually drawn
    int gi = 0;             // glyph index in `face`
    uint16_t face = 0;      // FontLibrary face id
    uint8_t colour = 0;     // 0 = text colour, 1 = highlight (engine colour code)
    int line = 0;
    int entry = -1;         // typewriter entry index of the engine glyph this one belongs to (-1 never revealed)
    float x = 0.0f;         // pen position, output px
};

struct LayoutLine {
    int first = 0, count = 0;   // range in PageLayout::items
    int baseline = 0;           // output px, integer
    float x0 = 0.0f, x1 = 0.0f; // pen extent of the line (advance based), output px
    int words = 0;
};

struct PageLayout {
    bool ok = false;
    const char* reason = "";        // why ok == false
    float em_gba = 0.0f;            // em actually used (after auto-fit), GBA px
    float em_px = 0.0f;             // em in output px (quantised to 1/32 px)
    int pitch_px = 0;               // line pitch in output px
    int xheight_px = 0;             // fitted x-height of the primary face in output px
    bool shrunk = false;            // auto-fit reduced the size
    float sx = 1.0f, sy = 1.0f;     // output px per GBA px
    float area_l = 0, area_t = 0, area_r = 0, area_b = 0;   // text area, output px (relative to dst origin)
    float ink_t = 0, ink_b = 0;     // vertical glyph ink extent over the page, output px
    int entry_count = 0;            // number of typewriter entries covered
    int engine_lines = 0;           // lines of the engine's own layout
    std::vector<LayoutItem> items;  // drawable glyphs only (spaces and control words are not items)
    std::vector<LayoutLine> lines;
};

// Maps an engine text word to a Unicode code point. Returns 0 when the word is not renderable
// (control words 0x0A / 0x4079 / 0x4000 / 0x20 are handled by the layout and return 0 here as well).
uint32_t engine_word_to_codepoint(uint16_t word);
bool is_engine_control_word(uint16_t word);   // 0x0A, 0x4079, 0x4000

// Lays out `page` with `st` at the output scale (sx, sy). Returns out.ok.
bool layout_page(const PageSnapshot& page, const OverlaySettings& st, float sx, float sy, PageLayout& out);

// Hash of everything that influences the layout (cache key helper).
uint64_t layout_key(const PageSnapshot& page, const OverlaySettings& st, int dst_w, int dst_h, uint64_t font_generation);

} // namespace overlay
} // namespace khcom
