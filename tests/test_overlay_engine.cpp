// Unit tests of the ENGINE side of the dialogue overlay (src/text_overlay/engine_hooks.*):
//   * fixed-point decoding and word classification,
//   * the snapshot builder + stand-down predicate (table of cases),
//   * the full hook flow on a fake guest memory (layout -> window update -> draw call hides only when it should),
//   * inertness: overlay disabled => no extra hook in the chain, nothing captured, nothing hidden.
#undef NDEBUG
#include "text_overlay/battle_text.h"
#include "text_overlay/engine_hooks.h"
#include "text_overlay/fade_sampler.h"

#include "widescreen_adapter.h"
#include "armv4t/runtime_arm.h"

#include <SDL.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

extern "C" {
extern void (*g_runtime_fn_entry_hook)(uint32_t entry_pc);
}

using namespace khcom::overlay;
namespace eng = khcom::overlay::engine;

namespace {

// ---- a fake guest address space ------------------------------------------------------------------
std::map<uint32_t, uint8_t> g_mem;
void w8(uint32_t a, uint8_t v) { g_mem[a] = v; }
void w16(uint32_t a, uint16_t v) { w8(a, v & 255); w8(a + 1, static_cast<uint8_t>(v >> 8)); }
void w32(uint32_t a, uint32_t v) { w16(a, v & 0xFFFF); w16(a + 2, static_cast<uint16_t>(v >> 16)); }
uint8_t r8(uint32_t a) { auto it = g_mem.find(a); return it == g_mem.end() ? 0 : it->second; }
uint16_t r16(uint32_t a) { return static_cast<uint16_t>(r8(a) | (r8(a + 1) << 8)); }
uint32_t r32(uint32_t a) { return r16(a) | (static_cast<uint32_t>(r16(a + 2)) << 16); }
const eng::MemReader kReader{&r32, &r16, &r8};

uint64_t g_frame = 100;
uint64_t g_ppu_off = 0;   // PPU frame counter = vblank counter + offset
uint64_t frame_src() { return g_frame; }
uint64_t ppu_src() { return g_frame + g_ppu_off; }

constexpr uint32_t kW = 0x02000100u;
constexpr uint32_t kTable = 0x02001000u;
constexpr uint32_t kPool = 0x02002000u;
constexpr uint32_t kHandles = 0x02003000u;
constexpr uint32_t kStart = 0x08FBD400u;

// Page "Hi you" + newline + "ok": one pool entry per word except the newline (8 entries).
std::vector<uint16_t> page_words() {
    std::vector<uint16_t> w;
    for (char c : std::string("Hi you")) w.push_back(static_cast<uint8_t>(c));
    w.push_back(0x0A);
    w.push_back('o');
    w.push_back('k');
    return w;
}

// Writes the whole world for one page. `reveal` = typewriter counter; `cont` = continuation pointer.
void build_world(const std::vector<uint16_t>& words, int reveal, uint32_t cont, uint16_t kind = 0x10,
                 uint32_t start = kStart, int x0 = 72, int y0 = 12) {
    g_mem.clear();
    uint32_t a = start;
    for (uint16_t w : words) { w16(a, w); a += 2; }
    w16(a, 0);   // terminator after the page
    int drawable = 0;
    for (uint16_t w : words) if (eng::is_drawable_word(w)) ++drawable;
    w8(kW + 0x20, 2);
    w8(kW + 0x24, static_cast<uint8_t>(reveal));
    w8(kW + 0x26, static_cast<uint8_t>(drawable));
    w8(kW + 0x27, 0);
    w8(kW + 0x28, 3);
    w8(kW + 0x2B, 0);
    w32(kW + 0x3C, kTable);
    w32(kW + 0x40, cont);
    w32(kTable + 0, kind);
    w32(eng::kPoolPtrAddr, kPool);
    w8(eng::kPoolCountAddr, static_cast<uint8_t>(drawable));
    int pen = 0, line = 0, i = 0;
    for (uint16_t w : words) {
        if (w == 0x0A) { pen = 0; ++line; continue; }
        if (!eng::is_drawable_word(w)) continue;
        const uint32_t e = kPool + 24u * i;
        w32(e + 0, static_cast<uint32_t>((x0 + pen) << 8));
        w32(e + 4, static_cast<uint32_t>((y0 + 12 * line) << 8));
        if (w != 0x20) {
            const uint32_t h = kHandles + 16u * i;
            w32(e + 8, h);
            w32(h + 0, eng::kFontTilesBase + 128u * (w == 0xE000 ? 25u : w));
        } else {
            w32(e + 8, 0);
        }
        w8(e + 20, 0);
        w8(e + 21, 1);
        pen += 6;
        ++i;
    }
}

void hook_layout(uint32_t window, uint32_t start, int x0, int y0) {
    g_cpu.R[0] = static_cast<uint32_t>(x0) << 8;   // the engine passes 8.8 fixed point
    g_cpu.R[1] = static_cast<uint32_t>(y0) << 8;
    g_cpu.R[2] = start;
    g_cpu.R[3] = window + 0x40;
    g_runtime_fn_entry_hook(eng::kFnLayout);
}
void hook_update(uint32_t window) {
    g_cpu.R[0] = window;
    g_runtime_fn_entry_hook(eng::kFnWindowUpdate);
}
// Returns R0 after the draw hook: 0 when the overlay hid the engine glyphs, the original count otherwise.
uint32_t hook_draw(uint32_t window, uint32_t count, uint32_t lr = eng::kDrawCallerLr) {
    g_cpu.R[0] = count;
    g_cpu.R[4] = window;
    g_cpu.R[14] = lr;
    g_runtime_fn_entry_hook(eng::kFnDraw);
    return g_cpu.R[0];
}

// A fully valid raw page for the pure predicate.
eng::RawPage valid_raw() {
    eng::RawPage r;
    r.window = kW;
    r.kind = 0x10;
    r.placement = 2;
    r.revealed = 4;
    r.total = 8;
    r.state = 0;
    r.start_ptr = kStart;
    r.layout_window = kW;
    r.x0 = 72;
    r.y0 = 12;
    r.cont_ptr = 0;
    r.words = page_words();
    r.words_ok = true;
    r.pool_count = 8;
    r.pool_valid = true;
    int i = 0;
    for (uint16_t w : r.words) {
        if (!eng::is_drawable_word(w)) continue;
        eng::PoolEntryRaw e;
        e.x = static_cast<uint32_t>((72 + 6 * i) << 8);
        e.y = static_cast<uint32_t>(12 << 8);
        e.handle = w == 0x20 ? 0 : kHandles + 16u * i;
        e.glyph_src = w == 0x20 ? 0 : eng::kFontTilesBase + 128u * w;
        e.used = 1;
        r.pool.push_back(e);
        ++i;
    }
    return r;
}

bool can_draw_yes(const PageSnapshot&, void*) { return true; }
bool can_draw_no(const PageSnapshot&, void*) { return false; }

void test_decoding() {
    assert(eng::fixed_to_px(72u << 8) == 72);
    assert(eng::fixed_to_px((72u << 8) | 0x80) == 72);
    assert(eng::fixed_to_px(static_cast<uint32_t>(-16 * 256)) == -16);
    assert(eng::fixed_to_float((72u << 8) | 0x80) == 72.5f);
    assert(eng::fixed_to_float(static_cast<uint32_t>(-16 * 256 + 64)) == -15.75f);
    assert(eng::is_supported_word('A') && eng::is_supported_word(0xFF) && eng::is_supported_word(0x0A) &&
           eng::is_supported_word(0xE000) && eng::is_supported_word(0x4079) && eng::is_supported_word(0x4000));
    assert(!eng::is_supported_word(0) && !eng::is_supported_word(0x1F) && !eng::is_supported_word(0x100) &&
           !eng::is_supported_word(0x3042) && !eng::is_supported_word(0x2192));
    assert(eng::is_drawable_word(' ') && eng::is_drawable_word(0xE000));
    assert(!eng::is_drawable_word(0) && !eng::is_drawable_word(0x0A) && !eng::is_drawable_word(0x4079) &&
           !eng::is_drawable_word(0x4000));
    std::cout << "[PASS] Overlay engine: fixed-point decoding + word classes" << std::endl;
}

struct Case {
    const char* name;
    void (*mutate)(eng::RawPage&, eng::Env&);
    eng::Reason expect;
};

void test_predicate_table() {
    static const Case kCases[] = {
        {"valid", [](eng::RawPage&, eng::Env&) {}, eng::Reason::Overlay},
        {"disabled", [](eng::RawPage&, eng::Env& e) { e.enabled = false; }, eng::Reason::Disabled},
        {"battle", [](eng::RawPage&, eng::Env& e) { e.battle = true; }, eng::Reason::Battle},
        {"no viewport", [](eng::RawPage&, eng::Env& e) { e.viewport_ok = false; }, eng::Reason::NoViewport},
        {"subtitle kind 0x3E", [](eng::RawPage& r, eng::Env&) { r.kind = 0x3E; }, eng::Reason::Subtitle},
        {"layout call not seen", [](eng::RawPage& r, eng::Env&) { r.start_ptr = 0; }, eng::Reason::NoLayout},
        {"layout of another window", [](eng::RawPage& r, eng::Env&) { r.layout_window = kW + 0x100; },
         eng::Reason::NoLayout},
        {"text before cutscene range", [](eng::RawPage& r, eng::Env&) { r.start_ptr = eng::kCutsceneTextBegin - 2; },
         eng::Reason::OutsideCutsceneRange},
        {"text after cutscene range", [](eng::RawPage& r, eng::Env&) { r.start_ptr = eng::kCutsceneTextEnd; },
         eng::Reason::OutsideCutsceneRange},
        {"unreadable range", [](eng::RawPage& r, eng::Env&) { r.words_ok = false; }, eng::Reason::BadRange},
        {"empty range", [](eng::RawPage& r, eng::Env&) { r.words.clear(); }, eng::Reason::BadRange},
        {"shift-jis word", [](eng::RawPage& r, eng::Env&) { r.words[1] = 0x3042; }, eng::Reason::UnsupportedWord},
        {"icon word", [](eng::RawPage& r, eng::Env&) { r.words[0] = 0x2192; }, eng::Reason::UnsupportedWord},
        {"control word below 0x20", [](eng::RawPage& r, eng::Env&) { r.words[2] = 0x05; }, eng::Reason::UnsupportedWord},
        {"revealed > total", [](eng::RawPage& r, eng::Env&) { r.revealed = 9; }, eng::Reason::BadCounters},
        {"total != words", [](eng::RawPage& r, eng::Env&) { r.total = 7; r.pool.pop_back(); r.pool_count = 7; },
         eng::Reason::BadCounters},
        {"pool count byte differs", [](eng::RawPage& r, eng::Env&) { r.pool_count = 9; }, eng::Reason::BadCounters},
        {"pool vector short", [](eng::RawPage& r, eng::Env&) { r.pool.pop_back(); }, eng::Reason::BadCounters},
        {"zero total", [](eng::RawPage& r, eng::Env&) { r.total = 0; }, eng::Reason::BadCounters},
        {"no pool pointer", [](eng::RawPage& r, eng::Env&) { r.pool_valid = false; }, eng::Reason::NoPool},
        {"glyph code mismatch", [](eng::RawPage& r, eng::Env&) { r.pool[0].glyph_src += 128; }, eng::Reason::PoolMismatch},
        {"missing tile handle", [](eng::RawPage& r, eng::Env&) { r.pool[1].handle = 0; }, eng::Reason::PoolMismatch},
        {"entry not in use", [](eng::RawPage& r, eng::Env&) { r.pool[2].used = 0; }, eng::Reason::PoolMismatch},
        {"origin differs from pool", [](eng::RawPage& r, eng::Env&) { r.x0 = 16; }, eng::Reason::BadPlacement},
        {"unknown placement", [](eng::RawPage& r, eng::Env&) { r.x0 = 40; r.pool[0].x = 40u << 8; },
         eng::Reason::BadPlacement},
        {"placement check off", [](eng::RawPage& r, eng::Env& e) {
             r.x0 = 40; r.pool[0].x = 40u << 8; e.placement_check = false; }, eng::Reason::Overlay},
    };
    for (const Case& c : kCases) {
        eng::RawPage r = valid_raw();
        eng::Env e;
        c.mutate(r, e);
        PageSnapshot s;
        const eng::Reason got = eng::evaluate(r, e, &s, &can_draw_yes, nullptr);
        if (got != c.expect) {
            std::fprintf(stderr, "predicate case '%s': got %s expected %s\n", c.name, eng::reason_name(got),
                         eng::reason_name(c.expect));
        }
        assert(got == c.expect);
        assert(s.valid == (got == eng::Reason::Overlay));
    }
    {   // the render side may refuse
        eng::RawPage r = valid_raw();
        PageSnapshot s;
        assert(eng::evaluate(r, eng::Env{}, &s, &can_draw_no, nullptr) == eng::Reason::CannotDraw);
        assert(!s.valid);
    }
    {   // snapshot content of the valid page
        eng::RawPage r = valid_raw();
        r.state = 1;
        r.revealed = 8;
        PageSnapshot s;
        assert(eng::evaluate(r, eng::Env{}, &s, nullptr, nullptr) == eng::Reason::Overlay);
        assert(s.text == page_words());
        assert(s.glyphs.size() == 8 && s.revealed == 8 && s.page_complete);
        assert(s.glyphs[0].word == 'H' && s.glyphs[0].gx == 72.0f && s.glyphs[0].gy == 12.0f);
        assert(s.glyphs[2].word == ' ');
        assert(s.origin_x == 72.0f && s.origin_y == 12.0f && s.wrap_px == 155 && s.line_pitch == 12 && s.max_lines == 3);
        r.state = 0;
        assert(eng::evaluate(r, eng::Env{}, &s, nullptr, nullptr) == eng::Reason::Overlay && !s.page_complete);
        r.revealed = 3;
        r.state = 1;
        assert(eng::evaluate(r, eng::Env{}, &s, nullptr, nullptr) == eng::Reason::Overlay && !s.page_complete);
    }
    std::cout << "[PASS] Overlay engine: stand-down predicate (" << sizeof(kCases) / sizeof(kCases[0])
              << " cases) + snapshot decode" << std::endl;
}

void test_hook_flow_and_inertness() {
    void (*saved_hook)(uint32_t) = g_runtime_fn_entry_hook;
    const bool saved_enabled = settings().enabled;
    eng::set_memory_reader_for_tests(&kReader);
    eng::set_frame_source_for_tests(&frame_src, &ppu_src);
    eng::set_pairing_lag_for_tests(2);
    SDL_Rect dst{0, 0, 720, 480};

    // ---- overlay OFF: the chain holds no overlay link, nothing is captured, nothing is hidden ----
    settings().enabled = false;
    khcom_update_widescreen_state();
    assert(!eng::wants_hook());
    assert(g_runtime_fn_entry_hook != nullptr);
    build_world(page_words(), 4, 0);
    eng::set_viewport_for_tests(reinterpret_cast<SDL_Renderer*>(0x10), dst);
    const uint64_t w0 = eng::stats().windows, h0 = eng::stats().hidden;
    const uint64_t d0 = OverlayRenderer::instance().draws();   // the global counter may already be non-zero
    hook_layout(kW, kStart, 72, 12);
    hook_update(kW);
    assert(hook_draw(kW, 4) == 4);
    assert(eng::stats().windows == w0 && eng::stats().hidden == h0);
    eng::on_present(reinterpret_cast<SDL_Renderer*>(0x10), dst);   // must not draw (nor touch the fake renderer)
    assert(OverlayRenderer::instance().draws() == d0);

    // ---- overlay ON: hook chained, decision made once, draw count zeroed ----
    settings().enabled = true;
    khcom_update_widescreen_state();
    assert(eng::wants_hook());
    build_world(page_words(), 4, 0);
    hook_layout(kW, kStart, 72, 12);
    g_frame = 100;
    hook_update(kW);
    {
        assert(hook_draw(kW, 4) == 0);
        eng::Reason why;
        PageSnapshot snap;
        assert(eng::last_decision_for_tests(&why, &snap) && why == eng::Reason::Overlay);
        assert(snap.valid && snap.revealed == 4 && snap.glyphs.size() == 8 && snap.text == page_words());
        assert(eng::stats().hidden == h0 + 1);
    }
    // the draw call of any other caller is never touched
    hook_update(kW);
    assert(hook_draw(kW, 5, 0x0805B61Bu) == 5);
    // a draw call for another window object is never touched
    hook_update(kW);
    assert(hook_draw(kW + 0x80, 5) == 5);

    // ---- stand-down paths leave the engine in charge (count untouched) ----
    {   // subtitle kind
        build_world(page_words(), 4, 0, 0x3E);
        hook_layout(kW, kStart, 72, 12);
        hook_update(kW);
        assert(hook_draw(kW, 4) == 4);
    }
    {   // text outside the cutscene tables
        build_world(page_words(), 4, 0, 0x10, 0x08FFF000u);
        hook_layout(kW, 0x08FFF000u, 72, 12);
        hook_update(kW);
        assert(hook_draw(kW, 4) == 4);
    }
    {   // unsupported word in the page
        std::vector<uint16_t> w = page_words();
        w[1] = 0x3042;
        build_world(w, 4, 0);
        hook_layout(kW, kStart, 72, 12);
        hook_update(kW);
        assert(hook_draw(kW, 4) == 4);
    }
    {   // pool differs from the page (stale pool / savestate mid-page)
        build_world(page_words(), 4, 0);
        w32(kHandles + 0, eng::kFontTilesBase + 128u * 'Z');
        hook_layout(kW, kStart, 72, 12);
        hook_update(kW);
        assert(hook_draw(kW, 4) == 4);
    }
    {   // window update without a captured layout (hook installed mid-page): reset() forgets it
        build_world(page_words(), 4, 0);
        eng::reset();
        hook_update(kW);
        assert(hook_draw(kW, 4) == 4);
        // ... and the next page start re-arms it
        hook_layout(kW, kStart, 72, 12);
        hook_update(kW);
        assert(hook_draw(kW, 4) == 0);
    }
    {   // frame counter going backwards (savestate load) discards what was captured
        build_world(page_words(), 4, 0);
        g_frame = 500;
        hook_layout(kW, kStart, 72, 12);
        hook_update(kW);
        assert(hook_draw(kW, 4) == 0);
        g_frame = 10;
        hook_update(kW);   // the layout capture was dropped by the regression
        assert(hook_draw(kW, 4) == 4);
    }
    {   // PPU frame counter jumping while the vblank counter steps normally (savestate load): captured state dropped
        build_world(page_words(), 4, 0);
        g_ppu_off = 0;
        g_frame = 700;
        hook_layout(kW, kStart, 72, 12);
        hook_update(kW);
        assert(hook_draw(kW, 4) == 0);
        g_frame = 701;                    // normal step: still armed
        hook_update(kW);
        assert(hook_draw(kW, 4) == 0);
        g_frame = 702;
        g_ppu_off = 8298;                 // state loaded: ppu counter now 9000
        hook_update(kW);
        assert(hook_draw(kW, 4) == 4);
        g_frame = 703;
        hook_update(kW);                  // still no layout call since the load: engine keeps the page
        assert(hook_draw(kW, 4) == 4);
        g_ppu_off = 0;
    }
    {   // multi-page: the continuation pointer bounds the page
        std::vector<uint16_t> two = page_words();
        std::vector<uint16_t> page1(two.begin(), two.begin() + 7);   // "Hi you\n"
        build_world(page1, 2, kStart + 14);
        uint32_t a = kStart + 14;                                   // the rest of the message follows in ROM
        for (size_t i = 7; i < two.size(); ++i, a += 2) w16(a, two[i]);
        w16(a, 0);
        g_frame = 800;
        hook_update(kW);                  // absorbs the counter reset of the previous block
        hook_layout(kW, kStart, 72, 12);
        hook_update(kW);
        assert(hook_draw(kW, 2) == 0);
        eng::Reason why;
        PageSnapshot snap;
        assert(eng::last_decision_for_tests(&why, &snap) && why == eng::Reason::Overlay && snap.text == page1);
    }

    // ---- back to OFF: the link is switched back, state forgotten ----
    settings().enabled = false;
    khcom_update_widescreen_state();
    assert(!eng::wants_hook());
    const uint64_t w1 = eng::stats().windows;
    hook_update(kW);
    assert(hook_draw(kW, 4) == 4 && eng::stats().windows == w1);

    settings().enabled = saved_enabled;
    eng::set_memory_reader_for_tests(nullptr);
    eng::set_frame_source_for_tests(nullptr);
    eng::reset();
    g_runtime_fn_entry_hook = saved_hook;
    khcom_update_widescreen_state();
    std::cout << "[PASS] Overlay engine: hook flow (hide only when the predicate holds) + inert when disabled"
              << std::endl;
}

// ---- screen fades -----------------------------------------------------------------------------------
void test_fades() {
    // BLDCNT effect 2 / 3 with the OBJ layer (bit 4) as first target; BLDY clamps at 16
    FadeState f = fade_from_regs(0x0090, 0, 8, true);   // effect 2, OBJ
    assert(f.mode == FadeMode::Brighten && f.level == 0.5f);
    f = fade_from_regs(0x00D0, 0, 16, true);            // effect 3
    assert(f.mode == FadeMode::Darken && f.level == 1.0f);
    f = fade_from_regs(0x00D0, 0, 31, true);
    assert(f.mode == FadeMode::Darken && f.level == 1.0f);   // clamped like the PPU
    assert(fade_from_regs(0x00D0, 0, 0, true).mode == FadeMode::None);
    assert(fade_from_regs(0x00CF, 0, 16, true).mode == FadeMode::None);   // BG-only first targets: the text OBJ is not faded
    assert(fade_from_regs(0x0010, 0, 16, true).mode == FadeMode::None);   // effect 0
    f = fade_from_regs(0x0050, 0x0808, 0, true);   // alpha, OBJ first target, EVA 8
    assert(f.mode == FadeMode::Alpha && f.level == 0.5f);
    assert(fade_from_regs(0x0050, 0x0010, 0, true).mode == FadeMode::None);   // EVA 16: opaque
    assert(fade_from_regs(0x0F40, 0x0A06, 0, true).mode == FadeMode::None);   // the observed box/figure fades: OBJ not first target

    // the bundled PPU ignores BLDCNT for normal OBJ (the dialogue glyphs): the default mapping is inert
    assert(!kPpuBlendsNormalObj);
    assert(fade_from_regs(0x00D0, 0, 16).mode == FadeMode::None && fade_from_regs(0x0090, 0, 8).mode == FadeMode::None);

    // colours
    assert(fade_rgb(0x102030u, FadeMode::Darken, 1.0f) == 0u);
    assert(fade_rgb(0x102030u, FadeMode::Brighten, 1.0f) == 0xFFFFFFu);
    assert(fade_rgb(0xFF8000u, FadeMode::Darken, 0.5f) == 0x804000u);
    assert(fade_rgb(0x000000u, FadeMode::Brighten, 0.5f) == 0x808080u);
    assert(fade_rgb(0x123456u, FadeMode::None, 0.7f) == 0x123456u);
    assert(fade_rgb(0x123456u, FadeMode::Alpha, 0.7f) == 0x123456u);

    // palette fades seen through the engine text colours
    const uint32_t body = 0x632100FFu, shadow = 0xD09838FFu;   // (99,33,0) / (208,152,56): the engine's own colours
    assert(fade_from_text_colours(body, shadow, body, shadow).mode == FadeMode::None);
    auto scale = [](uint32_t c, float k) {
        auto ch = [&](int s) { return static_cast<uint32_t>(((c >> s) & 0xFF) * k + 0.5f) << s; };
        return ch(24) | ch(16) | ch(8) | 0xFFu;
    };
    f = fade_from_text_colours(body, shadow, scale(body, 0.5f), scale(shadow, 0.5f));
    assert(f.mode == FadeMode::Darken && std::fabs(f.level - 0.5f) < 0.06f);
    f = fade_from_text_colours(body, shadow, 0x000000FFu, 0x000000FFu);
    assert(f.mode == FadeMode::Darken && f.level > 0.95f);
    f = fade_from_text_colours(body, shadow, 0xFFFFFFFFu, 0xFFFFFFFFu);
    assert(f.mode == FadeMode::Brighten && f.level > 0.95f);
    assert(fade_from_text_colours(body, shadow, 0xFFFFFFFFu, scale(shadow, 0.5f)).mode == FadeMode::None);   // inconsistent
    assert(fade_from_text_colours(0, 0, body, shadow).mode == FadeMode::None);                               // unknown reference

    // per-scanline sampler table: the fade of the text's own scanline is used
    eng::fade_sampler_reset_for_tests();
    eng::FrameBlend fb;
    assert(!eng::fade_sampler_last_frame(&fb));
    for (int y = 0; y < 160; ++y) {
        eng::BlendRegs r;
        if (y >= 100) { r.bldcnt = 0x00D0; r.bldy = 4; }   // HBlank-DMA style: only the lower part is darkened
        eng::fade_sampler_feed_line_for_tests(y, r);
    }
    eng::fade_sampler_frame_ready_for_tests();
    assert(eng::fade_sampler_last_frame(&fb));
    assert(fb.state_at(20, true).mode == FadeMode::None);
    f = fb.state_at(120, true);
    assert(f.mode == FadeMode::Darken && f.level == 0.25f);
    assert(fb.state_at(-5, true).mode == FadeMode::None && fb.state_at(999, true).mode == FadeMode::Darken);
    eng::fade_sampler_reset_for_tests();
}

// ---- battle message boxes (PPU observer path, battle_text.cpp) ------------------------------------------------
namespace bt = khcom::overlay::engine::battle;

// Deterministic fake ROM glyph bitmap.
void fake_glyph(int g, uint8_t out[128]) {
    for (int i = 0; i < 128; ++i) out[i] = static_cast<uint8_t>(g * 7 + i * 3 + 1);
}
bool fake_rom_glyph(void*, int g, uint8_t out[128]) {
    if (g < 0 || g > 255) return false;
    fake_glyph(g, out);
    return true;
}

// Pool of a test page: text lines on the 6 px grid, line pitch 12, like the engine's pool (spaces are entries without a tile).
std::vector<eng::PoolEntryRaw> make_pool(const std::vector<std::string>& lines, int x0, int y0, const std::vector<int>& colour_entries = {}) {
    std::vector<eng::PoolEntryRaw> pool;
    int i = 0;
    for (size_t li = 0; li < lines.size(); ++li) {
        int pen = 0;
        for (unsigned char c : lines[li]) {
            eng::PoolEntryRaw e;
            e.x = static_cast<uint32_t>((x0 + pen) << 8);
            e.y = static_cast<uint32_t>((y0 + 12 * static_cast<int>(li)) << 8);
            if (c != ' ') {
                e.handle = 0x02003000u + 16u * static_cast<uint32_t>(i);
                e.glyph_src = eng::kFontTilesBase + 128u * c;
            }
            e.used = 1;
            for (int ce : colour_entries) if (ce == i) e.color = 1;
            pool.push_back(e);
            pen += 6;
            ++i;
        }
    }
    return pool;
}

// A frame as the PPU sees it: pool + OAM + VRAM + OBJ palette. Entry k of the pool is drawn by OAM sprite 10 + k.
struct BWorld {
    bt::RawBattle raw;
    std::vector<uint8_t> vram = std::vector<uint8_t>(96 * 1024, 0);
    std::vector<uint16_t> pal = std::vector<uint16_t>(256, 0);
    static int sprite_of(int k) { return 10 + k; }
    static uint32_t tile_of(int k) { return 100u + 4u * static_cast<uint32_t>(k); }
    BWorld(const BWorld&) = delete;
    BWorld& operator=(const BWorld&) = delete;
    BWorld(const std::vector<std::string>& lines, int typed_entries, int x0 = 16, int y0 = 12, bool one_d = true,
           const std::vector<int>& colour_entries = {}) {
        raw.pool = make_pool(lines, x0, y0, colour_entries);
        raw.pool_count = static_cast<uint8_t>(raw.pool.size());
        raw.pool_valid = true;
        raw.obj_1d = one_d;
        raw.vram = vram.data();
        raw.rom_glyph = &fake_rom_glyph;
        pal[5 * 16 + 1] = 0x001F;   // bank 5 body: red
        pal[5 * 16 + 2] = 0x03E0;   // bank 5 shadow: green
        pal[6 * 16 + 1] = 0x7C00;   // bank 6 body: blue (colour-code words)
        pal[6 * 16 + 2] = 0x7FFF;   // bank 6 shadow: white
        raw.obj_pal = pal.data();
        for (auto& o : raw.oam) o.a0 = 0x0200;   // every entry disabled
        for (int k = 0; k < static_cast<int>(raw.pool.size()) && k < typed_entries; ++k) put_sprite(k);
    }
    void put_sprite(int k, int bank = -1) {
        const eng::PoolEntryRaw& e = raw.pool[static_cast<size_t>(k)];
        if (e.handle == 0) return;
        const int b = bank >= 0 ? bank : (e.color != 0 ? 6 : 5);
        const int idx = sprite_of(k);
        const int x = eng::fixed_to_px(e.x), y = eng::fixed_to_px(e.y);
        raw.oam[idx].a0 = static_cast<uint16_t>(y & 0xFF);
        raw.oam[idx].a1 = static_cast<uint16_t>((x & 0x1FF) | (1u << 14));
        raw.oam[idx].a2 = static_cast<uint16_t>(tile_of(k) | (static_cast<uint32_t>(b) << 12));
        uint8_t g[128];
        fake_glyph(static_cast<int>((e.glyph_src - eng::kFontTilesBase) / 128u), g);
        if (raw.obj_1d) {
            std::memcpy(&vram[0x10000 + tile_of(k) * 32], g, 128);
        } else {
            std::memcpy(&vram[0x10000 + tile_of(k) * 32], g, 64);
            std::memcpy(&vram[0x10000 + (tile_of(k) + 32) * 32], g + 64, 64);
        }
    }
    void clear_sprite(int k) { raw.oam[sprite_of(k)] = bt::OamRaw{0x0200, 0, 0}; }
    void stray(int idx, int y, int x, int tile, int bank) {
        raw.oam[idx] = bt::OamRaw{static_cast<uint16_t>(y), static_cast<uint16_t>((x & 0x1FF) | (1u << 14)),
                                  static_cast<uint16_t>(static_cast<uint32_t>(tile) | (static_cast<uint32_t>(bank) << 12))};
    }
    // the OAM as 1 KB of bytes (what the PPU observer receives)
    void to_bytes(std::vector<uint8_t>& oam) const {
        oam.assign(1024, 0);
        for (int i = 0; i < 128; ++i) {
            oam[i * 8] = raw.oam[i].a0 & 0xFF; oam[i * 8 + 1] = raw.oam[i].a0 >> 8;
            oam[i * 8 + 2] = raw.oam[i].a1 & 0xFF; oam[i * 8 + 3] = raw.oam[i].a1 >> 8;
            oam[i * 8 + 4] = raw.oam[i].a2 & 0xFF; oam[i * 8 + 5] = raw.oam[i].a2 >> 8;
        }
    }
    void pal_bytes(std::vector<uint8_t>& p) const {
        p.assign(1024, 0);
        for (int i = 0; i < 256; ++i) { p[0x200 + 2 * i] = pal[i] & 0xFF; p[0x201 + 2 * i] = pal[i] >> 8; }
    }
};

bool bt_can_draw_yes(const PageSnapshot&, void*) { return true; }
bool bt_can_draw_no(const PageSnapshot&, void*) { return false; }

int hide_bits(const bt::Decision& d) {
    int n = 0;
    for (int i = 0; i < 128; ++i) if ((d.hide[i / 64] >> (i % 64)) & 1ull) ++n;
    return n;
}

void test_battle_decode() {
    const std::vector<std::string> kPage = {"Hi you", "ok"};   // 8 pool entries: 6 + 2
    bt::Env env;
    env.frame_off_x = 22;
    auto run = [&](const BWorld& w, const bt::Env& e, bt::Decision* d, bool (*cd)(const PageSnapshot&, void*) = nullptr) {
        return bt::evaluate(w.raw, e, d, cd, nullptr);
    };
    bt::Decision d;
    // fully typed page
    {
        BWorld w(kPage, 8);
        assert(run(w, env, &d) == bt::Reason::Overlay);
        assert(d.snap.valid && d.snap.source == 1 && d.matched == 7);   // 7 glyph sprites (the space has none)
        assert(d.snap.revealed == 8 && d.snap.page_complete);
        const std::vector<uint16_t> want = {'H', 'i', ' ', 'y', 'o', 'u', 0x0A, 'o', 'k'};
        assert(d.snap.text == want);
        assert(d.snap.glyphs.size() == 8 && d.snap.glyphs[0].word == 'H' && d.snap.glyphs[2].word == ' ');
        // x is shifted by the picture offset inside the 16:9 image, y is not; origin follows the first cell
        assert(d.snap.glyphs[0].gx == 38.0f && d.snap.glyphs[0].gy == 12.0f && d.snap.glyphs[6].gx == 38.0f && d.snap.glyphs[6].gy == 24.0f);
        assert(d.snap.origin_x == 38.0f && d.snap.origin_y == 12.0f);
        assert(d.snap.wrap_px == 155 && d.snap.line_pitch == 12 && d.snap.max_lines == 3);
        // engine colours come from the OBJ palette bank of the sprites: bank 5 entry 1 = red, 2 = green
        assert(d.snap.engine_text_rgba == 0xFF0000FFu && d.snap.engine_shadow_rgba == 0x00FF00FFu && d.snap.engine_highlight_rgba == 0);
        // hide mask = exactly the matched sprites
        assert(hide_bits(d) == 7 && ((d.hide[0] >> BWorld::sprite_of(0)) & 1ull) && !((d.hide[0] >> BWorld::sprite_of(2)) & 1ull));
        assert(d.page_signature != 0);
        // the same page again gives the same signature, another page does not
        bt::Decision d2;
        assert(run(w, env, &d2) == bt::Reason::Overlay && d2.page_signature == d.page_signature);
        BWorld other({"Hi yoo", "ok"}, 8);
        assert(run(other, env, &d2) == bt::Reason::Overlay && d2.page_signature != d.page_signature);
    }
    // typewriter: a prefix of the page is typed; spaces count as typed once a later glyph is
    {
        BWorld w1(kPage, 1);
        assert(run(w1, env, &d) == bt::Reason::Overlay && d.matched == 1 && d.snap.revealed == 1 && !d.snap.page_complete);
        BWorld w4(kPage, 4);   // "Hi y": entries 0..3 typed
        assert(run(w4, env, &d) == bt::Reason::Overlay && d.matched == 3 && d.snap.revealed == 4);
        BWorld w2(kPage, 2);   // "Hi" typed, the space not yet reached
        assert(run(w2, env, &d) == bt::Reason::Overlay && d.matched == 2 && d.snap.revealed == 2);
        BWorld w7(kPage, 7);   // all but the last glyph
        assert(run(w7, env, &d) == bt::Reason::Overlay && d.snap.revealed == 7 && !d.snap.page_complete);
    }
    // nothing typed yet / box closed: nothing to hide, nothing to draw
    {
        BWorld w(kPage, 0);
        assert(run(w, env, &d) == bt::Reason::NoText && !d.snap.valid && d.hide[0] == 0 && d.hide[1] == 0);
    }
    // typed sprites that are not a prefix of the page (page change in progress)
    {
        BWorld w(kPage, 8);
        w.clear_sprite(1);
        assert(run(w, env, &d) == bt::Reason::NotPrefix && !d.snap.valid && d.hide[0] == 0);
    }
    // a glyph-shaped sprite that is not part of the page and shares the text palette bank
    {
        BWorld w(kPage, 4);
        w.stray(90, 50, 100, 300, 5);
        assert(run(w, env, &d) == bt::Reason::ForeignSprite && !d.snap.valid && d.hide[0] == 0 && d.hide[1] == 0);
        w.stray(90, 50, 100, 300, 1);   // another palette bank: not text
        assert(run(w, env, &d) == bt::Reason::Overlay);
        w.stray(90, 50 | 0x0200, 100, 300, 5);   // disabled (bit 9 of attr0)
        assert(run(w, env, &d) == bt::Reason::Overlay);
        w.stray(90, 50, 100, 300, 5);
        w.raw.oam[90].a0 |= 1u << 14;   // wide shape: not a 16x16 square
        assert(run(w, env, &d) == bt::Reason::Overlay);
    }
    // glyph bitmap in VRAM differs from the ROM glyph; the check can be switched off
    {
        BWorld w(kPage, 4);
        w.vram[0x10000 + BWorld::tile_of(0) * 32 + 5] ^= 0x55;
        assert(run(w, env, &d) == bt::Reason::BitmapMismatch && !d.snap.valid);
        bt::Env nb = env;
        nb.check_bitmaps = false;
        assert(run(w, nb, &d) == bt::Reason::Overlay);
        bt::RawBattle r2 = w.raw;
        r2.vram = nullptr;   // no VRAM given: skipped
        assert(bt::evaluate(r2, env, &d, nullptr, nullptr) == bt::Reason::Overlay);
    }
    // 2D OBJ mapping reads the second tile row 32 tiles further
    {
        BWorld w(kPage, 8, 16, 12, false);
        assert(run(w, env, &d) == bt::Reason::Overlay && d.matched == 7);
        w.vram[0x10000 + (BWorld::tile_of(3) + 32) * 32 + 9] ^= 0x01;
        assert(run(w, env, &d) == bt::Reason::BitmapMismatch);
    }
    // two sprites on one glyph cell
    {
        BWorld w(kPage, 8);
        w.raw.oam[100] = w.raw.oam[BWorld::sprite_of(0)];
        assert(run(w, env, &d) == bt::Reason::DuplicateSprite);
    }
    // flipped / semi-transparent / 256 colour sprites are never glyph sprites
    {
        BWorld w(kPage, 8);
        w.raw.oam[BWorld::sprite_of(0)].a1 |= 0x1000;   // h-flip: glyph 0 unmatched, later ones matched
        assert(run(w, env, &d) == bt::Reason::NotPrefix);
        BWorld w2(kPage, 8);
        w2.raw.oam[BWorld::sprite_of(3)].a0 |= 0x0400;   // semi-transparent
        assert(run(w2, env, &d) == bt::Reason::NotPrefix);
        BWorld w3(kPage, 8);
        w3.raw.oam[BWorld::sprite_of(3)].a0 |= 0x2000;   // 256 colours
        assert(run(w3, env, &d) == bt::Reason::NotPrefix);
    }
    // pool problems
    {
        BWorld w(kPage, 8);
        w.raw.pool_valid = false;
        assert(run(w, env, &d) == bt::Reason::NoPool);
        BWorld w1(kPage, 8);
        w1.raw.pool_count = 0;
        assert(run(w1, env, &d) == bt::Reason::NoPool);
        BWorld w2(kPage, 8);
        w2.raw.pool_count = 9;   // vector shorter than the count byte
        assert(run(w2, env, &d) == bt::Reason::NoPool);
        BWorld w3(kPage, 8);
        w3.raw.pool[2].used = 0;
        assert(run(w3, env, &d) == bt::Reason::BadEntry);
        BWorld w4(kPage, 8);
        w4.raw.pool[1].glyph_src += 3;   // not on a glyph boundary
        assert(run(w4, env, &d) == bt::Reason::BadEntry);
        BWorld w5(kPage, 8);
        w5.raw.pool[0].glyph_src = eng::kFontTilesBase + 128u * 17u;   // an icon glyph the overlay cannot reproduce
        assert(run(w5, env, &d) == bt::Reason::UnsupportedGlyph);
        BWorld w6(kPage, 8);
        w6.raw.pool[0].glyph_src = eng::kFontTilesBase - 128u;
        assert(run(w6, env, &d) == bt::Reason::BadEntry);
        BWorld w7(kPage, 8);
        w7.raw.pool[0].glyph_src = eng::kFontTilesBase + 128u * 300u;
        assert(run(w7, env, &d) == bt::Reason::UnsupportedGlyph);
    }
    // environment: disabled, no viewport, story window active
    {
        BWorld w(kPage, 8);
        bt::Env e = env;
        e.enabled = false;
        assert(run(w, e, &d) == bt::Reason::Disabled && !d.snap.valid && d.hide[0] == 0);
        e = env;
        e.viewport_ok = false;
        assert(run(w, e, &d) == bt::Reason::NoViewport);
        e = env;
        e.story_window = true;
        assert(run(w, e, &d) == bt::Reason::StoryWindow && !d.snap.valid && d.hide[0] == 0 && d.hide[1] == 0);
    }
    // the render side may refuse: nothing is hidden
    {
        BWorld w(kPage, 8);
        assert(run(w, env, &d, &bt_can_draw_yes) == bt::Reason::Overlay);
        assert(run(w, env, &d, &bt_can_draw_no) == bt::Reason::CannotDraw && !d.snap.valid && d.hide[0] == 0 && d.hide[1] == 0);
    }
    // colour-code words: the sprite uses another bank; its colours become the highlight colours
    {
        BWorld w(kPage, 8, 16, 12, true, {3, 4});   // "yo" are colour-code glyphs
        assert(run(w, env, &d) == bt::Reason::Overlay);
        assert(d.snap.glyphs[3].color == 1 && d.snap.glyphs[4].color == 1 && d.snap.glyphs[0].color == 0 && d.snap.glyphs[5].color == 0);
        assert(d.snap.engine_highlight_rgba == 0x0000FFFFu && d.snap.engine_highlight_shadow_rgba == 0xFFFFFFFFu);
        assert(d.snap.engine_text_rgba == 0xFF0000FFu);
        // the coloured sprite bank is part of the "ours" set: a stray glyph-shaped sprite in bank 6 stands down
        w.stray(90, 50, 100, 300, 6);
        assert(run(w, env, &d) == bt::Reason::ForeignSprite);
    }
    // blank-line gaps and leading spaces (centred tutorial boxes): newlines follow the rows, spaces keep their cells
    {
        BWorld w({"   B Button", "", "x"}, 20, 46, 12);   // the empty middle line leaves a 24 px gap
        assert(run(w, env, &d) == bt::Reason::Overlay);
        const std::vector<uint16_t> want = {' ', ' ', ' ', 'B', ' ', 'B', 'u', 't', 't', 'o', 'n', 0x0A, 0x0A, 'x'};
        // (the empty line has no entries: its row is only visible as the 24 px jump to the next cell)
        std::vector<uint16_t> got = d.snap.text;
        assert(got == want);
        assert(d.snap.origin_x == 68.0f && d.snap.glyphs[3].gx == 68.0f + 18.0f);   // first cell is a space; 'B' is 3 cells right
    }
    // sprite index 127 lands in the second word of the hide mask
    {
        BWorld w(kPage, 8);
        w.raw.oam[127] = w.raw.oam[BWorld::sprite_of(0)];
        w.raw.oam[BWorld::sprite_of(0)].a0 = 0x0200;
        assert(run(w, env, &d) == bt::Reason::Overlay && ((d.hide[1] >> 63) & 1ull) && !(d.hide[0] & (1ull << BWorld::sprite_of(0))));
    }
    // negative x (sprite partly in the left margin) and the y wrap of OAM (y >= 160 means above the screen)
    {
        BWorld w({"ab"}, 2, -4, -6);
        assert(run(w, env, &d) == bt::Reason::Overlay && d.matched == 2 && d.snap.glyphs[0].gx == -4.0f + 22.0f && d.snap.glyphs[0].gy == -6.0f);
    }
    std::cout << "[PASS] Overlay engine: battle message decode (OAM + pool + VRAM glyph match, prefix/foreign/bitmap stand-downs)"
              << std::endl;
}

// Runtime part: the observer logic fed frame by frame (no PPU). Matched OAM entries are disabled while the frame is rendered
// and restored when the PPU latches it.
void test_battle_runtime() {
    eng::set_memory_reader_for_tests(&kReader);
    bt::set_memory_reader_for_tests(&kReader);
    bt::reset();
    bt::set_enabled_for_tests(true);
    bt::set_viewport_for_tests(true);
    bt::set_view_for_decision(22, 284);
    // guest memory: the pool at 0x02034A88 -> 0x02005000, the count byte, the ROM glyph table (fake bitmaps)
    const std::vector<std::string> lines = {"Hi you", "ok"};
    g_mem.clear();
    const uint32_t pool = 0x02005000u;
    auto write_pool = [&](const std::vector<std::string>& l) {
        w32(bt::kBattlePoolPtrAddr, pool);
        const std::vector<eng::PoolEntryRaw> entries = make_pool(l, 16, 12);
        w8(eng::kPoolCountAddr, static_cast<uint8_t>(entries.size()));
        for (size_t i = 0; i < entries.size(); ++i) {
            const uint32_t e = pool + 24u * static_cast<uint32_t>(i);
            w32(e + 0, entries[i].x);
            w32(e + 4, entries[i].y);
            w32(e + 8, entries[i].handle);
            w8(e + 20, entries[i].color);
            w8(e + 21, entries[i].used);
            if (entries[i].handle) w32(entries[i].handle, entries[i].glyph_src);
        }
    };
    write_pool(lines);
    for (int g = 0; g < 256; ++g) {
        uint8_t b[128];
        fake_glyph(g, b);
        for (int i = 0; i < 128; i += 4)
            w32(eng::kFontTilesBase + 128u * static_cast<uint32_t>(g) + static_cast<uint32_t>(i),
                static_cast<uint32_t>(b[i]) | (static_cast<uint32_t>(b[i + 1]) << 8) | (static_cast<uint32_t>(b[i + 2]) << 16) |
                    (static_cast<uint32_t>(b[i + 3]) << 24));
    }
    BWorld w(lines, 5);   // five entries typed: "Hi yo" (4 glyph sprites)
    std::vector<uint8_t> oam, pal;
    w.to_bytes(oam);
    w.pal_bytes(pal);
    const std::vector<uint8_t> oam_before = oam;

    PageSnapshot snap;
    uint64_t tag = 0;
    int image_w = 0;
    // frame 1: decision made at scanline 0 -> the matched sprites are disabled in the live OAM; frame_ready restores them
    bt::feed_scanline0_for_tests(1000, 0x1541, w.vram.data(), oam.data(), pal.data());
    assert(oam != oam_before);
    int disabled = 0;
    for (int i = 0; i < 128; ++i) if ((oam[i * 8 + 1] & 0x02) && !(oam_before[i * 8 + 1] & 0x02)) ++disabled;
    assert(disabled == 4);
    for (int i = 0; i < 128; ++i) {   // unrelated entries are untouched
        const bool ours = i >= BWorld::sprite_of(0) && i <= BWorld::sprite_of(4) && i != BWorld::sprite_of(2);
        if (!ours) assert(std::memcmp(&oam[i * 8], &oam_before[i * 8], 8) == 0);
    }
    assert(!bt::latched_snapshot(&snap, &tag, &image_w));   // nothing latched before frame_ready
    bt::feed_frame_ready_for_tests(1000);
    assert(oam == oam_before);                              // restored for the guest's vblank code
    assert(bt::latched_snapshot(&snap, &tag, &image_w));
    assert(snap.valid && snap.source == 1 && snap.revealed == 5 && snap.glyphs.size() == 8 && image_w == 284);
    assert(snap.glyphs[0].gx == 38.0f && snap.page_id >= bt::kBattlePageIdBase);
    const uint32_t id1 = snap.page_id;

    // a guest write to an entry in the middle of the frame is not undone by the restore
    bt::feed_scanline0_for_tests(1001, 0x1541, w.vram.data(), oam.data(), pal.data());
    const int victim = BWorld::sprite_of(0);
    oam[victim * 8] = 0x77;                                  // the guest rewrites attr0 of a hidden entry
    bt::feed_frame_ready_for_tests(1001);
    assert(oam[victim * 8] == 0x77);
    oam[victim * 8] = oam_before[victim * 8];

    // typewriter step: same page id, more revealed
    w.put_sprite(5);
    w.to_bytes(oam);
    bt::feed_scanline0_for_tests(1002, 0x1541, w.vram.data(), oam.data(), pal.data());
    bt::feed_frame_ready_for_tests(1002);
    assert(bt::latched_snapshot(&snap, &tag, &image_w) && snap.page_id == id1 && snap.revealed == 6);

    // a frame whose scanline-0 decision is not the frame that gets latched (numbers differ) latches nothing
    bt::feed_scanline0_for_tests(1003, 0x1541, w.vram.data(), oam.data(), pal.data());
    bt::feed_frame_ready_for_tests(2000);
    assert(!bt::latched_snapshot(&snap, &tag, &image_w));

    // viewport unknown / overlay off: no decision, the OAM is never touched
    w.to_bytes(oam);
    const std::vector<uint8_t> oam2 = oam;
    bt::set_viewport_for_tests(false);
    bt::feed_scanline0_for_tests(1004, 0x1541, w.vram.data(), oam.data(), pal.data());
    assert(oam == oam2);
    bt::feed_frame_ready_for_tests(1004);
    assert(!bt::latched_snapshot(&snap, &tag, &image_w));
    bt::set_viewport_for_tests(true);
    bt::set_enabled_for_tests(false);
    bt::feed_scanline0_for_tests(1005, 0x1541, w.vram.data(), oam.data(), pal.data());
    assert(oam == oam2);
    bt::feed_frame_ready_for_tests(1005);
    assert(!bt::latched_snapshot(&snap, &tag, &image_w));
    bt::set_enabled_for_tests(true);

    // new page (other words at the same cells): while the old sprites are still in OAM the bitmaps differ -> engine draws
    write_pool({"Ho you", "ok"});
    bt::feed_scanline0_for_tests(1006, 0x1541, w.vram.data(), oam.data(), pal.data());   // old sprites, new pool: 'i' != 'o'
    assert(oam == oam2);                                                                    // nothing hidden
    bt::feed_frame_ready_for_tests(1006);
    assert(!bt::latched_snapshot(&snap, &tag, &image_w));
    {   // then the engine draws the new page's sprites: a new page id
        BWorld w2({"Ho you", "ok"}, 3);
        std::vector<uint8_t> oam3;
        w2.to_bytes(oam3);
        bt::feed_scanline0_for_tests(1007, 0x1541, w2.vram.data(), oam3.data(), pal.data());
        bt::feed_frame_ready_for_tests(1007);
        assert(bt::latched_snapshot(&snap, &tag, &image_w) && snap.page_id != id1 && snap.revealed == 2);
    }
    // reset() drops everything
    bt::reset();
    assert(!bt::latched_snapshot(&snap, &tag, &image_w));

    bt::set_memory_reader_for_tests(nullptr);
    eng::set_memory_reader_for_tests(nullptr);
    g_mem.clear();
    std::cout << "[PASS] Overlay engine: battle observer flow (disable in OAM for the frame, restore at latch, pairing, page ids)"
              << std::endl;
}

}  // namespace

int run_overlay_engine_tests(bool verbose) {
    (void)verbose;
    test_decoding();
    test_predicate_table();
    test_hook_flow_and_inertness();
    test_fades();
    test_battle_decode();
    test_battle_runtime();
    return 0;
}
