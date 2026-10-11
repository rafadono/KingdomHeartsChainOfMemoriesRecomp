#pragma once
// ENGINE side of the output-resolution overlay for BATTLE dialogue / tutorial boxes (see docs/TEXT_OVERLAY_ENGINE.md,
// section "Battle dialogue").
//
// The battle message boxes are NOT drawn by the story window code the story path hooks (0x08073238 / 0x080658B8): they
// use a second copy of the text library whose glyph pool lives at EWRAM pointer 0x02034A88, and the per-frame task that
// types and submits their glyphs (0x080A3848 and callees) is executed by gbarecomp's interpreter bridge, i.e. it is
// invisible to the function-entry hook. This path therefore does not use function hooks at all:
//
//  * it observes the PPU (GbaPpu::PresentationObserver): at the first scanline of every frame it reads the OAM / VRAM /
//    OBJ palette the PPU is about to render, plus the glyph pool in guest memory, and matches the 16x16 glyph sprites in
//    OAM against the pool entries (position + glyph bitmap in VRAM == the ROM glyph). What the engine really draws this
//    frame is therefore known exactly, whatever the guest is doing;
//  * the matched sprites are hidden for the frame being rendered by setting the OAM "disabled" bit of those entries at the
//    first scanline and restoring it when the PPU latches the frame (before the guest runs its vblank code, which rewrites
//    the whole OAM every frame anyway); the OBJ x provider of the PPU cannot be used because the 3:2 (unextended) PPU path
//    never calls it. The same decision becomes a PageSnapshot drawn by the render side at present time; it is committed
//    when the PPU latches the frame (frame_ready), so it is paired with exactly the frame that is presented.
//
// Strict stand-down: anything that does not match (unknown glyph, sprite without pool entry, typed glyphs that are not a
// prefix of the page, foreign sprites sharing the text palette, glyph bitmap differing from the ROM glyph, render side
// refusing the page) leaves ALL engine glyphs visible and draws nothing.
#include "engine_hooks.h"
#include "overlay_types.h"

#include <cstdint>
#include <vector>

struct SDL_Renderer;
struct SDL_Rect;

namespace khcom::overlay::engine::battle {

inline constexpr uint32_t kBattlePoolPtrAddr = 0x02034A88u;   // glyph pool pointer of the second text library (128 x 24 bytes)
// The entry count byte (0x02034A90, engine::kPoolCountAddr), the 24 byte entry layout, the ROM font (engine::kFontTilesBase,
// 128 bytes per glyph) and the 8.8 fixed point are shared with the story library.

inline constexpr int kBattleWrapPx = 155;     // layout routine 0x0806BB44: wrap at 0x9B00 (8.8) from the origin, like the story one
inline constexpr int kBattleLinePitch = 12;   // 0xC00 per newline
inline constexpr int kBattleMaxLines = 3;     // the layout stops when the pen is more than 0x1800 (24 px) below the first line
inline constexpr uint32_t kBattlePageIdBase = 0x80000000u;   // battle page ids never collide with story page ids

struct OamRaw {
    uint16_t a0 = 0, a1 = 0, a2 = 0;
};

// Reads the 128 bytes of ROM glyph `g` (false when not readable). Used to compare the glyph bitmap in VRAM.
using RomGlyphReader = bool (*)(void* ctx, int glyph, uint8_t out[128]);

// Everything the decision needs for ONE frame, as seen by the PPU (OAM / VRAM / palette) and guest memory (pool).
struct RawBattle {
    uint64_t frame = 0;
    uint8_t pool_count = 0;                 // byte at engine::kPoolCountAddr
    bool pool_valid = false;                // pool pointer in EWRAM / IWRAM
    std::vector<PoolEntryRaw> pool;         // entries 0..pool_count-1
    OamRaw oam[128];
    bool obj_1d = true;                     // DISPCNT bit 6
    const uint8_t* vram = nullptr;          // 96 KB VRAM as the PPU sees it (nullptr: bitmap check skipped, tests only)
    const uint16_t* obj_pal = nullptr;      // 256 OBJ palette colours (BGR555), nullptr = colours unknown
    RomGlyphReader rom_glyph = nullptr;
    void* rom_ctx = nullptr;
};

struct Env {
    bool enabled = true;
    bool viewport_ok = true;
    bool story_window = false;   // the story dialogue window (function-hook path) updated within the last frames: it owns the text
    int frame_off_x = 0;      // x of the 240 px game picture inside the presented image (extra_left in the 16:9 view, else 0)
    bool check_bitmaps = true;
};

enum class Reason : int {
    Overlay = 0,
    Disabled,
    NoViewport,
    StoryWindow,       // the story window path is active (never two paths on one frame)
    NoPool,            // pool pointer invalid / count 0 or > 128
    BadEntry,          // entry not in use, handle without readable glyph
    UnsupportedGlyph,  // icon / non-Latin glyph: the overlay cannot reproduce it
    NoText,            // no glyph sprite of the page is on screen (nothing to hide, nothing to draw)
    NotPrefix,         // typed glyph sprites are not a prefix of the page (stale pool / page change in progress)
    ForeignSprite,     // an unmatched 16x16 sprite shares the text palette bank
    BitmapMismatch,    // VRAM tile of a matched sprite differs from the ROM glyph
    DuplicateSprite,   // two sprites on the same glyph cell
    CannotDraw,        // render side refused the page
    Count_
};
const char* reason_name(Reason r);

struct Decision {
    Reason reason = Reason::Disabled;
    PageSnapshot snap;                      // valid only for Reason::Overlay
    uint64_t hide[2] = {0, 0};              // OAM indices of the engine glyph sprites to hide (bit i of hide[i / 64])
    int matched = 0;                        // glyph sprites matched to pool entries
    uint64_t page_signature = 0;            // hash of the page (words, positions, colour flags): changes when the page changes
};

// Pure decision (unit tested). `can_draw` (optional) is asked last. `out` is always filled with at least the reason.
Reason evaluate(const RawBattle& raw, const Env& env, Decision* out,
                bool (*can_draw)(const PageSnapshot&, void*) = nullptr, void* can_draw_ctx = nullptr);

// ---- runtime ---------------------------------------------------------------------------------------------------------
// Attaches / detaches the PPU observer according to engine::mode() (Mode::On attaches). Called every present (cheap).
void sync_observer();
// The snapshot of the frame the PPU latched last (valid only if the frame carried battle text). `tag` = latch counter.
// `image_w` = width (GBA px) of the presented image the snapshot's coordinates refer to (240 or 240 + margins).
bool latched_snapshot(PageSnapshot* out, uint64_t* tag, int* image_w = nullptr);
// Presentation geometry for the decision (the game picture offset inside the presented image and the image width in GBA px).
void set_view_for_decision(int frame_off_x, int image_w);
int view_image_width();
// The render side's cheap "can you draw this page" check (needs the presentation rectangle, provided by engine_hooks.cpp).
void set_can_draw_for_runtime(bool (*fn)(const PageSnapshot&));
// Drops everything (settings off, savestate, tests).
void reset();

struct Stats {
    uint64_t frames = 0;                    // frames evaluated
    uint64_t overlay_frames = 0;
    uint64_t hidden_sprites = 0;            // OAM entries disabled for a frame (diagnostics)
    uint64_t standdowns[static_cast<int>(Reason::Count_)] = {};
};
const Stats& stats();

// ---- test hooks --------------------------------------------------------------------------------------------------------
void set_memory_reader_for_tests(const MemReader* reader);   // pool reads (nullptr = guest bus)
// Feeds the two PPU observer events: scanline 0 of a frame (decision; `oam` is modified in place like the live OAM) and the latch
// of the frame (frame_ready; `ppu_frame` is the PPU frame counter at that moment).
void feed_scanline0_for_tests(uint64_t ppu_frame, uint16_t dispcnt, const uint8_t* vram, const uint8_t* oam, const uint8_t* pal);
void feed_frame_ready_for_tests(uint64_t ppu_frame);
void set_viewport_for_tests(bool ok);
void set_enabled_for_tests(bool on);   // overrides engine::mode() for the observer logic

}  // namespace khcom::overlay::engine::battle
