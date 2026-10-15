// Tests of this game's side of the HD dialogue text: the embedded fonts registered with the generic text engine, the
// conversion of the engine's words / PageSnapshot into a gbaimp::text::TextPage, the game's default colours and settings.
// The engine itself (layout, rasteriser, fonts, fades, caches) is tested in gba-improvements.
#include "text_overlay/text_adapter.h"

#include "gbaimp/text_fonts.h"
#include "gbaimp/text_synth.h"

#include <SDL.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace khcom::overlay;

namespace {

int g_fail = 0;
#define CHECK(cond, msg)                                                                   \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ++g_fail;                                                                      \
            std::printf("  [FAIL] %s (%s:%d)\n", std::string(msg).c_str(), __FILE__, __LINE__); \
        }                                                                                  \
    } while (0)

PageSnapshot snapshot(const std::vector<std::string>& lines, bool glyphs = true) {
    const gbaimp::text::TextPage t = gbaimp::text::synth_page(lines, 72.0f, 108.0f, glyphs);
    PageSnapshot p;
    p.valid = true;
    p.page_id = 1;
    p.origin_x = 72.0f;
    p.origin_y = 108.0f;
    p.wrap_px = 155;
    p.line_pitch = 12;
    p.max_lines = 3;
    for (const gbaimp::text::TextChar& c : t.chars) {
        p.text.push_back(static_cast<uint16_t>(c.cp));
        if (c.has_pos) {
            PageGlyph g;
            g.word = static_cast<uint16_t>(c.cp);
            g.gx = c.gx;
            g.gy = c.gy;
            p.glyphs.push_back(g);
        }
    }
    p.revealed = glyphs ? static_cast<int>(p.glyphs.size()) : 0;
    return p;
}

// Offscreen software canvas: draws one page and sums the colour channels of what was drawn on black.
struct Canvas {
    SDL_Surface* surf = nullptr;
    SDL_Renderer* ren = nullptr;
    int w = 960, h = 640;
    Canvas() {
        surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surf) ren = SDL_CreateSoftwareRenderer(surf);
    }
    ~Canvas() {
        if (ren) SDL_DestroyRenderer(ren);
        if (surf) SDL_FreeSurface(surf);
    }
    DrawInfo info() const {
        DrawInfo d;
        d.renderer = ren;
        d.dst_x = 0; d.dst_y = 0; d.dst_w = w; d.dst_h = h;
        d.gba_w = 240; d.gba_h = 160;
        return d;
    }
    void sums(long& r, long& g, long& b) {
        r = g = b = 0;
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
        const uint32_t* px = static_cast<const uint32_t*>(surf->pixels);
        for (int i = 0; i < w * h; ++i) {
            r += (px[i] >> 16) & 0xFF;
            g += (px[i] >> 8) & 0xFF;
            b += px[i] & 0xFF;
        }
    }
    void clear() {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
    }
};

void test_fonts() {
    gbaimp::text::FontLibrary::instance().set_user_font_dir("");   // only the embedded families
    const std::vector<std::string> names = renderer().family_names();
    const char* expected[] = {"Humanist Sans", "Humanist Narrow", "Humanist Serif", "Legible Sans",
                              "Rounded Sans", "Monospace", "Pixel Terminal", "Pixel Arcade"};
    CHECK(names.size() == 8, "eight embedded families");
    for (size_t i = 0; i < 8 && i < names.size(); ++i) CHECK(names[i] == expected[i], std::string("family name ") + expected[i]);
    gbaimp::text::FontLibrary& lib = gbaimp::text::FontLibrary::instance();
    CHECK(lib.builtin_count() == 8, "all eight are built-in");
    const gbaimp::text::FontFace* terminal = lib.face(6);
    const gbaimp::text::FontFace* arcade = lib.face(7);
    CHECK(terminal && terminal->size_adjust == 1.2f, "Pixel Terminal size adjust");
    CHECK(arcade && arcade->size_adjust == 0.72f, "Pixel Arcade size adjust");
    for (int i = 0; i < 8; ++i) {
        const gbaimp::text::FontFace* f = lib.face(i);
        CHECK(f && f->valid(), "face is usable");
        int missing = 0;
        for (uint32_t cp : {0x2019u, 0x201Cu, 0x201Du, 0x2026u, 0xE9u, uint32_t('A'), uint32_t('z')})
            if (f && f->glyph_index(cp) == 0) ++missing;
        CHECK(missing == 0, "the Latin subset has the characters the dialogue uses");
    }
}

void test_words_and_pages() {
    CHECK(engine_word_to_codepoint('A') == 'A' && engine_word_to_codepoint(0xE9) == 0xE9, "ASCII and Latin-1 words");
    CHECK(engine_word_to_codepoint(0x92) == 0x2019 && engine_word_to_codepoint(0x85) == 0x2026, "Windows-1252 punctuation");
    CHECK(engine_word_to_codepoint(0xE000) == 0x2026, "the pause mark is an ellipsis");
    CHECK(engine_word_to_codepoint(0x81) == 0 && engine_word_to_codepoint(0x0A) == 0 && engine_word_to_codepoint(0x4079) == 0,
          "undefined words and control words are not renderable");

    // A normal page: one char per word, entries follow the engine glyph list (spaces included).
    const PageSnapshot s = snapshot({"One look", "at this"});
    const gbaimp::text::TextPage p = to_text_page(s);
    CHECK(p.valid && p.chars.size() == s.text.size(), "every word becomes a character");
    int entries = 0;
    for (const auto& c : p.chars) {
        if (c.cp == 0x0A) { CHECK(c.entry == -1 && !c.has_pos, "a line break has no entry"); continue; }
        CHECK(c.has_pos && c.entry == entries, "entries are consecutive in typewriter order");
        ++entries;
    }
    CHECK(entries == static_cast<int>(s.glyphs.size()), "one entry per engine glyph");
    CHECK(p.revealed == s.revealed && p.page_id == s.page_id && p.wrap_px == 155 && p.line_pitch == 12 && p.max_lines == 3,
          "geometry is copied");
    CHECK(p.cell_height == 16 && p.baseline_row == 11, "the glyph cell of the game's font");

    // No glyph list: entries are numbered here, nothing has a position.
    const gbaimp::text::TextPage q = to_text_page(snapshot({"No glyphs"}, false));
    CHECK(!q.chars.empty() && !q.chars[0].has_pos && q.chars[0].entry == 0 && q.chars[4].entry == 4, "no glyph list: numbered entries");

    // Colour-code words are folded into the colour of the glyphs after them.
    PageSnapshot c;
    c.valid = true;
    c.text = {'A', 0x4079, 'B', 'C', 0x4000, 'D', 0};
    const gbaimp::text::TextPage cp = to_text_page(c);
    CHECK(cp.chars.size() == 4 && cp.chars[0].colour == 0 && cp.chars[1].colour == 1 && cp.chars[2].colour == 1 && cp.chars[3].colour == 0,
          "colour code on / off folded into the glyph colour");

    // A glyph flagged with a colour by the engine is highlighted even without the words.
    PageSnapshot f = snapshot({"AB"});
    f.glyphs[1].color = 1;
    const gbaimp::text::TextPage fp = to_text_page(f);
    CHECK(fp.chars[0].colour == 0 && fp.chars[1].colour == 1, "engine colour flag of a glyph");

    // A space the engine did not draw has no reveal entry; unsupported words cannot be drawn.
    PageSnapshot sp;
    sp.valid = true;
    sp.text = {'A', ' ', 'B'};
    PageGlyph ga, gb;
    ga.word = 'A';
    gb.word = 'B';
    sp.glyphs = {ga, gb};
    const gbaimp::text::TextPage spp = to_text_page(sp);
    CHECK(spp.chars[1].cp == ' ' && spp.chars[1].entry == -1 && spp.chars[2].entry == 1, "an undrawn space is not a reveal entry");
    PageSnapshot bad = snapshot({"AB"});
    bad.text[1] = 0x81;
    CHECK(to_text_page(bad).chars[1].cp == 0, "an undefined word is not renderable");

    PageSnapshot invalid = snapshot({"AB"});
    invalid.valid = false;
    CHECK(!to_text_page(invalid).valid && to_text_page(invalid).chars.empty(), "an invalid snapshot gives an empty invalid page");
}

void test_settings_and_colours() {
    const TextSettings& s = settings();
    CHECK(!s.enabled, "the overlay is off by default");
    CHECK(s.size_em == 10.0f && s.line_spacing == 1.2f && s.shadow_offset == 0.6f && s.outline_width == 0.0f &&
          s.min_size_em == 5.0f && s.auto_fit && s.style == TextStyle::Shadow && s.wrap == WrapMode::KeepSourceBreaks,
          "the game's default settings");
    CHECK(s.highlight_rgba == 0xFFE27AFFu && s.fallback_text_rgba == 0x632100FFu && s.fallback_shadow_rgba == 0xD09838FFu,
          "the game's highlight and 'match the game' fallback colours");

    Canvas cv;
    CHECK(cv.ren != nullptr, "software renderer available");
    if (!cv.ren) return;
    TextSettings& st = settings();
    const TextSettings saved = st;
    st.enabled = true;
    st.style = TextStyle::Plain;
    st.size_em = 10.0f;
    long r, g, b;

    PageSnapshot page = snapshot({"Hello there"});
    CHECK(can_draw_page(page, cv.info()), "a plain page can be drawn");
    st.enabled = false;
    CHECK(!can_draw_page(page, cv.info()), "disabled settings: can_draw is false");
    st.enabled = true;

    // text_rgba 0 = match the colour the engine draws the page with.
    st.text_rgba = 0;
    page.engine_text_rgba = 0x00FF00FFu;
    cv.clear();
    CHECK(draw_page(page, cv.info()), "page draws");
    cv.sums(r, g, b);
    CHECK(g > 0 && r == 0 && b == 0, "text_rgba 0 matches the engine text colour");

    // Unknown engine colour: the game's brown.
    page.engine_text_rgba = 0;
    cv.clear();
    draw_page(page, cv.info());
    cv.sums(r, g, b);
    CHECK(r > 0 && g > 0 && b == 0 && r > g, "unknown engine colour falls back to the game's brown");

    // Colour-code glyphs: the fixed warm yellow, or the engine's own highlight colour.
    st.text_rgba = 0xFF0000FFu;
    PageSnapshot hl = snapshot({"AB"});
    hl.glyphs[0].color = 1;
    hl.glyphs[1].color = 1;
    cv.clear();
    draw_page(hl, cv.info());
    cv.sums(r, g, b);
    CHECK(r > 0 && g > 0, "default highlight is the warm yellow");
    hl.engine_highlight_rgba = 0x0000FFFFu;
    cv.clear();
    draw_page(hl, cv.info());
    cv.sums(r, g, b);
    CHECK(g == 0 && b > 0, "the engine's highlight colour wins");

    st = saved;
    renderer().release_gpu();
}

} // namespace

int run_text_adapter_tests(bool verbose) {
    g_fail = 0;
    test_fonts();
    test_words_and_pages();
    test_settings_and_colours();
    if (verbose || g_fail) std::printf("text adapter tests: %d failure(s)\n", g_fail);
    return g_fail;
}
