#include "battle_text.h"

#include "../dialogue_backlog.h"      // ScopedInternalBusRead
#include "gbaimp/widescreen.h"         // Widescreen::effective_extra_pixels
#include "armv4t/runtime_arm.h"       // bus_read_*
#include "gba/gba_ppu.h"
#include "runtime_bus_bridge.h"        // active_ppu

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace khcom::overlay::engine::battle {

const char* reason_name(Reason r) {
    static const char* k[] = {"overlay",       "disabled",   "no-viewport", "story-window", "no-pool",       "bad-entry",
                              "unsupported-glyph", "no-text", "not-prefix",  "foreign-sprite", "bitmap-mismatch",
                              "duplicate-sprite",  "cannot-draw"};
    const int i = static_cast<int>(r);
    return (i >= 0 && i < static_cast<int>(Reason::Count_)) ? k[i] : "?";
}

// =================================================================================================================
// Pure decision
// =================================================================================================================
namespace {

uint32_t bgr555_rgba(uint16_t c) {
    auto e = [](uint32_t v) { return (v << 3) | (v >> 2); };
    return (e(c & 31u) << 24) | (e((c >> 5) & 31u) << 16) | (e((c >> 10) & 31u) << 8) | 0xFFu;
}

struct Cell {
    int px = 0, py = 0;     // glyph cell, GBA px
    int glyph = 0x20;
    uint16_t word = 0x20;
    bool space = true;
    bool colour = false;
    int sprite = -1;        // matched OAM index
};

// Candidate glyph sprite: plain 16x16, 4bpp, normal mode, no rotation, no flip, not disabled.
bool is_candidate(const OamRaw& o) {
    if (o.a0 & 0x0300u) return false;                  // rotation/scaling or disabled
    if (((o.a0 >> 10) & 3u) != 0) return false;        // semi-transparent / window OBJ
    if (o.a0 & 0x2000u) return false;                  // 256 colours
    if ((o.a0 >> 14) != 0 || (o.a1 >> 14) != 1) return false;   // square 16x16
    if (o.a1 & 0x3000u) return false;                  // flips
    return true;
}

int sprite_x(const OamRaw& o) { const int x = o.a1 & 0x1FF; return x >= 256 ? x - 512 : x; }
int sprite_y(const OamRaw& o) { const int y = o.a0 & 0xFF; return y >= 160 ? y - 256 : y; }

// The 128 bytes of the sprite's 4 tiles in the order of the ROM glyph (row of 2 tiles, then the next row).
bool sprite_bytes(const uint8_t* vram, uint32_t tile, bool obj_1d, uint8_t out[128]) {
    if (tile >= 1024) return false;
    auto copy = [&](uint32_t t, uint8_t* dst) {
        const uint32_t off = 0x10000u + t * 32u;
        if (off + 64u > 96u * 1024u) return false;
        std::memcpy(dst, vram + off, 64);
        return true;
    };
    if (obj_1d) {
        const uint32_t off = 0x10000u + tile * 32u;
        if (off + 128u > 96u * 1024u) return false;
        std::memcpy(out, vram + off, 128);
        return true;
    }
    return copy(tile, out) && copy(tile + 32u, out + 64);
}

}  // namespace

Reason evaluate(const RawBattle& raw, const Env& env, Decision* out, bool (*can_draw)(const PageSnapshot&, void*),
                void* can_draw_ctx) {
    Decision local;
    Decision& d = out ? *out : local;
    d = Decision{};
    auto fail = [&](Reason r) {
        d.reason = r;
        d.snap.valid = false;
        d.hide[0] = d.hide[1] = 0;
        return r;
    };
    d.snap.frame = raw.frame;
    d.snap.source = 1;
    if (!env.enabled) return fail(Reason::Disabled);
    if (!env.viewport_ok) return fail(Reason::NoViewport);
    if (env.story_window) return fail(Reason::StoryWindow);
    const int n = raw.pool_count;
    if (!raw.pool_valid || n < 1 || n > kPoolEntries || static_cast<int>(raw.pool.size()) != n) return fail(Reason::NoPool);

    // ---- pool -> cells (the engine laid out one entry per drawable word, spaces included) ---------------------------
    std::vector<Cell> cells(static_cast<size_t>(n));
    uint64_t sig = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { sig ^= v; sig *= 1099511628211ull; };
    for (int i = 0; i < n; ++i) {
        const PoolEntryRaw& e = raw.pool[static_cast<size_t>(i)];
        Cell& c = cells[static_cast<size_t>(i)];
        if (e.used != 1) return fail(Reason::BadEntry);
        c.px = fixed_to_px(e.x);
        c.py = fixed_to_px(e.y);
        c.colour = e.color != 0;
        if (e.handle == 0) {
            c.glyph = 0x20;
        } else {
            if (e.glyph_src < kFontTilesBase || (e.glyph_src - kFontTilesBase) % 128u != 0) return fail(Reason::BadEntry);
            c.glyph = static_cast<int>((e.glyph_src - kFontTilesBase) / 128u);
        }
        if (c.glyph == 0x20) {
            c.space = true;
            c.word = 0x20;
        } else if (c.glyph >= 0x21 && c.glyph <= 0xFF) {
            c.space = false;
            c.word = static_cast<uint16_t>(c.glyph);
        } else if (c.glyph == 25) {
            c.space = false;
            c.word = 0xE000;
        } else {
            return fail(Reason::UnsupportedGlyph);
        }
        mix(static_cast<uint64_t>(c.word)); mix(e.x); mix(e.y); mix(c.colour ? 1u : 0u);
    }
    d.page_signature = sig;

    // ---- OAM -> pool matching ------------------------------------------------------------------------------------------
    bool used[128] = {};
    uint16_t banks = 0;   // palette banks of the matched sprites
    int matched = 0, last = -1;
    for (int k = 0; k < n; ++k) {
        Cell& c = cells[static_cast<size_t>(k)];
        if (c.space) continue;
        int found = -1;
        for (int i = 0; i < 128; ++i) {
            if (used[i] || !is_candidate(raw.oam[i])) continue;
            if (sprite_x(raw.oam[i]) != c.px || sprite_y(raw.oam[i]) != c.py) continue;
            if (found >= 0) return fail(Reason::DuplicateSprite);
            found = i;
        }
        if (found < 0) continue;
        if (raw.vram && env.check_bitmaps) {
            uint8_t a[128], b[128];
            if (!raw.rom_glyph || !raw.rom_glyph(raw.rom_ctx, c.glyph, a) ||
                !sprite_bytes(raw.vram, raw.oam[found].a2 & 0x3FFu, raw.obj_1d, b) || std::memcmp(a, b, 128) != 0)
                return fail(Reason::BitmapMismatch);
        }
        used[found] = true;
        c.sprite = found;
        banks = static_cast<uint16_t>(banks | (1u << (raw.oam[found].a2 >> 12)));
        ++matched;
        last = k;
    }
    d.matched = matched;
    if (matched == 0) return fail(Reason::NoText);
    for (int k = 0; k <= last; ++k)
        if (!cells[static_cast<size_t>(k)].space && cells[static_cast<size_t>(k)].sprite < 0) return fail(Reason::NotPrefix);
    for (int i = 0; i < 128; ++i)   // a glyph-shaped sprite that is not part of the page but shares its palette: not ours to judge
        if (!used[i] && is_candidate(raw.oam[i]) && (banks & (1u << (raw.oam[i].a2 >> 12)))) return fail(Reason::ForeignSprite);

    // ---- snapshot ------------------------------------------------------------------------------------------------------
    PageSnapshot& s = d.snap;
    const float off = static_cast<float>(env.frame_off_x);
    int last_nonspace = -1;
    for (int k = 0; k < n; ++k)
        if (!cells[static_cast<size_t>(k)].space) last_nonspace = k;
    s.revealed = last >= last_nonspace ? n : last + 1;
    s.page_complete = s.revealed >= n;
    s.glyphs.reserve(static_cast<size_t>(n));
    int prev_py = cells[0].py;
    for (int k = 0; k < n; ++k) {
        const Cell& c = cells[static_cast<size_t>(k)];
        const PoolEntryRaw& e = raw.pool[static_cast<size_t>(k)];
        const int dl = (c.py - prev_py + kBattleLinePitch / 2) / kBattleLinePitch;   // lines advanced since the previous cell
        for (int q = 0; q < dl; ++q) s.text.push_back(0x000A);
        prev_py = c.py;
        PageGlyph g;
        g.word = c.word;
        g.gx = fixed_to_float(e.x) + off;
        g.gy = fixed_to_float(e.y);
        g.color = c.colour ? 1 : 0;
        s.glyphs.push_back(g);
        s.text.push_back(c.word);
    }
    s.origin_x = static_cast<float>(cells[0].px) + off;
    s.origin_y = static_cast<float>(cells[0].py);
    s.wrap_px = kBattleWrapPx;
    s.line_pitch = kBattleLinePitch;
    s.max_lines = kBattleMaxLines;
    if (raw.obj_pal) {
        int plain = -1, coloured = -1;
        for (int k = 0; k <= last; ++k) {
            const Cell& c = cells[static_cast<size_t>(k)];
            if (c.sprite < 0) continue;
            if (!c.colour && plain < 0) plain = c.sprite;
            if (c.colour && coloured < 0) coloured = c.sprite;
        }
        auto col = [&](int sprite, int entry) -> uint32_t {
            return bgr555_rgba(raw.obj_pal[(raw.oam[sprite].a2 >> 12) * 16 + entry]);
        };
        if (plain >= 0) { s.engine_text_rgba = col(plain, 1); s.engine_shadow_rgba = col(plain, 2); }
        if (coloured >= 0) { s.engine_highlight_rgba = col(coloured, 1); s.engine_highlight_shadow_rgba = col(coloured, 2); }
    }
    for (const Cell& c : cells)
        if (c.sprite >= 0) d.hide[c.sprite / 64] |= 1ull << (c.sprite % 64);
    s.valid = true;
    if (can_draw && !can_draw(s, can_draw_ctx)) return fail(Reason::CannotDraw);
    d.reason = Reason::Overlay;
    return Reason::Overlay;
}

// =================================================================================================================
// Runtime: PPU observer
// =================================================================================================================
namespace {

const MemReader* s_reader = nullptr;
uint32_t rd32(uint32_t a) { return s_reader ? s_reader->r32(a) : bus_read_u32(a); }
uint8_t rd8(uint32_t a) { return s_reader ? s_reader->r8(a) : bus_read_u8(a); }

struct RomGlyphs {
    uint8_t bytes[256][128];
    bool have[256] = {};
    bool tried[256] = {};
} s_rom;

bool rom_glyph_cb(void*, int g, uint8_t out[128]) {
    if (g < 0 || g > 255) return false;
    if (!s_rom.tried[g]) {
        s_rom.tried[g] = true;
        const uint32_t base = kFontTilesBase + 128u * static_cast<uint32_t>(g);
        for (uint32_t i = 0; i < 128; i += 4) {
            const uint32_t v = rd32(base + i);
            std::memcpy(&s_rom.bytes[g][i], &v, 4);
        }
        s_rom.have[g] = true;
    }
    if (!s_rom.have[g]) return false;
    std::memcpy(out, s_rom.bytes[g], 128);
    return true;
}

struct State;
State& st();

bool g_test_enabled = false, g_test_viewport = true, g_test_override = false;
bool g_log = false, g_log_init = false;
int g_off_x = 0, g_image_w = 240;
bool g_have_view = false;

bool enabled_now() { return g_test_override ? g_test_enabled : mode() == Mode::On; }

class Observer final : public gba::GbaPpu::PresentationObserver {
public:
    void scanline(const gba::GbaPpu& ppu, uint32_t y, uint16_t dispcnt, const uint8_t*, const uint8_t* vram, const uint8_t* oam,
                  const uint8_t* pal) override;
    void frame_ready() override;
    void restored() noexcept override;
};

struct State {
    Observer obs;
    bool attached = false, blocked = false;
    uint64_t hide[2] = {0, 0};
    // OAM entries disabled for the frame being rendered: (index, original attr0) and the buffer they live in.
    uint8_t* oam_mut = nullptr;
    int hidden_n = 0;
    uint16_t hidden_a0[128];
    uint8_t hidden_idx[128];
    bool pending_have = false;
    uint64_t pending_tag = ~0ull;
    PageSnapshot pending;
    PageSnapshot latched;
    int latched_w = 240;
    int pending_w = 240;
    bool latched_valid = false;
    uint64_t latched_seq = 0;
    uint64_t sig_last = 0;
    uint32_t logged_page = 0;
    uint32_t page_counter = 0;
    uint64_t frame_in_progress = ~0ull;
    Reason last_reason = Reason::Count_;
    Stats stats;
};

State& st() {
    static State s;
    return s;
}

bool can_draw_cb(const PageSnapshot& snap, void*);

uint16_t oam_a0(const uint8_t* oam, int i) { return static_cast<uint16_t>(oam[i * 8] | (oam[i * 8 + 1] << 8)); }
void oam_set_a0(uint8_t* oam, int i, uint16_t v) { oam[i * 8] = static_cast<uint8_t>(v & 0xFF); oam[i * 8 + 1] = static_cast<uint8_t>(v >> 8); }

// Puts back the attr0 of the entries disabled for this frame. An entry the guest rewrote meanwhile is left alone.
void restore_hidden() {
    State& s = st();
    if (s.oam_mut) {
        for (int k = 0; k < s.hidden_n; ++k) {
            const int i = s.hidden_idx[k];
            if (oam_a0(s.oam_mut, i) == static_cast<uint16_t>(s.hidden_a0[k] | 0x0200u)) oam_set_a0(s.oam_mut, i, s.hidden_a0[k]);
        }
    }
    s.hidden_n = 0;
    s.oam_mut = nullptr;
}

void apply_hidden(const uint8_t* oam) {
    State& s = st();
    restore_hidden();
    s.oam_mut = const_cast<uint8_t*>(oam);   // the PPU only hands out a const view of the live OAM buffer
    for (int i = 0; i < 128; ++i) {
        if (!((s.hide[i >> 6] >> (i & 63)) & 1ull)) continue;
        const uint16_t a0 = oam_a0(oam, i);
        s.hidden_idx[s.hidden_n] = static_cast<uint8_t>(i);
        s.hidden_a0[s.hidden_n] = a0;
        ++s.hidden_n;
        oam_set_a0(s.oam_mut, i, static_cast<uint16_t>(a0 | 0x0200u));   // plain (non rotation) sprite: bit 9 = disabled
        ++s.stats.hidden_sprites;
    }
}

void frame_begin(uint64_t ppu_frame, uint16_t dispcnt, const uint8_t* vram, const uint8_t* oam, const uint8_t* pal) {
    State& s = st();
    restore_hidden();   // a frame that was never latched (rasterization off): do not leave entries disabled
    s.hide[0] = s.hide[1] = 0;
    s.pending_have = false;
    s.frame_in_progress = ppu_frame;
    if (!enabled_now() || !oam) return;
    if (!g_log_init) {
        g_log_init = true;
        const char* v = std::getenv("KHCOM_OVERLAY_LOG");
        g_log = v && *v && std::strcmp(v, "0") != 0;
    }
    RawBattle raw;
    raw.frame = ppu_frame;
    raw.obj_1d = (dispcnt & 0x0040u) != 0;
    raw.vram = vram;
    raw.rom_glyph = &rom_glyph_cb;
    bool any = false;
    for (int i = 0; i < 128; ++i) {
        raw.oam[i].a0 = static_cast<uint16_t>(oam[i * 8] | (oam[i * 8 + 1] << 8));
        raw.oam[i].a1 = static_cast<uint16_t>(oam[i * 8 + 2] | (oam[i * 8 + 3] << 8));
        raw.oam[i].a2 = static_cast<uint16_t>(oam[i * 8 + 4] | (oam[i * 8 + 5] << 8));
        if (is_candidate(raw.oam[i])) any = true;
    }
    if (!any) {   // no glyph-shaped sprite at all: nothing of the page can be on screen
        s.last_reason = Reason::NoText;
        return;
    }
    uint16_t pal16[256];
    if (pal) {
        for (int i = 0; i < 256; ++i) pal16[i] = static_cast<uint16_t>(pal[0x200 + 2 * i] | (pal[0x201 + 2 * i] << 8));
        raw.obj_pal = pal16;
    }
    {
        ScopedInternalBusRead guard;
        const uint32_t pool = rd32(kBattlePoolPtrAddr);
        raw.pool_count = rd8(kPoolCountAddr);
        raw.pool_valid = (pool >> 24) == 0x02 || (pool >> 24) == 0x03;
        if (raw.pool_valid && raw.pool_count >= 1 && raw.pool_count <= kPoolEntries) {
            raw.pool.reserve(raw.pool_count);
            for (int i = 0; i < raw.pool_count; ++i) {
                const uint32_t e = pool + static_cast<uint32_t>(i) * kPoolStride;
                PoolEntryRaw p;
                p.x = rd32(e + 0);
                p.y = rd32(e + 4);
                p.handle = rd32(e + 8);
                p.color = rd8(e + 20);
                p.used = rd8(e + 21);
                p.glyph_src = (p.handle >> 24) == 0x02 || (p.handle >> 24) == 0x03 ? rd32(p.handle) : 0;
                raw.pool.push_back(p);
            }
        }
        if (!g_test_override) {   // geometry of the image the PPU is producing now (battle 16:9: 22 px margins around the 240 px picture)
            int l = 0, r = 0;
            gbaimp::Widescreen::instance().effective_extra_pixels(&l, &r);
            g_off_x = l;
            g_image_w = 240 + l + r;
        }
        Env env;
        env.enabled = true;
        env.viewport_ok = g_test_override ? g_test_viewport : true;   // the render side's can_draw check refuses when no viewport is known
        env.frame_off_x = g_off_x;
        env.story_window = !g_test_override && story_window_recent();
        Decision d;
        const Reason r = evaluate(raw, env, &d, g_test_override ? nullptr : &can_draw_cb, nullptr);
        ++s.stats.frames;
        ++s.stats.standdowns[static_cast<int>(r)];
        if (r == Reason::Overlay) {
            ++s.stats.overlay_frames;
            if (d.page_signature != s.sig_last) {
                s.sig_last = d.page_signature;
                ++s.page_counter;
            }
            d.snap.page_id = kBattlePageIdBase + s.page_counter;
            if (g_log && d.snap.page_id != s.logged_page) {
                s.logged_page = d.snap.page_id;
                std::fprintf(stderr, "[battle] page %u origin=(%.1f,%.1f) glyphs=%zu text=\"", d.snap.page_id - kBattlePageIdBase,
                             d.snap.origin_x, d.snap.origin_y, d.snap.glyphs.size());
                for (uint16_t w : d.snap.text) {
                    if (w == 0x0A) std::fputc('|', stderr);
                    else if (w >= 0x20 && w < 0x7F) std::fputc(static_cast<int>(w), stderr);
                    else std::fprintf(stderr, "<%X>", w);
                }
                std::fprintf(stderr, "\" ys:");
                int py = -999;
                for (const PageGlyph& g : d.snap.glyphs) {
                    if (static_cast<int>(g.gy) != py) { py = static_cast<int>(g.gy); std::fprintf(stderr, " %d@%.0f", py, g.gx); }
                }
                std::fputc(10, stderr);
            }
            s.hide[0] = d.hide[0];
            s.hide[1] = d.hide[1];
            apply_hidden(oam);
            s.pending = std::move(d.snap);
            s.pending_w = g_image_w;
            s.pending_have = true;
            s.pending_tag = ppu_frame;
        }
        if (g_log && (r != s.last_reason || r == Reason::Overlay)) {
            static int lines = 0;
            if (lines < 20000) {
                ++lines;
                std::fprintf(stderr, "[battle] P=%llu reason=%s matched=%d/%d rev=%d\n", static_cast<unsigned long long>(ppu_frame),
                             reason_name(r), d.matched, raw.pool_count, r == Reason::Overlay ? s.pending.revealed : -1);
            }
        }
        s.last_reason = r;
    }
}

void Observer::scanline(const gba::GbaPpu& ppu, uint32_t y, uint16_t dispcnt, const uint8_t*, const uint8_t* vram, const uint8_t* oam,
                        const uint8_t* pal) {
    if (y != 0) return;
    frame_begin(ppu.frame_count(), dispcnt, vram, oam, pal);
}

void Observer::frame_ready() {
    State& s = st();
    restore_hidden();   // the frame is fully rendered: the guest's vblank code sees its own OAM again
    const gba::GbaPpu* ppu = gbarecomp::active_ppu();
    const bool paired = s.pending_have && ppu && s.pending_tag == ppu->frame_count();
    if (paired) {
        s.latched = s.pending;
        s.latched_w = s.pending_w;
        s.latched_valid = true;
    } else {
        s.latched_valid = false;
    }
    s.pending_have = false;
    ++s.latched_seq;
}

void Observer::restored() noexcept {
    State& s = st();
    s.hidden_n = 0;      // the OAM was replaced by the savestate: nothing of ours is in it
    s.oam_mut = nullptr;
    s.hide[0] = s.hide[1] = 0;
    s.pending_have = false;
    s.latched_valid = false;
}

}  // namespace

// can_draw needs the presentation geometry: it is provided by engine_hooks.cpp through this setter.
namespace {
bool (*g_can_draw)(const PageSnapshot&) = nullptr;
bool can_draw_cb(const PageSnapshot& snap, void*) { return g_can_draw ? g_can_draw(snap) : true; }
}  // namespace

void set_can_draw_for_runtime(bool (*fn)(const PageSnapshot&)) { g_can_draw = fn; }

void set_view_for_decision(int frame_off_x, int image_w) {
    g_off_x = frame_off_x;
    g_image_w = image_w;
    g_have_view = true;
}

void sync_observer() {
    State& s = st();
    gba::GbaPpu* ppu = gbarecomp::active_ppu();
    if (!ppu) return;
    if (enabled_now()) {
        gba::GbaPpu::PresentationObserver* o = ppu->presentation_observer();
        if (o == &s.obs) return;
        if (o != nullptr) { s.blocked = true; return; }   // somebody else (netplay, fade log) owns the observer: stand down
        ppu->set_presentation_observer(&s.obs);
        s.attached = true;
        s.blocked = false;
    } else if (s.attached) {
        restore_hidden();
        if (ppu->presentation_observer() == &s.obs) ppu->set_presentation_observer(nullptr);
        s.attached = false;
        s.hide[0] = s.hide[1] = 0;
        s.pending_have = false;
        s.latched_valid = false;
    }
}

bool latched_snapshot(PageSnapshot* out, uint64_t* tag, int* image_w) {
    const State& s = st();
    if (!s.latched_valid) return false;
    if (out) *out = s.latched;
    if (tag) *tag = s.latched_seq;
    if (image_w) *image_w = s.latched_w;
    return true;
}

int view_image_width() { return g_image_w; }

void reset() {
    State& s = st();
    restore_hidden();
    s.hide[0] = s.hide[1] = 0;
    s.pending_have = false;
    s.latched_valid = false;
    s.sig_last = 0;
    s.page_counter = 0;
    s.last_reason = Reason::Count_;
    s.stats = Stats{};
    std::memset(s_rom.tried, 0, sizeof(s_rom.tried));
    std::memset(s_rom.have, 0, sizeof(s_rom.have));
}

const Stats& stats() { return st().stats; }
void set_memory_reader_for_tests(const MemReader* reader) { s_reader = reader; }
void set_viewport_for_tests(bool ok) { g_test_viewport = ok; }
void set_enabled_for_tests(bool on) { g_test_override = true; g_test_enabled = on; }

void feed_scanline0_for_tests(uint64_t ppu_frame, uint16_t dispcnt, const uint8_t* vram, const uint8_t* oam, const uint8_t* pal) {
    frame_begin(ppu_frame, dispcnt, vram, oam, pal);
}

void feed_frame_ready_for_tests(uint64_t ppu_frame) {
    State& s = st();
    restore_hidden();
    if (s.pending_have && s.pending_tag == ppu_frame) {
        s.latched = s.pending;
        s.latched_w = s.pending_w;
        s.latched_valid = true;
    } else {
        s.latched_valid = false;
    }
    s.pending_have = false;
    ++s.latched_seq;
}

}  // namespace khcom::overlay::engine::battle
