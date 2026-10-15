#include "engine_hooks.h"

#include "../dialogue_backlog.h"      // ScopedInternalBusRead
#include "../widescreen_adapter.h"    // khcom_is_battle_active
#include "gbaimp/widescreen.h"         // Widescreen::effective_extra_pixels
#include "battle_text.h"
#include "fade_sampler.h"
#include "text_adapter.h"
#include "armv4t/runtime_arm.h"       // g_cpu, bus_read_*
#include "gba/gba_ppu.h"
#include "runtime_bus_bridge.h"        // active_ppu

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" unsigned long long g_runtime_vblank_starts;

namespace khcom::overlay::engine {

// =================================================================================================
// Pure decoding
// =================================================================================================
int fixed_to_px(uint32_t v) { return static_cast<int32_t>(v << 8) >> 16; }

float fixed_to_float(uint32_t v) { return static_cast<float>(static_cast<int32_t>(v << 8)) / 65536.0f; }

bool is_supported_word(uint16_t w) {
    return (w >= 0x20 && w <= 0xFF) || w == 0x0A || w == 0xE000 || w == 0x4079 || w == 0x4000;
}

bool is_drawable_word(uint16_t w) { return w != 0 && w != 0x0A && w != 0x4079 && w != 0x4000; }

namespace {
// Glyph index the engine uses for a supported word (kept local so this module has no dependency on any text model).
int glyph_of(uint16_t w) {
    if (w >= 0x20 && w <= 0xFF) return w;
    if (w == 0xE000) return 25;
    return -1;
}

// The 4 known text origins (placement style x top/bottom): x0 in {16, 72}, y0 in {12, 108} GBA px.
bool known_placement(int x, int y) {
    return (x == 16 || x == 72) && (y == 12 || y == 108);
}
}  // namespace

const char* reason_name(Reason r) {
    static const char* k[] = {"overlay",     "disabled",         "battle",       "no-viewport", "subtitle",
                              "no-layout",   "outside-cutscene", "bad-range",    "unsupported-word",
                              "bad-counters", "no-pool",         "pool-mismatch", "bad-placement",
                              "cannot-draw", "no-draw-call"};
    const int i = static_cast<int>(r);
    return (i >= 0 && i < static_cast<int>(Reason::Count_)) ? k[i] : "?";
}

Reason evaluate(const RawPage& raw, const Env& env, PageSnapshot* out,
                bool (*can_draw)(const PageSnapshot&, void*), void* can_draw_ctx) {
    PageSnapshot local;
    PageSnapshot& s = out ? *out : local;
    s = PageSnapshot{};
    s.frame = raw.frame;
    s.page_id = raw.page_id;

    if (!env.enabled) return Reason::Disabled;
    if (raw.kind == kPageKindSubtitle) return Reason::Subtitle;
    if (env.battle) return Reason::Battle;
    if (!env.viewport_ok) return Reason::NoViewport;
    if (raw.start_ptr == 0 || raw.layout_window != raw.window) return Reason::NoLayout;
    if (raw.start_ptr < kCutsceneTextBegin || raw.start_ptr >= kCutsceneTextEnd) return Reason::OutsideCutsceneRange;
    if (!raw.words_ok || raw.words.empty()) return Reason::BadRange;

    int drawable = 0;
    for (uint16_t w : raw.words) {
        if (!is_supported_word(w)) return Reason::UnsupportedWord;
        if (is_drawable_word(w)) ++drawable;
    }
    if (raw.total == 0 || raw.total > kPoolEntries || raw.revealed > raw.total) return Reason::BadCounters;
    if (drawable != raw.total || raw.pool_count != raw.total || static_cast<int>(raw.pool.size()) != raw.total)
        return Reason::BadCounters;
    if (!raw.pool_valid) return Reason::NoPool;

    // Pool == page: the i-th pool entry is the i-th drawable word; glyph identity comes from the ROM source pointer
    // of the tile allocation (spaces have no tile).
    s.text = raw.words;
    s.glyphs.reserve(raw.pool.size());
    size_t pi = 0;
    for (uint16_t w : raw.words) {
        if (!is_drawable_word(w)) continue;
        const PoolEntryRaw& e = raw.pool[pi++];
        if (e.used != 1) return Reason::PoolMismatch;
        if (w != 0x20) {
            const int g = glyph_of(w);
            if (g < 0 || e.handle == 0 || e.glyph_src != kFontTilesBase + 128u * static_cast<uint32_t>(g))
                return Reason::PoolMismatch;
        } else if (e.handle != 0 && e.glyph_src != kFontTilesBase + 128u * 0x20u) {
            return Reason::PoolMismatch;
        }
        PageGlyph g;
        g.word = w;
        g.gx = fixed_to_float(e.x);
        g.gy = fixed_to_float(e.y);
        g.color = e.color;
        s.glyphs.push_back(g);
    }

    const int px0 = fixed_to_px(raw.pool.front().x), py0 = fixed_to_px(raw.pool.front().y);
    if (env.placement_check && (px0 != raw.x0 || py0 != raw.y0 || !known_placement(raw.x0, raw.y0)))
        return Reason::BadPlacement;

    s.origin_x = static_cast<float>(raw.x0);
    s.origin_y = static_cast<float>(raw.y0);
    s.revealed = raw.revealed;
    s.page_complete = raw.revealed >= raw.total && raw.state != 0;
    if (raw.y0 == 108) {   // bottom placements: box measured on two captured pages (docs/TEXT_OVERLAY_ENGINE.md)
        s.box_x = static_cast<float>(raw.x0 - 11);
        s.box_y = 100.0f;
        s.box_w = 175.0f;
        s.box_h = 52.0f;
    }
    s.engine_text_rgba = raw.text_rgba;
    s.engine_shadow_rgba = raw.shadow_rgba;
    s.wrap_px = 155;
    s.line_pitch = 12;
    s.max_lines = 3;

    if (can_draw) {
        s.valid = true;   // the render side sees a valid snapshot when asked
        if (!can_draw(s, can_draw_ctx)) {
            s.valid = false;
            return Reason::CannotDraw;
        }
    }
    s.valid = true;
    return Reason::Overlay;
}

// =================================================================================================
// Runtime
// =================================================================================================
namespace {

struct Slot {
    uint64_t tag = ~0ull;
    bool overlay = false;
    Reason reason = Reason::Disabled;
    PageSnapshot snap;
};
constexpr int kRing = 8;

Slot s_ring[kRing];
Slot s_pending;                 // decision of the current window update, committed by the draw hook
bool s_pending_valid = false;
uint32_t s_pending_window = 0;

// layout capture (0x08064EF4 entry)
uint32_t s_lay_window = 0, s_lay_start = 0;
int32_t s_lay_x0 = 0, s_lay_y0 = 0;
uint32_t s_page_id = 0;

uint64_t (*s_frame_fn)() = nullptr;
uint64_t (*s_ppu_fn)() = nullptr;
uint64_t s_last_ppu = 0;
uint32_t s_dump_page = 0;
uint64_t s_last_frame = 0;
int s_last_rev = 0, s_last_total = 0;   // fade log only
uint64_t s_last_window_frame = 0;   // frame of the last dialogue window update (fade log only)
int s_lag = -1;                 // -1 = not resolved yet
bool s_nohide = false;
int s_force_bldcnt = -1, s_force_bldy = 0;
int s_force_pal_mode = 0, s_force_pal_max = 0, s_force_pal_ramp = 1;   // KHCOM_FORCE_PALFADE=<b|d>:<max 1..16>:<start PPU frame>:<ramp frames>
uint64_t s_force_pal_start = 0;
uint16_t s_force_pal_orig[512];
bool s_force_pal_have = false;   // KHCOM_FORCE_BLD=<hex BLDCNT>:<BLDY>: calibration aid that makes the PPU fade the screen
bool s_fade_log = false;   // KHCOM_FADE_LOG=1: per-present dump of the PPU blend registers (calibration aid, works with the overlay off)
bool s_log = false, s_log_init = false;
int s_log_lines = 0;
Stats s_stats;

// last known presentation target (for can_draw at window-update time)
SDL_Rect s_dst{0, 0, 0, 0};
SDL_Renderer* s_renderer = nullptr;
uint64_t s_dst_present_seen = 0;

// PPU frame counter (restored by a savestate load, unlike g_runtime_vblank_starts): used to detect discontinuities.
uint64_t ppu_frame() {
    if (s_frame_fn) return s_ppu_fn ? s_ppu_fn() : s_frame_fn();
    const gba::GbaPpu* p = gbarecomp::active_ppu();
    return p ? p->frame_count() : 0;
}

uint64_t frame_now() {
    return s_frame_fn ? s_frame_fn() : static_cast<uint64_t>(g_runtime_vblank_starts);
}

void init_env() {
    if (s_log_init) return;
    s_log_init = true;
    const char* v = std::getenv("KHCOM_OVERLAY_LOG");
    s_log = v && *v && std::strcmp(v, "0") != 0;
    const char* nh = std::getenv("KHCOM_OVERLAY_NOHIDE");
    s_nohide = nh && *nh && std::strcmp(nh, "0") != 0;
    if (const char* fb = std::getenv("KHCOM_FORCE_BLD")) {
        unsigned c = 0, y = 0;
        if (std::sscanf(fb, "%x:%u", &c, &y) == 2) { s_force_bldcnt = static_cast<int>(c); s_force_bldy = static_cast<int>(y); }
    }
    if (const char* fp = std::getenv("KHCOM_FORCE_PALFADE")) {
        char m = 0;
        unsigned mx = 0, ramp = 1;
        unsigned long long st = 0;
        if (std::sscanf(fp, "%c:%u:%llu:%u", &m, &mx, &st, &ramp) >= 3 && (m == 'b' || m == 'd')) {
            s_force_pal_mode = m == 'b' ? 2 : 3;
            s_force_pal_max = static_cast<int>(mx > 16 ? 16 : mx);
            s_force_pal_start = st;
            s_force_pal_ramp = ramp ? static_cast<int>(ramp) : 1;
        }
    }
    const char* fl = std::getenv("KHCOM_FADE_LOG");
    s_fade_log = fl && *fl && std::strcmp(fl, "0") != 0;
    const char* lag = std::getenv("KHCOM_OVERLAY_LAG");
    if (lag && *lag) s_lag = std::atoi(lag);
    if (s_lag < 0) s_lag = 2;   // calibrated default (docs/TEXT_OVERLAY_ENGINE.md)
}

void fade_log_present() {
    fade_sampler_attach();
    FrameBlend fb;
    if (!fade_sampler_last_frame(&fb)) return;
    ScopedInternalBusRead guard;
    const uint16_t bd = bus_read_u16(0x05000000u), e1 = bus_read_u16(0x05000200u + 2), e2 = bus_read_u16(0x05000200u + 4);
    const uint16_t dispcnt = bus_read_u16(0x04000000u);
    const bool dlg = s_last_window_frame != 0 && frame_now() <= s_last_window_frame + 2;
    int lum = -1;   // mean luminance (0..255) of the latched framebuffer, to spot palette fades
    if (const gba::GbaPpu* ppu = gbarecomp::active_ppu(); ppu && ppu->has_latched_framebuffer()) {
        const uint8_t* fb = ppu->latched_framebuffer();
        const size_t n = ppu->render_bytes() / 3;
        uint64_t acc = 0, cnt = 0;
        for (size_t i = 0; i < n; i += 7, ++cnt) acc += (fb[i * 3] * 77u + fb[i * 3 + 1] * 150u + fb[i * 3 + 2] * 29u) >> 8;
        lum = cnt ? static_cast<int>(acc / cnt) : -1;
    }
    static int plum = -99, ptxt = 0;
    static BlendRegs prev;
    static uint16_t pbd = 0, pe1 = 0, pe2 = 0;
    static bool pdlg = false, have_prev = false;
    const BlendRegs& a = fb.line[0];
    const BlendRegs& b = fb.line[100];
    const BlendRegs& c = fb.line[159];
    bool varies = false;
    for (int y = 1; y < 160; ++y) if (!(fb.line[y] == fb.line[0])) { varies = true; break; }
    if (have_prev && prev == a && !varies && pbd == bd && pe1 == e1 && pe2 == e2 && pdlg == dlg && plum == lum && ptxt == (dlg ? s_last_rev : 0)) return;
    prev = a; pbd = bd; pe1 = e1; pe2 = e2; pdlg = dlg; plum = lum; ptxt = dlg ? s_last_rev : 0;
    have_prev = true;
    std::fprintf(stderr, "[fade] V=%llu P=%llu dlg=%d txt=%d lum=%d dc=%04X bd=%04X e1=%04X e2=%04X l0=%04X/%04X/%02X l100=%04X/%04X/%02X l159=%04X/%04X/%02X%s\n",
                 static_cast<unsigned long long>(frame_now()), static_cast<unsigned long long>(ppu_frame()), dlg ? 1 : 0, dlg ? s_last_rev : 0, lum, dispcnt, bd, e1, e2,
                 a.bldcnt, a.bldalpha, a.bldy, b.bldcnt, b.bldalpha, b.bldy, c.bldcnt, c.bldalpha, c.bldy, varies ? " VARIES" : "");
}

// Screen fade of the presented frame (docs/TEXT_OVERLAY_ENGINE.md "Screen fades"). Independent of WHEN in the present
// sequence it is called: it reads the last completed frame's blend registers and the page's sampled text colours.
//  1. PPU blend registers of the text's scanline (BLDCNT effect 2 / 3 with the OBJ layer as first target, or alpha);
//  2. otherwise the engine's palette fades, seen as the engine text colours drifting from the ones the page started with.
void apply_fade(PageSnapshot& snap, DrawInfo& di) {
    static uint32_t ref_page = ~0u, ref_body = 0, ref_shadow = 0;
    if (snap.page_id != ref_page) {
        ref_page = snap.page_id;
        ref_body = snap.engine_text_rgba;
        ref_shadow = snap.engine_shadow_rgba;
    }
    FadeState f;
    if (kPpuBlendsNormalObj) {   // inert on the bundled PPU (see gbaimp/text_fade.h), so no per-scanline observer is installed
        fade_sampler_attach();
        FrameBlend fb;
        if (fade_sampler_last_frame(&fb)) f = fb.state_at(static_cast<int>(snap.origin_y) + 14);
    }
    bool from_palette = false;
    if (f.mode == FadeMode::None) {
        f = fade_from_text_colours(ref_body, ref_shadow, snap.engine_text_rgba, snap.engine_shadow_rgba);
        from_palette = f.mode != FadeMode::None;
    }
    snap.fade_mode = static_cast<int>(f.mode);
    snap.fade = f.level;
    if (f.mode == FadeMode::Alpha) {
        di.global_alpha = 1.0f - f.level;
    } else {
        di.fade_mode = static_cast<int>(f.mode);
        di.fade_level = f.level;
        di.fade_from_palette = from_palette;
    }
}

void draw_battle_text();   // defined below (needs apply_fade / make_info)

// Width (GBA px) of the image presented in s_dst: 240, or 240 + both margins while the 16:9 battle view is shown.
int presented_image_width() {
    int l = 0, r = 0;
    gbaimp::Widescreen::instance().effective_extra_pixels(&l, &r);
    return 240 + l + r;
}

DrawInfo make_info() {
    DrawInfo di;
    di.renderer = s_renderer;
    di.dst_x = s_dst.x;
    di.dst_y = s_dst.y;
    di.dst_w = s_dst.w;
    di.dst_h = s_dst.h;
    di.gba_w = presented_image_width();
    return di;
}

bool battle_can_draw_cb(const PageSnapshot& snap) {
    return s_renderer != nullptr && s_dst.w > 0 && can_draw_page(snap, make_info());
}

bool can_draw_cb(const PageSnapshot& snap, void*) {
    return can_draw_page(snap, make_info());
}

std::string text_of(const std::vector<uint16_t>& words) {
    std::string t;
    for (uint16_t w : words) {
        if (w == 0x0A) t += "\\n";
        else if (w >= 0x20 && w < 0x7F) t += static_cast<char>(w);
        else {
            char b[12];
            std::snprintf(b, sizeof(b), "<%X>", w);
            t += b;
        }
    }
    return t;
}

void log_slot(const char* what, uint64_t tag, const RawPage& raw, Reason r, const PageSnapshot& snap) {
    if (!s_log || s_log_lines > 400000) return;
    ++s_log_lines;
    std::fprintf(stderr,
                 "[overlay] %s V=%llu P=%llu pg=%u W=%08X kind=%02X type=%u pl=%u idx=%u rev=%u/%u st=%u start=%08X cont=%08X "
                 "o=(%d,%d) poolN=%u reason=%s",
                 what, static_cast<unsigned long long>(tag), static_cast<unsigned long long>(ppu_frame()), raw.page_id, raw.window, raw.kind, raw.type,
                 raw.placement, raw.page_index, raw.revealed, raw.total, raw.state, raw.start_ptr, raw.cont_ptr, raw.x0,
                 raw.y0, raw.pool_count, reason_name(r));
    if (!snap.glyphs.empty() && !raw.pool.empty()) {
        std::fprintf(stderr, " g0=(%.2f,%.2f)", snap.glyphs.front().gx, snap.glyphs.front().gy);
        const size_t k = std::min<size_t>(raw.revealed, snap.glyphs.size());
        if (k > 0) std::fprintf(stderr, " gl=(%.2f,%.2f)", snap.glyphs[k - 1].gx, snap.glyphs[k - 1].gy);
    }
    if (!raw.words.empty()) std::fprintf(stderr, " \"%s\"", text_of(raw.words).c_str());
    std::fprintf(stderr, "\n");
}

const MemReader* s_reader = nullptr;
uint32_t rd32(uint32_t a) { return s_reader ? s_reader->r32(a) : bus_read_u32(a); }
uint16_t rd16(uint32_t a) { return s_reader ? s_reader->r16(a) : bus_read_u16(a); }
uint8_t rd8(uint32_t a) { return s_reader ? s_reader->r8(a) : bus_read_u8(a); }

uint32_t bgr555_to_rgba(uint16_t c) {
    auto e = [](uint32_t v) { return (v << 3) | (v >> 2); };
    return (e(c & 31u) << 24) | (e((c >> 5) & 31u) << 16) | (e((c >> 10) & 31u) << 8) | 0xFFu;
}

// Reads the window object, the page words and the glyph pool. Guest memory is only read (idle-safe regions).
void read_raw(uint32_t W, RawPage* raw) {
    raw->window = W;
    raw->frame = frame_now();
    raw->placement = rd8(W + 0x20);
    raw->revealed = rd8(W + 0x24);
    raw->total = rd8(W + 0x26);
    raw->page_index = rd8(W + 0x27);
    raw->type = rd8(W + 0x28);
    raw->state = rd8(W + 0x2B);
    raw->cont_ptr = rd32(W + 0x40);
    const uint32_t table = rd32(W + 0x3C);
    raw->kind = static_cast<uint16_t>(rd32(table + 24u * raw->page_index) & 0xFFFFu);
    raw->start_ptr = s_lay_start;
    raw->layout_window = s_lay_window;
    raw->x0 = s_lay_x0;
    raw->y0 = s_lay_y0;
    raw->page_id = s_page_id;
    raw->text_rgba = bgr555_to_rgba(rd16(0x05000200u + 2));      // OBJ palette bank 0, entry 1 (body)
    raw->shadow_rgba = bgr555_to_rgba(rd16(0x05000200u + 4));    // entry 2 (shadow)

    // page words
    raw->words.clear();
    raw->words_ok = false;
    const uint32_t start = s_lay_start;
    if (start >= kCutsceneTextBegin && start < kCutsceneTextEnd && (start & 1u) == 0) {
        const uint32_t cont = raw->cont_ptr;
        bool ok = true;
        uint32_t a = start;
        for (int i = 0; i < kMaxPageWords + 1; ++i, a += 2) {
            if (cont != 0 && a == cont) break;
            const uint16_t w = rd16(a);
            if (w == 0) {
                if (cont != 0) ok = false;   // terminator before the continuation: inconsistent
                break;
            }
            if (i >= kMaxPageWords) { ok = false; break; }
            raw->words.push_back(w);
        }
        raw->words_ok = ok;
    }

    // pool
    raw->pool_count = rd8(kPoolCountAddr);
    const uint32_t pool = rd32(kPoolPtrAddr);
    raw->pool_valid = (pool >> 24) == 0x02 || (pool >> 24) == 0x03;
    raw->pool.clear();
    if (raw->pool_valid && raw->total <= kPoolEntries) {
        raw->pool.reserve(raw->total);
        for (int i = 0; i < raw->total; ++i) {
            const uint32_t e = pool + static_cast<uint32_t>(i) * kPoolStride;
            PoolEntryRaw p;
            p.x = rd32(e + 0);
            p.y = rd32(e + 4);
            p.handle = rd32(e + 8);
            p.color = rd8(e + 20);
            p.used = rd8(e + 21);
            p.glyph_src = (p.handle >> 24) == 0x02 || (p.handle >> 24) == 0x03 ? rd32(p.handle) : 0;
            raw->pool.push_back(p);
        }
    }
}

// ---- investigation aid (env-gated, documented in docs/TEXT_OVERLAY_ENGINE.md "Battle dialogue") -------------------
// KHCOM_OVERLAY_TRACE="0805A000-0805D000,08064B00-08066000" logs every guest function entry in those ranges
// (pc, R0-R4, LR) while the PPU frame is inside KHCOM_OVERLAY_TRACE_FRAMES="a-b"; every glyph-pool draw call
// (0x080658B8, any caller) additionally dumps the pool. Needs the hook (overlay on or KHCOM_OVERLAY_ENGINE_LOG=1).
struct TraceRange { uint32_t lo, hi; };
std::vector<TraceRange> s_trace_ranges;
uint64_t s_trace_a = 0, s_trace_b = ~0ull;
bool s_trace_init = false, s_trace_on = false;

void init_trace() {
    s_trace_init = true;
    const char* v = std::getenv("KHCOM_OVERLAY_TRACE");
    if (!v || !*v) return;
    const char* p = v;
    while (*p) {
        unsigned lo = 0, hi = 0;
        int n = 0;
        if (std::sscanf(p, "%x-%x%n", &lo, &hi, &n) == 2) {
            s_trace_ranges.push_back({lo, hi});
            p += n;
        } else {
            break;
        }
        if (*p == ',') ++p;
    }
    if (const char* f = std::getenv("KHCOM_OVERLAY_TRACE_FRAMES")) {
        unsigned long long a = 0, b = ~0ull;
        if (std::sscanf(f, "%llu-%llu", &a, &b) >= 1) { s_trace_a = a; s_trace_b = b; }
    }
    s_trace_on = !s_trace_ranges.empty() || std::getenv("KHCOM_OVERLAY_TRACE_POOL") != nullptr;
}

void trace_pool(const char* tag) {
    ScopedInternalBusRead guard;
    const uint32_t pool = bus_read_u32(kPoolPtrAddr);
    const unsigned cnt = bus_read_u8(kPoolCountAddr);
    std::fprintf(stderr, "[trace] %s pool=%08X cnt=%u R0=%u LR=%08X R4=%08X:", tag, pool, cnt, g_cpu.R[0], g_cpu.R[14], g_cpu.R[4]);
    if ((pool >> 24) == 2 || (pool >> 24) == 3) {
        const unsigned n = std::min<unsigned>(std::max<unsigned>(cnt, g_cpu.R[0] & 0xFF), 48);
        for (unsigned i = 0; i < n; ++i) {
            const uint32_t e = pool + i * kPoolStride;
            const uint32_t h = bus_read_u32(e + 8);
            const uint32_t src = ((h >> 24) == 2 || (h >> 24) == 3) ? bus_read_u32(h) : 0;
            const int gl = src >= kFontTilesBase ? static_cast<int>((src - kFontTilesBase) / 128u) : -1;
            std::fprintf(stderr, " [%u](%d,%d)g%d c%u u%u", i, fixed_to_px(bus_read_u32(e)), fixed_to_px(bus_read_u32(e + 4)), gl,
                         bus_read_u8(e + 20), bus_read_u8(e + 21));
        }
    }
    std::fputc(10, stderr);
}

// KHCOM_OVERLAY_OAMDUMP=<ppu frame>: dumps OAM (visible entries) + OBJ palette banks 0..2 and the display registers at that
// PPU frame, at present time (calibration aid: which sprites are the text glyphs, where they really are).
void oam_summary_if_due() {
    static uint64_t a = 0, b = 0;
    static bool init = false;
    if (!init) {
        init = true;
        if (const char* v = std::getenv("KHCOM_OVERLAY_OAMSUM")) {
            unsigned long long x = 0, y = 0;
            if (std::sscanf(v, "%llu-%llu", &x, &y) == 2) { a = x; b = y; }
        }
    }
    if (b == 0) return;
    const uint64_t p = ppu_frame();
    if (p < a || p > b) return;
    ScopedInternalBusRead guard;
    int n = 0, minx = 999, maxx = -999;
    uint32_t h = 2166136261u;
    for (int i = 0; i < 128; ++i) {
        const uint32_t ad = 0x07000000u + 8u * static_cast<uint32_t>(i);
        const uint16_t a0 = bus_read_u16(ad), a1 = bus_read_u16(ad + 2), a2 = bus_read_u16(ad + 4);
        if ((a0 & 0x0300) == 0x0200 || (a0 >> 14) != 0 || (a1 >> 14) != 1 || (a2 >> 12) != 5) continue;
        ++n;
        const int x = (a1 & 511) >= 256 ? (a1 & 511) - 512 : (a1 & 511);
        minx = std::min(minx, x);
        maxx = std::max(maxx, x);
        h = (h ^ a0) * 16777619u; h = (h ^ a1) * 16777619u; h = (h ^ a2) * 16777619u;
    }
    std::fprintf(stderr, "[oamsum] P=%llu text16=%d x=%d..%d hash=%08X\n", static_cast<unsigned long long>(p), n, minx, maxx, h);
}

void oam_dump_if_due() {
    static const uint64_t due = [] {
        const char* v = std::getenv("KHCOM_OVERLAY_OAMDUMP");
        return v && *v ? static_cast<uint64_t>(std::strtoull(v, nullptr, 10)) : 0ull;
    }();
    if (due == 0) return;
    static uint64_t last = 0;
    const uint64_t p = ppu_frame();
    if (p < due || p > due + 3 || p == last) return;
    last = p;
    ScopedInternalBusRead guard;
    std::fprintf(stderr, "[oam] P=%llu dispcnt=%04X\n", static_cast<unsigned long long>(p), bus_read_u16(0x04000000u));
    for (int i = 0; i < 128; ++i) {
        const uint32_t a = 0x07000000u + 8u * static_cast<uint32_t>(i);
        const uint16_t a0 = bus_read_u16(a), a1 = bus_read_u16(a + 2), a2 = bus_read_u16(a + 4);
        if ((a0 & 0x0300) == 0x0200) continue;   // disabled
        std::fprintf(stderr, "[oam] %3d a0=%04X a1=%04X a2=%04X y=%d x=%d shape=%d size=%d tile=%d pal=%d prio=%d mode=%d\n", i, a0, a1, a2,
                     a0 & 255, (a1 & 511) >= 256 ? (a1 & 511) - 512 : (a1 & 511), a0 >> 14, a1 >> 14, a2 & 1023, a2 >> 12, (a2 >> 10) & 3,
                     (a0 >> 10) & 3);
    }
    for (int b = 0; b < 3; ++b) {
        std::fprintf(stderr, "[oam] objpal%d:", b);
        for (int k = 0; k < 16; ++k) std::fprintf(stderr, " %04X", bus_read_u16(0x05000200u + 32u * b + 2u * k));
        std::fputc(10, stderr);
    }
}

// Sprite lists the OAM builder (0x08002F50) consumes: raw records and object records (calibration aid, same switches).
void trace_sprite_lists() {
    ScopedInternalBusRead guard;
    const uint32_t base = bus_read_u32(0x030074C8u);
    if ((base >> 24) != 2 && (base >> 24) != 3) return;
    const unsigned nobj = bus_read_u16(base + 0x28A8u), nraw = bus_read_u16(base + 0x2BACu);
    std::fprintf(stderr, "[spr] base=%08X nobj=%u nraw=%u raw:", base, nobj, nraw);
    for (unsigned i = 0; i < nraw && i < 64; ++i) {
        const uint32_t r = base + 0x28ACu + 24u * i;
        std::fprintf(stderr, " [%04X %04X %04X %04X]", bus_read_u16(r), bus_read_u16(r + 2), bus_read_u16(r + 4), bus_read_u16(r + 6));
    }
    std::fprintf(stderr, " obj:");
    for (unsigned i = 0; i < nobj && i < 64; ++i) {
        const uint32_t r = base + 0x1AA8u + 24u * i;
        std::fprintf(stderr, " [x%d y%d t=%08X d=%08X]", static_cast<int16_t>(bus_read_u16(r + 0x10)), static_cast<int16_t>(bus_read_u16(r + 0x12)),
                     bus_read_u32(r), bus_read_u32(r + 4));
    }
    std::fputc(10, stderr);
}

void trace_entry(uint32_t pc) {
    const uint64_t p = ppu_frame();
    if (p < s_trace_a || p > s_trace_b) return;
    if (pc == 0x08002F50u) trace_sprite_lists();
    if (pc == kFnDraw) trace_pool("draw");
    for (const TraceRange& r : s_trace_ranges)
        if (pc >= r.lo && pc < r.hi) {
            std::fprintf(stderr, "[trace] P=%llu pc=%08X r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X lr=%08X\n", static_cast<unsigned long long>(p), pc,
                         g_cpu.R[0], g_cpu.R[1], g_cpu.R[2], g_cpu.R[3], g_cpu.R[4], g_cpu.R[14]);
            return;
        }
}

void commit(Slot&& slot) {
    Slot& dst = s_ring[slot.tag % kRing];
    dst = std::move(slot);
}

void window_update(uint32_t W) {
    init_env();
    const uint64_t tag = frame_now();
    // Savestate load / rewind: the PPU frame counter (restored with the state) stops advancing in step with the
    // vblank counter (not restored). Anything captured before is stale: forget it, the engine owns the page.
    const uint64_t ppu = ppu_frame();
    if (s_last_frame != 0) {
        const int64_t dv = static_cast<int64_t>(tag) - static_cast<int64_t>(s_last_frame);
        const int64_t dp = static_cast<int64_t>(ppu) - static_cast<int64_t>(s_last_ppu);
        if (dv < 0 || dp - dv > 1 || dv - dp > 1) reset();
    }
    s_last_frame = tag;
    s_last_ppu = ppu;
    s_last_window_frame = tag;

    ScopedInternalBusRead guard;
    RawPage raw;
    read_raw(W, &raw);
    s_last_rev = raw.revealed;
    s_last_total = raw.total;

    Env env;
    env.enabled = mode() != Mode::Off;
    env.battle = khcom_is_battle_active();
    env.viewport_ok = s_renderer != nullptr && s_dst.w > 0 && s_dst.h > 0;

    if (s_log && std::getenv("KHCOM_OVERLAY_DUMP") && raw.revealed >= 1 && raw.pool_valid && !raw.pool.empty() &&
        raw.page_id != s_dump_page) {   // calibration aid: glyph resource objects + OBJ palette entries 1,2 of every bank
        s_dump_page = raw.page_id;
        const uint32_t pool = rd32(kPoolPtrAddr);
        const uint32_t e = pool;   // first entry
        std::fprintf(stderr, "[overlay] dump pg=%u entry0:", raw.page_id);
        for (int k = 0; k < 6; ++k) std::fprintf(stderr, " %08X", rd32(e + 4 * k));
        for (int which = 12; which <= 16; which += 4) {
            const uint32_t res = rd32(e + which);
            std::fprintf(stderr, "\n[overlay] dump res@+%d=%08X:", which, res);
            if ((res >> 24) == 2 || (res >> 24) == 3)
                for (int k = 0; k < 12; ++k) std::fprintf(stderr, " %08X", rd32(res + 4 * k));
        }
        std::fprintf(stderr, "\n[overlay] dump objpal(1,2):");
        for (int b = 0; b < 16; ++b)
            std::fprintf(stderr, " b%d=%04X/%04X", b, rd16(0x05000200u + 32u * b + 2), rd16(0x05000200u + 32u * b + 4));
        std::fputc(10, stderr);
    }

    Slot slot;
    slot.tag = tag;
    ++s_stats.windows;
    slot.reason = evaluate(raw, env, &slot.snap, mode() == Mode::On ? &can_draw_cb : nullptr, nullptr);
    slot.overlay = (slot.reason == Reason::Overlay);
    log_slot(slot.overlay ? "win" : "win(stand-down)", tag, raw, slot.reason, slot.snap);

    if (s_pending_valid) ++s_stats.standdowns[static_cast<int>(Reason::NoDrawCall)];   // previous decision never saw its draw call
    if (slot.overlay) {
        s_pending = std::move(slot);
        s_pending_window = W;
        s_pending_valid = true;     // committed by the draw hook (it must see the expected draw call)
    } else {
        s_pending_valid = false;
        ++s_stats.standdowns[static_cast<int>(slot.reason)];
        commit(std::move(slot));
    }
}

// Calibration aid (KHCOM_FORCE_PALFADE): fades the WHOLE palette RAM towards white / black the way the engine's own
// palette fades do, ramping from the start frame, so authentic and overlay runs can be compared frame by frame.
void force_palette_fade() {
    const uint64_t p = ppu_frame();
    if (p < s_force_pal_start) return;
    if (!s_force_pal_have) {
        for (int i = 0; i < 512; ++i) s_force_pal_orig[i] = bus_read_u16(0x05000000u + 2u * static_cast<uint32_t>(i));
        s_force_pal_have = true;
    }
    const int y = std::min<int>(s_force_pal_max, static_cast<int>((p - s_force_pal_start + 1) * static_cast<uint64_t>(s_force_pal_max) / static_cast<uint64_t>(s_force_pal_ramp)));
    for (int i = 0; i < 512; ++i) {
        const uint16_t c = s_force_pal_orig[i];
        int ch[3] = {c & 31, (c >> 5) & 31, (c >> 10) & 31};
        for (int& v : ch) v = s_force_pal_mode == 2 ? v + (((31 - v) * y + 8) >> 4) : v - ((v * y + 7) >> 4);
        bus_write_u16(0x05000000u + 2u * static_cast<uint32_t>(i), static_cast<uint16_t>(ch[0] | (ch[1] << 5) | (ch[2] << 10)));
    }
}

void draw_call() {
    // Only the dialogue window's own draw call (LR into 0x08073238, R4 = window object) is ever touched.
    if (g_cpu.R[14] != kDrawCallerLr) return;
    if (s_force_pal_mode != 0) force_palette_fade();
    if (s_force_bldcnt >= 0) {   // calibration aid (documented in docs/TEXT_OVERLAY_ENGINE.md): the PPU really fades the frame
        bus_write_u16(0x04000050u, static_cast<uint16_t>(s_force_bldcnt));
        bus_write_u16(0x04000054u, static_cast<uint16_t>(s_force_bldy));
    }
    if (!s_pending_valid) return;
    s_pending_valid = false;
    if (g_cpu.R[4] != s_pending_window) {   // not the window we decided for: leave everything to the engine
        ++s_stats.standdowns[static_cast<int>(Reason::NoDrawCall)];
        return;
    }
    Slot slot = std::move(s_pending);
    if (mode() == Mode::On && !s_nohide) {
        g_cpu.R[0] = 0;             // glyph count 0: the loop in 0x080658B8 submits nothing
        ++s_stats.hidden;
    }
    commit(std::move(slot));
}

void layout_call() {
    s_lay_x0 = fixed_to_px(g_cpu.R[0]);     // origin arguments are 8.8 fixed (72 px = 0x4800)
    s_lay_y0 = fixed_to_px(g_cpu.R[1]);
    s_lay_start = g_cpu.R[2];
    s_lay_window = g_cpu.R[3] - 0x40u;    // R3 = &W->continuation
    ++s_page_id;
    s_pending_valid = false;
    if (s_log && s_log_lines <= 400000) {
        ++s_log_lines;
        std::fprintf(stderr, "[overlay] layout V=%llu P=%llu W=%08X start=%08X x0=%d y0=%d pg=%u\n",
                     static_cast<unsigned long long>(frame_now()), static_cast<unsigned long long>(ppu_frame()), s_lay_window, s_lay_start, s_lay_x0, s_lay_y0,
                     s_page_id);
    }
}

}  // namespace

namespace {
// Battle message boxes (PPU observer path, battle_text.cpp): draws the snapshot that was latched together with the frame
// being presented. The engine glyph sprites of that frame were disabled in OAM (battle_text.cpp) while the PPU rendered it.
void draw_battle_text() {
    PageSnapshot snap;
    uint64_t tag = 0;
    int image_w = 0;
    if (!battle::latched_snapshot(&snap, &tag, &image_w)) return;
    DrawInfo di = make_info();
    if (di.gba_w != image_w) {   // the view changed between the frame and its presentation: cannot place the text
        ++s_stats.presents_missing;
        return;
    }
    apply_fade(snap, di);
    if (draw_page(snap, di)) ++s_stats.presents_drawn;
    else ++s_stats.presents_missing;
}
}  // namespace

Mode mode() {
    if (settings().enabled) return Mode::On;
    static const bool log_only = [] {
        const char* v = std::getenv("KHCOM_OVERLAY_ENGINE_LOG");
        return v && *v && std::strcmp(v, "0") != 0;
    }();
    return log_only ? Mode::LogOnly : Mode::Off;
}

bool wants_hook() {
    return mode() != Mode::Off;
}

void on_fn_entry(uint32_t pc) {
    if (!s_trace_init) init_trace();
    if (s_trace_on) trace_entry(pc);
    if (pc != kFnWindowUpdate && pc != kFnDraw && pc != kFnLayout) return;   // the whole cost for every other entry
    switch (pc) {
        case kFnLayout: layout_call(); break;
        case kFnWindowUpdate: window_update(g_cpu.R[0]); break;
        case kFnDraw: draw_call(); break;
        default: break;
    }
}

void note_game_viewport(const SDL_Rect& dst) {
    s_dst = dst;
    ++s_dst_present_seen;
}

void on_present(SDL_Renderer* renderer, const SDL_Rect& game_dst) {
    init_env();
    s_renderer = renderer;
    s_dst = game_dst;
    oam_dump_if_due();
    oam_summary_if_due();
    battle::set_can_draw_for_runtime(&battle_can_draw_cb);
    battle::sync_observer();   // attaches / detaches the PPU observer of the battle message path (Mode::On only)
    if (s_fade_log) fade_log_present();
    if (mode() != Mode::On) {
        if (!s_fade_log) fade_sampler_detach();
        if (s_log && mode() == Mode::LogOnly && s_log_lines <= 400000) {   // calibration aid: which frames exist
            ++s_log_lines;
            const uint64_t v = frame_now();
            std::fprintf(stderr, "[overlay] present V=%llu P=%llu slots:", static_cast<unsigned long long>(v),
                         static_cast<unsigned long long>(ppu_frame()));
            for (int k = 0; k < 5; ++k) {
                const Slot& s = s_ring[(v - k) % kRing];
                if (v >= static_cast<uint64_t>(k) && s.tag == v - k)
                    std::fprintf(stderr, " -%d:pg%u/rev%d/%s", k, s.snap.page_id, s.snap.revealed, s.overlay ? "ov" : "no");
                else
                    std::fprintf(stderr, " -%d:none", k);
            }
            std::fputc('\n', stderr);
        }
        return;
    }

    draw_battle_text();
    const uint64_t now = frame_now();
    const uint64_t want = now >= static_cast<uint64_t>(s_lag) ? now - static_cast<uint64_t>(s_lag) : 0;
    const Slot& slot = s_ring[want % kRing];
    if (slot.tag != want || !slot.overlay) {
        return;
    }
    DrawInfo di = make_info();
    PageSnapshot snap = slot.snap;
    snap.frame = want;
    apply_fade(snap, di);
    if (draw_page(snap, di)) ++s_stats.presents_drawn;
    else ++s_stats.presents_missing;
    if (s_log && s_log_lines <= 400000) {
        ++s_log_lines;
        std::fprintf(stderr, "[overlay] present V=%llu uses tag=%llu rev=%d/%zu pg=%u\n",
                     static_cast<unsigned long long>(now), static_cast<unsigned long long>(want), snap.revealed,
                     snap.glyphs.size(), snap.page_id);
    }
}

bool story_window_recent() {
    return s_last_window_frame != 0 && frame_now() <= s_last_window_frame + 3;
}

void reset() {
    battle::reset();
    for (Slot& s : s_ring) s = Slot{};
    s_pending_valid = false;
    s_lay_window = s_lay_start = 0;
    s_lay_x0 = s_lay_y0 = 0;
}

const Stats& stats() { return s_stats; }
void set_memory_reader_for_tests(const MemReader* reader) { s_reader = reader; }
void set_viewport_for_tests(SDL_Renderer* renderer, const SDL_Rect& dst) {
    s_renderer = renderer;
    s_dst = dst;
}
void set_frame_source_for_tests(uint64_t (*fn)(), uint64_t (*ppu)()) {
    s_frame_fn = fn;
    s_ppu_fn = ppu;
}
int pairing_lag() { init_env(); return s_lag; }
void set_pairing_lag_for_tests(int lag) { s_log_init = true; s_lag = lag; }
bool last_decision_for_tests(Reason* r, PageSnapshot* snap) {
    const Slot& s = s_ring[s_last_frame % kRing];
    if (s.tag != s_last_frame) return false;
    if (r) *r = s.reason;
    if (snap) *snap = s.snap;
    return true;
}

}  // namespace khcom::overlay::engine
