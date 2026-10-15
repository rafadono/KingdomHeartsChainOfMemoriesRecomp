// Tests of the HD dialogue text overlay (render side): layout, fit, reveal, caches, fonts, drawing.
// Drawing uses SDL's software renderer on an offscreen surface (no window / GPU needed).
// Built into khcom_unit_tests; also see docs/TEXT_OVERLAY_RENDER.md.
#undef NDEBUG
#include "text_overlay/overlay_fonts.h"
#include "text_overlay/overlay_layout.h"
#include "text_overlay/overlay_raster.h"
#include "text_overlay/overlay_synth.h"
#include "text_overlay/overlay_types.h"
#include "text_overlay/overlay_fade.h"
#include "gbaimp/capture_harness.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace khcom::overlay;

namespace {

int g_fail = 0;
#define CHECK(cond, msg)                                                                   \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "[FAIL] overlay: " << msg << " (" #cond ") line " << __LINE__ << std::endl; \
            ++g_fail;                                                                      \
        }                                                                                  \
    } while (0)

OverlaySettings base_settings() {
    OverlaySettings s;
    s.enabled = true;
    s.outline_width = 0.8f;
    return s;
}

DrawInfo info_for(float scale, SDL_Renderer* r = nullptr, int ox = 0, int oy = 0) {
    DrawInfo i;
    i.renderer = r;
    i.dst_x = ox;
    i.dst_y = oy;
    i.dst_w = static_cast<int>(240 * scale);
    i.dst_h = static_cast<int>(160 * scale);
    return i;
}

// Text of a layout line as a string (code points < 256 only).
std::string line_text(const PageLayout& L, int li) {
    std::string s;
    const LayoutLine& ln = L.lines[li];
    for (int i = ln.first; i < ln.first + ln.count; ++i) s.push_back(static_cast<char>(L.items[i].cp < 256 ? L.items[i].cp : '?'));
    return s;
}

std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\n') { if (!cur.empty()) w.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

struct Canvas {
    SDL_Surface* surf = nullptr;
    SDL_Renderer* ren = nullptr;
    int w = 0, h = 0;
    Canvas(int W, int H) : w(W), h(H) {
        surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surf) ren = SDL_CreateSoftwareRenderer(surf);
        clear();
    }
    ~Canvas() {
        OverlayRenderer::instance().release_gpu();
        if (ren) SDL_DestroyRenderer(ren);
        if (surf) SDL_FreeSurface(surf);
    }
    void clear() {
        if (!ren) return;
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
    }
    // bounding box and count of pixels that are not black
    bool ink(int& x0, int& y0, int& x1, int& y1, long& count, const SDL_Rect* region = nullptr) {
        std::vector<uint32_t> px(static_cast<size_t>(w) * h);
        if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4) != 0) return false;
        x0 = w; y0 = h; x1 = -1; y1 = -1; count = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (region && (x < region->x || y < region->y || x >= region->x + region->w || y >= region->y + region->h)) continue;
                if (px[static_cast<size_t>(y) * w + x] & 0x00FFFFFFu) {
                    x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); ++count;
                }
            }
        return true;
    }
    std::vector<uint32_t> pixels() {
        std::vector<uint32_t> px(static_cast<size_t>(w) * h);
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4);
        return px;
    }
};

void test_fonts(bool verbose) {
    OverlayRenderer& r = OverlayRenderer::instance();
    const std::vector<std::string> names = r.family_names();
    CHECK(names.size() >= 8, "at least 8 built-in families");
    CHECK(names[0] == "Humanist Sans", "first family is the default sans");
    FontLibrary& lib = FontLibrary::instance();
    for (int f = 0; f < lib.builtin_count(); ++f) {
        const FontFace* face = lib.face(f);
        CHECK(face && face->valid(), "face valid: " + names[f]);
        if (!face) continue;
        int missing = 0;
        for (int cp = 33; cp < 127; ++cp) {
            const int gi = face->glyph_index(static_cast<uint32_t>(cp));
            GlyphBitmap bm;
            if (!gi || !rasterize_glyph(*face, gi, 40.0f, 0, GlyphVariant::Body, 0.0f, bm)) { ++missing; continue; }
            long ink = 0;
            for (uint8_t a : bm.a) ink += a;
            if (ink == 0) ++missing;
        }
        CHECK(missing == 0, "every printable ASCII char renders with ink in " + names[f]);
        // the typographic specials used by the script
        for (uint32_t cp : {0x2019u, 0x201Cu, 0x201Du, 0x2026u, 0xE9u}) CHECK(face->glyph_index(cp) != 0, "special glyph in " + names[f]);
        if (verbose) std::cout << "  family " << f << " " << names[f] << " upem " << face->upem << " xh " << face->xheight << std::endl;
    }
}

void test_layout_keep_and_reveal() {
    OverlaySettings st = base_settings();
    PageSnapshot p = synth_page({"One look at this castle, and I", "just knew! They're here."});
    PageLayout L;
    CHECK(layout_page(p, st, 5.4f, 5.4f, L), "keep layout ok");
    CHECK(L.lines.size() == 2, "keep: same number of lines as the engine");
    CHECK(line_text(L, 0) == "Onelookatthiscastle,andI", "keep: first line words");
    CHECK(L.entry_count == static_cast<int>(p.glyphs.size()), "entry count equals engine glyph entries");
    // entries of items follow the engine glyph order (spaces are entries but not items)
    size_t gi = 0;
    bool entries_ok = true;
    for (const LayoutItem& it : L.items) {
        while (gi < p.glyphs.size() && p.glyphs[gi].word == ' ') ++gi;
        if (gi >= p.glyphs.size() || it.entry != static_cast<int>(gi) || p.glyphs[gi].word != it.cp) { entries_ok = false; break; }
        ++gi;
    }
    CHECK(entries_ok, "item entries map to engine glyph indices in typewriter order");

    // line structure from the glyph rows alone (no newline words in the text)
    PageSnapshot q = p;
    q.text.erase(std::remove(q.text.begin(), q.text.end(), static_cast<uint16_t>(0x0A)), q.text.end());
    // without the newline word the engine rows (gy) still say two lines; the line break needs a space: keep text consistent
    for (size_t i = 0; i < q.text.size(); ++i) if (q.text[i] == 'I' && i + 1 < q.text.size() && q.text[i + 1] == 'j') { q.text.insert(q.text.begin() + i + 1, ' '); break; }
    q.glyphs.insert(q.glyphs.begin() + 30, PageGlyph{' ', 0, 108.0f, 0});
    PageLayout L2;
    CHECK(layout_page(q, st, 4.0f, 4.0f, L2) && L2.lines.size() == 2, "keep: line structure taken from glyph rows");

    // 3-line page keeps 3 lines at every scale and size where it fits
    PageSnapshot p3 = synth_page({"I've been looking for you,", "Sora. The door to this place", "has been sealed for so long..."});
    for (float sc : {3.0f, 4.0f, 5.4f, 6.19f})
        for (float em : {6.0f, 8.0f, 10.0f, 12.0f}) {
            OverlaySettings s2 = st;
            s2.size_em = em;
            PageLayout l3;
            if (layout_page(p3, s2, sc, sc, l3)) CHECK(l3.lines.size() == 3, "keep never changes the line count");
        }

    // reveal prefix: draw with increasing `revealed`; ink must grow monotonically and be a subset of the full page
    Canvas cv(static_cast<int>(240 * 4.0f), static_cast<int>(160 * 4.0f));
    CHECK(cv.ren != nullptr, "software renderer available");
    if (!cv.ren) return;
    DrawInfo info = info_for(4.0f, cv.ren);
    PageSnapshot full = p;
    full.revealed = static_cast<int>(full.glyphs.size());
    OverlayRenderer& r = OverlayRenderer::instance();
    cv.clear();
    CHECK(r.draw(full, st, info), "full page draws");
    std::vector<uint32_t> full_px = cv.pixels();
    long prev = 0;
    for (int n : {0, 1, 5, 12, 20, 33, 40, static_cast<int>(full.glyphs.size())}) {
        PageSnapshot pr = full;
        pr.revealed = n;
        cv.clear();
        const bool drawn = r.draw(pr, st, info);
        CHECK(drawn == (n > 0), "draw returns true iff something is revealed");
        int x0, y0, x1, y1;
        long cnt;
        cv.ink(x0, y0, x1, y1, cnt);
        CHECK(cnt >= prev, "ink grows with the typewriter");
        prev = cnt;
        const std::vector<uint32_t> px = cv.pixels();
        long outside = 0;
        for (size_t i = 0; i < px.size(); ++i) if ((px[i] & 0xFFFFFF) && !(full_px[i] & 0xFFFFFF)) ++outside;
        CHECK(outside == 0, "revealed prefix is a subset of the full page");
    }
}

void test_reflow_and_fit() {
    OverlaySettings st = base_settings();
    st.wrap = WrapMode::Reflow;
    const std::string text = "One look at this castle, and I just knew! They're here. Sora, Donald and Goofy were waiting together";
    PageSnapshot p = synth_page({text});
    const std::vector<std::string> expect = split_words(text);
    for (float sc : {3.0f, 4.0f, 5.4f, 6.19f})
        for (float em : {6.0f, 7.0f, 8.0f, 10.0f, 12.0f, 14.0f}) {
            OverlaySettings s2 = st;
            s2.size_em = em;
            s2.min_size_em = 5.0f;
            PageLayout L;
            if (!layout_page(p, s2, sc, sc, L)) continue;
            std::vector<std::string> got;
            for (size_t li = 0; li < L.lines.size(); ++li) {
                // words never split across lines: rebuild words from items + gaps
                const LayoutLine& ln = L.lines[li];
                std::string cur;
                float last_end = -1;
                (void)last_end;
                for (int i = ln.first; i < ln.first + ln.count; ++i) cur.push_back(static_cast<char>(L.items[i].cp));
                got.push_back(cur);
                CHECK(ln.x0 >= L.area_l - 0.01f, "line starts inside the area");
                CHECK(ln.x1 <= L.area_r + 0.5f, "line ends inside the area (no overflow to the right)");
            }
            CHECK(L.ink_t >= L.area_t - 0.01f && L.ink_b <= L.area_b + 0.01f, "page ink inside the text area vertically");
            // glue lines: every original word must appear whole, in order
            std::string joined;
            for (const std::string& g : got) joined += g;
            std::string orig;
            for (const std::string& w : expect) orig += w;
            CHECK(joined == orig, "reflow keeps every letter in order");
            // a break only happens between words: the first letter of a line follows a space in the source
            size_t pos = 0;
            for (size_t li = 0; li + 1 < got.size(); ++li) {
                pos += got[li].size();
                // find the word boundary in the original at this letter offset
                size_t acc = 0;
                bool boundary = false;
                for (const std::string& w : expect) { acc += w.size(); if (acc == pos) boundary = true; }
                CHECK(boundary, "no word is broken across lines");
            }
        }
    // auto-fit
    OverlaySettings big = st;
    big.size_em = 30.0f;
    big.min_size_em = 5.0f;
    PageLayout L;
    CHECK(layout_page(p, big, 5.4f, 5.4f, L) && L.shrunk && L.em_gba < 30.0f && L.em_gba >= 5.0f, "auto-fit shrinks a huge size to fit");
    big.min_size_em = 29.0f;
    CHECK(!layout_page(p, big, 5.4f, 5.4f, L), "auto-fit cannot fit above the minimum -> can_draw false");
    big.auto_fit = false;
    CHECK(layout_page(p, big, 5.4f, 5.4f, L) && !L.shrunk && L.em_gba == 30.0f, "auto-fit off keeps the requested size");
    // orphan avoidance: a page whose last line would be one short word
    OverlaySettings o = st;
    PageSnapshot po = synth_page({"The keyblade chooses its own master wisely and"});
    PageLayout LO;
    if (layout_page(po, o, 4.0f, 4.0f, LO) && LO.lines.size() >= 2) CHECK(LO.lines.back().words >= 1, "reflow produced a last line");
    // paragraph break (blank line) is honoured
    PageSnapshot pp = synth_page({"First part.", "", "Second part."});
    PageLayout LP;
    CHECK(layout_page(pp, o, 4.0f, 4.0f, LP) && LP.lines.size() == 2, "blank line keeps paragraphs apart in Reflow");
}

void test_unsupported_and_words() {
    OverlaySettings st = base_settings();
    DrawInfo info = info_for(4.0f);
    OverlayRenderer& r = OverlayRenderer::instance();
    PageSnapshot p = synth_page({"Hello world"});
    CHECK(r.can_draw(p, st, info), "plain ASCII page can be drawn");
    OverlaySettings off = st;
    off.enabled = false;
    CHECK(!r.can_draw(p, off, info), "disabled settings -> can_draw false");
    PageSnapshot bad = p;
    bad.text.push_back(0x3042);   // Shift-JIS / wide word
    bad.glyphs.push_back(PageGlyph{0x3042, 0, 0, 0});
    CHECK(!r.can_draw(bad, st, info), "unsupported word -> can_draw false");
    PageSnapshot undef = p;
    undef.text.push_back(0x81);   // undefined in Windows-1252
    CHECK(!r.can_draw(undef, st, info), "undefined cp1252 word -> can_draw false");
    PageSnapshot special = synth_page({"Wait\x85 it\x92s \x93quoted\x94 \xE9"});
    special.text.push_back(0xE000);   // pause glyph
    CHECK(r.can_draw(special, st, info), "ellipsis, apostrophe, quotes, accents and the pause glyph render");
    PageSnapshot invalid = p;
    invalid.valid = false;
    CHECK(!r.can_draw(invalid, st, info), "invalid snapshot -> can_draw false");
    CHECK(engine_word_to_codepoint(0xE000) == 0x2026 && engine_word_to_codepoint(0x92) == 0x2019 && engine_word_to_codepoint(0x0A) == 0,
          "engine word mapping");
    // colour code words toggle the highlight and are not entries
    PageSnapshot col = synth_page({"ab"});
    col.text.insert(col.text.begin(), 0x4079);
    col.text.push_back(0x4000);
    PageLayout L;
    CHECK(layout_page(col, st, 4.0f, 4.0f, L) && L.items.size() == 2 && L.items[0].colour == 1, "colour code highlights the following glyphs");
}

void test_drawing_and_caches() {
    OverlayRenderer& r = OverlayRenderer::instance();
    OverlaySettings st = base_settings();
    PageSnapshot p = synth_page({"One look at this castle, and I", "just knew! They're here."});
    for (float sc : {3.0f, 5.4f, 6.19f}) {
        Canvas cv(static_cast<int>(240 * sc) + 8, static_cast<int>(160 * sc) + 8);
        if (!cv.ren) { CHECK(false, "canvas"); return; }
        for (int style = 0; style < 3; ++style)
            for (int wrap = 0; wrap < 2; ++wrap) {
                OverlaySettings s = st;
                s.style = static_cast<TextStyle>(style);
                s.wrap = static_cast<WrapMode>(wrap);
                DrawInfo info = info_for(sc, cv.ren, 4, 4);
                cv.clear();
                CHECK(r.draw(p, s, info), "page draws");
                int x0, y0, x1, y1;
                long cnt;
                cv.ink(x0, y0, x1, y1, cnt);
                CHECK(cnt > 200, "ink was drawn");
                // everything inside the text area (+ shadow/outline slack), offset by the dst origin
                const float lx = (72.0f - 1.0f) * sc + 4, rx = (72.0f + 155.0f + 1.0f) * sc + 4;
                const float ty = (108.0f - 1.0f) * sc + 4, by = (108.0f + 40.0f + 1.0f) * sc + 4;
                CHECK(x0 >= lx - 1 && x1 <= rx + 1 && y0 >= ty - 1 && y1 <= by + 1, "ink stays inside the text box bounds");
            }
    }
    // caches: identical draws do no layout / raster work; scale and setting changes rebuild
    Canvas cv(240 * 4, 160 * 4);
    DrawInfo info = info_for(4.0f, cv.ren);
    r.invalidate();
    cv.clear();
    r.draw(p, st, info);
    const uint64_t lb = r.layout_builds(), gr = r.glyphs_rasterized(), fl = r.atlas_flushes();
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 60; ++i) { cv.clear(); r.draw(p, st, info); }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.layout_builds() == lb && r.glyphs_rasterized() == gr && r.atlas_flushes() == fl, "warm draws allocate/rasterise nothing");
    CHECK(ms / 60.0 < 40.0, "warm draw is fast even on the software renderer");
    OverlaySettings s2 = st;
    s2.size_em = 9.0f;
    r.draw(p, s2, info);
    CHECK(r.layout_builds() == lb + 1, "size change rebuilds the layout");
    DrawInfo info2 = info_for(5.4f, cv.ren);
    r.draw(p, s2, info2);
    CHECK(r.layout_builds() == lb + 2, "scale change rebuilds the layout");
    CHECK(r.atlas_flushes() > fl, "scale change flushes the glyph atlas");
    const uint64_t fl2 = r.atlas_flushes();
    r.invalidate();
    r.draw(p, s2, info2);
    CHECK(r.atlas_flushes() > fl2, "invalidate() drops the atlas");
    // fade / opacity: half alpha is dimmer than full
    cv.clear();
    r.draw(p, st, info);
    int a0, b0, c0, d0;
    long full_cnt;
    cv.ink(a0, b0, c0, d0, full_cnt);
    const std::vector<uint32_t> full_px = cv.pixels();
    cv.clear();
    DrawInfo faded = info;
    faded.global_alpha = 0.5f;
    r.draw(p, st, faded);
    const std::vector<uint32_t> half_px = cv.pixels();
    uint64_t sf = 0, sh = 0;
    for (size_t i = 0; i < full_px.size(); ++i) { sf += (full_px[i] >> 8) & 0xFF; sh += (half_px[i] >> 8) & 0xFF; }
    CHECK(sh > 0 && sh < sf * 3 / 4, "global_alpha dims the text");
    cv.clear();
    CHECK(!r.draw(p, st, [&] { DrawInfo d = info; d.global_alpha = 0.0f; return d; }()), "fully faded draws nothing");

    // brightness fades act on the colours: darken pulls white text towards black, brighten pulls dark text towards white
    auto green_sum = [&](const OverlaySettings& s, const DrawInfo& d) {
        cv.clear();
        r.draw(p, s, d);
        uint64_t g = 0;
        for (uint32_t px : cv.pixels()) g += (px >> 8) & 0xFF;
        return g;
    };
    OverlaySettings plain = st;
    plain.style = TextStyle::Plain;
    DrawInfo dk = info;
    dk.fade_mode = static_cast<int>(FadeMode::Darken);
    dk.fade_level = 0.5f;
    const uint64_t g_full = green_sum(plain, info), g_dark = green_sum(plain, dk);
    CHECK(g_dark > 0 && g_dark < g_full * 3 / 4, "darken fade dims the text colour");
    dk.fade_level = 1.0f;
    CHECK(green_sum(plain, dk) == 0, "full darken fade makes white text black");
    OverlaySettings dim = plain;
    dim.text_rgba = 0x404040FFu;
    DrawInfo br = info;
    const uint64_t g_dim = green_sum(dim, br);
    br.fade_mode = static_cast<int>(FadeMode::Brighten);
    br.fade_level = 1.0f;
    const uint64_t g_bright = green_sum(dim, br);
    CHECK(g_bright > g_dim * 2, "brighten fade pulls dark text towards white");
    CHECK(g_bright >= g_full * 9 / 10, "full brighten fade makes the text white");

    // match-game colours: colour 0 draws with the colour the snapshot says the engine uses (here pure green), the
    // fallback is the engine's brown when unknown; a palette-derived fade is not applied twice to matched colours
    OverlaySettings match = plain;
    match.text_rgba = 0;
    PageSnapshot pg = p;
    pg.engine_text_rgba = 0x00FF00FFu;
    cv.clear();
    r.draw(pg, match, info);
    uint64_t rs = 0, gs = 0;
    for (uint32_t px : cv.pixels()) { rs += (px >> 16) & 0xFF; gs += (px >> 8) & 0xFF; }
    CHECK(gs > 0 && rs == 0, "text_rgba 0 matches the engine text colour");
    pg.engine_text_rgba = 0;
    cv.clear();
    r.draw(pg, match, info);
    gs = 0;
    for (uint32_t px : cv.pixels()) gs += (px >> 8) & 0xFF;
    CHECK(gs > 0, "unknown engine colour falls back to the engine's brown");
    pg.engine_text_rgba = 0x00FF00FFu;
    DrawInfo pf = info;
    pf.fade_mode = static_cast<int>(FadeMode::Darken);
    pf.fade_level = 1.0f;
    pf.fade_from_palette = true;
    cv.clear();
    r.draw(pg, match, pf);
    gs = 0;
    for (uint32_t px : cv.pixels()) gs += (px >> 8) & 0xFF;
    CHECK(gs > 0, "palette-derived fade does not darken matched colours twice");
    pf.fade_from_palette = false;
    cv.clear();
    r.draw(pg, match, pf);
    gs = 0;
    for (uint32_t px : cv.pixels()) gs += (px >> 8) & 0xFF;
    CHECK(gs == 0, "register fade darkens matched colours");
}

// Battle message boxes (docs/TEXT_OVERLAY_ENGINE.md "Battle dialogue"): lines the engine indents with spaces (centred tutorial
// boxes), the engine's colour for colour-code words, and a 16:9 image (gba_w 284) with the text offset inside it.
void test_battle_additions() {
    OverlaySettings st = base_settings();
    const std::vector<std::string> lines = {"       B Button:  Jump", "Tap the +Control Pad Left or", "   Right twice:  Dodge Roll"};
    PageSnapshot p = synth_page(lines, 46.0f, 12.0f);   // engine grid: 5.5 px per character
    PageLayout L;
    CHECK(layout_page(p, st, 4.0f, 4.0f, L) && L.ok && L.lines.size() == 3, "indented page lays out in keep mode");
    if (L.lines.size() == 3) {
        const float base = L.lines[1].x0;                // the line without leading spaces starts at the text area
        CHECK(std::fabs(base - L.area_l) < 0.01f, "unindented line starts at the area's left edge");
        CHECK(std::fabs((L.lines[0].x0 - base) - 7 * 5.5f * 4.0f) < 0.05f, "keep: 7 leading spaces become the engine's indent");
        CHECK(std::fabs((L.lines[2].x0 - base) - 3 * 5.5f * 4.0f) < 0.05f, "keep: 3 leading spaces become the engine's indent");
        CHECK(line_text(L, 0) == "BButton:Jump" && line_text(L, 2) == "Righttwice:DodgeRoll", "leading spaces are not drawn as glyphs");
    }
    // reflow ignores the indent (the page is re-wrapped)
    OverlaySettings rf = st;
    rf.wrap = WrapMode::Reflow;
    PageLayout R;
    CHECK(layout_page(p, rf, 4.0f, 4.0f, R) && R.ok, "indented page lays out in reflow mode");
    for (const LayoutLine& ln : R.lines) CHECK(std::fabs(ln.x0 - R.area_l) < 0.01f, "reflow: lines start at the area's left edge");
    // an ordinary page has no indent at all (story pages must lay out exactly as before)
    PageSnapshot q = synth_page({"One look at this castle, and I", "just knew! They're here."}, 72.0f, 108.0f);
    PageLayout Q;
    CHECK(layout_page(q, st, 5.4f, 5.4f, Q) && Q.lines.size() == 2, "ordinary page");
    for (const LayoutLine& ln : Q.lines) CHECK(std::fabs(ln.x0 - Q.area_l) < 0.01f, "ordinary lines are not indented");
    // an indent that does not fit shrinks the text instead of overflowing
    PageSnapshot wide = synth_page({"              Fit this text please now"}, 46.0f, 12.0f);
    PageLayout W;
    OverlaySettings fit = st;
    fit.size_em = 14.0f;
    CHECK(layout_page(wide, fit, 4.0f, 4.0f, W) && W.shrunk, "indent + text wider than the area shrinks the size");

    // engine colour for colour-code words, in the 16:9 geometry (gba_w 284, text offset by the 22 px margin)
    OverlayRenderer& r = OverlayRenderer::instance();
    OverlaySettings plain = base_settings();
    plain.style = TextStyle::Plain;
    plain.text_rgba = 0xFF0000FFu;                    // explicit red text
    PageSnapshot h = synth_page({"ab cd"}, 22.0f + 16.0f, 12.0f);
    h.source = 1;
    h.glyphs[3].color = 1;
    h.glyphs[4].color = 1;                           // "cd" are colour-code words
    Canvas cv(284 * 4, 160 * 4);
    if (!cv.ren) { CHECK(false, "canvas"); return; }
    DrawInfo info;
    info.renderer = cv.ren;
    info.dst_w = 284 * 4;
    info.dst_h = 160 * 4;
    info.gba_w = 284;
    cv.clear();
    h.engine_highlight_rgba = 0;
    CHECK(r.draw(h, plain, info), "battle page draws with the default highlight");
    uint64_t gs_default = 0, rs = 0;
    for (uint32_t px : cv.pixels()) { gs_default += (px >> 8) & 0xFF; rs += (px >> 16) & 0xFF; }
    CHECK(gs_default > 0 && rs > 0, "default highlight is the fixed warm yellow (has green), the plain text is red");
    r.invalidate();
    cv.clear();
    h.engine_highlight_rgba = 0x0000FFFFu;           // the engine draws its colour-code words in pure blue
    CHECK(r.draw(h, plain, info), "battle page draws with the engine highlight colour");
    uint64_t gs = 0, bs = 0;
    for (uint32_t px : cv.pixels()) { gs += (px >> 8) & 0xFF; bs += px & 0xFF; }
    CHECK(gs == 0 && bs > 0, "colour-code words use the engine's own colour (blue), nothing else has green");
    // the text sits at the offset position of the 284 wide image: ink starts right of the origin, nowhere near x = 0
    int x0, y0, x1, y1;
    long cnt;
    cv.ink(x0, y0, x1, y1, cnt);
    CHECK(x0 >= (22 + 16 - 1) * 4 && x1 <= (22 + 16 + 155 + 1) * 4, "text is placed in the 284 px wide frame (origin_x includes the margin)");
    // the palette-derived fade is not applied twice to the engine's (already faded) highlight colour
    DrawInfo pf = info;
    pf.fade_mode = static_cast<int>(FadeMode::Darken);
    pf.fade_level = 1.0f;
    pf.fade_from_palette = true;
    cv.clear();
    r.draw(h, plain, pf);
    uint64_t bs2 = 0;
    for (uint32_t px : cv.pixels()) bs2 += px & 0xFF;
    CHECK(bs2 > 0, "palette-derived fade leaves the engine highlight colour as it is");
    pf.fade_from_palette = false;
    cv.clear();
    r.draw(h, plain, pf);
    bs2 = 0;
    for (uint32_t px : cv.pixels()) bs2 += px & 0xFF;
    CHECK(bs2 == 0, "a register fade darkens the highlight colour");
}

void test_user_fonts_and_validation() {
    namespace fs = std::filesystem;
    FontLibrary& lib = FontLibrary::instance();
    const int builtin = lib.builtin_count();
    const fs::path tmp = fs::temp_directory_path() / "khcom_overlay_fonts_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    // a valid font: one of the repo's subsets (tests run from the repo root); skip when not found
    const char* cands[] = {"third_party/fonts/PTSans-Regular.subset.ttf", "../third_party/fonts/PTSans-Regular.subset.ttf"};
    std::vector<uint8_t> good;
    for (const char* c : cands) {
        std::ifstream f(c, std::ios::binary);
        if (f) { good.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); break; }
    }
    if (good.empty()) { std::cout << "  (user font test skipped: subset ttf not found from cwd)" << std::endl; return; }
    CHECK(validate_truetype(good.data(), good.size()), "a real font validates");
    auto write = [&](const char* name, const std::vector<uint8_t>& d) {
        std::ofstream o(tmp / name, std::ios::binary);
        o.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    };
    write("MyFont.ttf", good);
    write("Broken.ttf", std::vector<uint8_t>(good.begin(), good.begin() + good.size() / 2));
    write("Garbage.ttf", std::vector<uint8_t>(2000, 0x5A));
    write("notafont.txt", good);
    lib.set_user_font_dir(tmp.string());
    CHECK(lib.family_count() == builtin + 1, "only the valid .ttf is listed");
    CHECK(lib.family_name(builtin) == "MyFont (fonts/)", "user font name from the file name");
    OverlayRenderer& r = OverlayRenderer::instance();
    const std::vector<std::string> names = r.family_names();
    CHECK(names.size() == static_cast<size_t>(builtin + 1) && names.front() == "Humanist Sans", "built-ins first, then user fonts");
    OverlaySettings st = base_settings();
    st.family = builtin;
    PageLayout L;
    CHECK(layout_page(synth_page({"User font page"}), st, 4.0f, 4.0f, L) && !L.items.empty(), "a user font lays out");
    // mutation fuzz of the validator: must never crash
    std::mt19937 rng(1234);
    int accepted = 0;
    for (int i = 0; i < 300; ++i) {
        std::vector<uint8_t> m = good;
        for (int k = 0; k < 1 + i % 8; ++k) m[rng() % m.size()] = static_cast<uint8_t>(rng());
        if (validate_truetype(m.data(), m.size())) ++accepted;
    }
    CHECK(accepted <= 300, "validator survives mutated fonts");
    CHECK(!validate_truetype(nullptr, 0) && !validate_truetype(good.data(), 8), "validator rejects tiny input");
    lib.set_user_font_dir((tmp / "nonexistent").string());
    CHECK(lib.family_count() == builtin, "removing the folder removes the user fonts");
    lib.set_user_font_dir("fonts");
    fs::remove_all(tmp, ec);
}

} // namespace

int run_overlay_render_tests(const char* png_dir, bool verbose) {
    g_fail = 0;
    if (!SDL_WasInit(0)) SDL_Init(0);
    test_fonts(verbose);
    test_layout_keep_and_reveal();
    test_reflow_and_fit();
    test_unsupported_and_words();
    test_drawing_and_caches();
    test_battle_additions();
    test_user_fonts_and_validation();
    (void)png_dir;
    return g_fail;
}

#ifdef KHCOM_OVERLAY_TEST_MAIN
int main(int argc, char** argv) {
    return run_overlay_render_tests(argc > 1 ? argv[1] : nullptr, true) == 0 ? 0 : 1;
}
#endif
