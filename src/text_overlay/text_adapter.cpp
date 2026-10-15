#include "text_adapter.h"

#include "../text_fonts.h"
#include "overlay_fonts_data.h"

#include "gbaimp/text_fonts.h"

#include <mutex>

namespace khcom::overlay {

uint32_t engine_word_to_codepoint(uint16_t w) {
    static const uint16_t k80[32] = {
        0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
        0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
    if (w > 0x20 && w < 0x7F) return w;
    if (w >= 0x80 && w <= 0x9F) return k80[w - 0x80];
    if (w >= 0xA1 && w <= 0xFF) return w;
    if (w == 0xE000) return 0x2026;   // glyph 25: the 12 px pause mark, drawn as an ellipsis
    return 0;
}

gbaimp::text::TextPage to_text_page(const PageSnapshot& snap) {
    using gbaimp::text::TextChar;
    gbaimp::text::TextPage page;
    page.valid = snap.valid;
    page.revealed = snap.revealed;
    page.page_id = snap.page_id;
    page.origin_x = snap.origin_x;
    page.origin_y = snap.origin_y;
    page.wrap_px = snap.wrap_px;
    page.line_pitch = snap.line_pitch;
    page.max_lines = snap.max_lines;
    page.box_x = snap.box_x;
    page.box_y = snap.box_y;
    page.box_w = snap.box_w;
    page.box_h = snap.box_h;
    page.native_text_rgba = snap.engine_text_rgba;
    page.native_shadow_rgba = snap.engine_shadow_rgba;
    page.native_highlight_rgba = snap.engine_highlight_rgba;
    // The glyph cell of the game's font is 16 rows with the baseline below row 10: the TextPage defaults (16 / 11).
    if (!snap.valid) return page;

    // The engine lists one glyph per drawn word (spaces included) in typewriter order. Words are matched to that list in
    // order; a space the engine did not draw has no reveal entry.
    const size_t G = snap.glyphs.size();
    const bool have_gl = G > 0;
    size_t g = 0;
    int counter = 0;
    uint8_t col = 0;
    page.chars.reserve(snap.text.size());
    for (uint16_t w : snap.text) {
        if (w == 0) break;
        TextChar c;
        if (w == 0x000A) { c.cp = 0x0A; page.chars.push_back(c); continue; }
        if (w == 0x4079) { col = 1; continue; }   // colour code on
        if (w == 0x4000) { col = 0; continue; }   // colour code off
        const bool is_space = (w == 0x20 || w == 0xA0);
        int entry = -1;
        if (have_gl) {
            if (g < G && (!is_space || snap.glyphs[g].word == w)) {
                entry = static_cast<int>(g);
                c.has_pos = true;
                c.gx = snap.glyphs[g].gx;
                c.gy = snap.glyphs[g].gy;
                ++g;
            }
        } else {
            entry = counter++;
        }
        c.entry = entry;
        if (is_space) {
            c.cp = w;
        } else {
            c.cp = engine_word_to_codepoint(w);   // 0 = not renderable
            c.colour = (have_gl && entry >= 0 && snap.glyphs[entry].color) ? 1 : col;
        }
        page.chars.push_back(c);
    }
    return page;
}

TextSettings& settings() {
    static TextSettings s = [] {
        TextSettings t;
        t.enabled = false;
        t.family = 0;
        t.size_em = 10.0f;
        t.line_spacing = 1.2f;
        t.letter_spacing = 0.0f;
        t.wrap = WrapMode::KeepSourceBreaks;
        t.style = TextStyle::Shadow;
        t.shadow_offset = 0.6f;
        t.outline_width = 0.0f;
        t.text_rgba = 0xFFFFFFFFu;
        t.shadow_rgba = 0x000000B0u;
        t.outline_rgba = 0x000000FFu;
        t.opacity = 1.0f;
        t.box_padding = 0.0f;
        t.auto_fit = true;
        t.min_size_em = 5.0f;
        // Colours of this game's engine text: the fixed warm yellow of the colour-code words, and the brown body / tan
        // shadow used for "match the game" when the page does not report its own palette colours.
        t.highlight_rgba = 0xFFE27AFFu;
        t.fallback_text_rgba = 0x632100FFu;
        t.fallback_shadow_rgba = 0xD09838FFu;
        return t;
    }();
    return s;
}

namespace {

// Embedded OFL fonts (Latin subsets): the three PT families of text_fonts_data.cpp, then the extra families of
// overlay_fonts_data.cpp. size_adjust is the optical correction of the em of that family.
struct Builtin { const char* name; int source; float adjust; };   // source: 0..2 = textfonts::Face, 100+ = extra index
const Builtin kBuiltins[] = {
    {"Humanist Sans", 0, 1.0f},      // PT Sans
    {"Humanist Narrow", 1, 1.0f},    // PT Sans Narrow
    {"Humanist Serif", 2, 1.0f},     // PT Serif
    {"Legible Sans", 100, 1.0f},     // Atkinson Hyperlegible
    {"Rounded Sans", 101, 1.0f},     // Varela Round
    {"Monospace", 102, 1.0f},        // Cousine
    {"Pixel Terminal", 103, 1.2f},   // VT323
    {"Pixel Arcade", 104, 0.72f},    // Press Start 2P
};

void register_builtin_fonts() {
    gbaimp::text::FontLibrary& lib = gbaimp::text::FontLibrary::instance();
    for (const Builtin& b : kBuiltins) {
        const unsigned char* data = nullptr;
        size_t size = 0;
        if (b.source < 100) {
            data = textfonts::face_data(b.source);
            size = textfonts::face_size(b.source);
        } else if (b.source - 100 < extra_face_count()) {
            data = extra_face_data(b.source - 100);
            size = extra_face_size(b.source - 100);
        }
        if (data) lib.add_builtin_font(b.name, data, size, b.adjust);   // never expected to fail: the family is then omitted
    }
}

} // namespace

gbaimp::text::TextRenderer& renderer() {
    static std::once_flag once;
    std::call_once(once, register_builtin_fonts);
    return gbaimp::text::TextRenderer::instance();
}

bool can_draw_page(const PageSnapshot& snap, const DrawInfo& info) {
    return renderer().can_draw(to_text_page(snap), settings(), info);
}

bool draw_page(const PageSnapshot& snap, const DrawInfo& info) {
    return renderer().draw(to_text_page(snap), settings(), info);
}

} // namespace khcom::overlay
