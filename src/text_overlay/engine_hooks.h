#pragma once
// ENGINE side of the output-resolution dialogue overlay (see docs/TEXT_OVERLAY_ENGINE.md).
//
//  * captures, once per emulated frame, what the game's dialogue window is showing (page text, glyph
//    pool, typewriter counter) into a khcom::overlay::PageSnapshot;
//  * decides ONCE per frame (stand-down predicate) whether the overlay or the engine owns the text;
//  * when the overlay owns it, makes the engine's own dialogue draw call submit nothing (R0 := 0 at
//    0x080658B8), without touching guest memory;
//  * hands the snapshot that belongs to the PRESENTED frame to OverlayRenderer::draw() at present time.
//
// The hook is installed only while the overlay is enabled (see wants_hook()); with the overlay off
// nothing of this module runs. The pure parts (decoding + predicate) are free of runtime state so
// they are unit tested (tests/test_overlay_engine.cpp).
#include "overlay_types.h"

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Rect;

namespace khcom::overlay::engine {

// ---- engine addresses (USA ROM, see docs/TEXT_OVERLAY_ENGINE.md) --------------------------------
inline constexpr uint32_t kFnLayout = 0x08064EF4u;        // layout(x0, y0, text*, &continuation)
inline constexpr uint32_t kFnWindowUpdate = 0x08073238u;  // dialogue window per-frame update (R0 = window object W)
inline constexpr uint32_t kFnDraw = 0x080658B8u;          // submit glyph pool: draw(count)
inline constexpr uint32_t kDrawCallerLr = 0x0807325Bu;    // LR of the draw call made by the window update
inline constexpr uint32_t kPoolPtrAddr = 0x02034A84u;     // pointer to the glyph pool (128 x 24 bytes)
inline constexpr uint32_t kPoolCountAddr = 0x02034A90u;   // byte: entries laid out for the page
inline constexpr uint32_t kFontTilesBase = 0x090CBFB2u;   // glyph g's ROM source = base + 128 * g
inline constexpr uint32_t kCutsceneTextBegin = 0x08FBD378u;
inline constexpr uint32_t kCutsceneTextEnd = 0x08FFE504u;
inline constexpr uint16_t kPageKindSubtitle = 0x3E;       // page entry kind drawn by the other (movie subtitle) family
inline constexpr int kPoolEntries = 128;
inline constexpr int kPoolStride = 24;
inline constexpr int kMaxPageWords = 400;

// ---- pure decoding -----------------------------------------------------------------------------
// Engine fixed point (entry +0 / +4): pixel = (int32_t)(v << 8) >> 16 (floor); the float keeps the 1/256 fraction.
int fixed_to_px(uint32_t v);
float fixed_to_float(uint32_t v);

// Words the overlay can reproduce: Latin 0x20..0xFF, newline, the pause mark 0xE000 and the colour switches.
bool is_supported_word(uint16_t w);
// Words that produce a pool entry (one glyph each, spaces included): everything but 0, 0x0A and the colour switches.
bool is_drawable_word(uint16_t w);

struct PoolEntryRaw {
    uint32_t x = 0, y = 0;        // 8.8 fixed
    uint32_t handle = 0;          // VRAM-tile allocation object (0 for spaces)
    uint32_t glyph_src = 0;       // handle->+0: ROM glyph source pointer (0 when no handle)
    uint8_t color = 0;            // +20
    uint8_t used = 0;             // +21 (1 = entry in use)
};

// Everything read from guest memory for one frame of the dialogue window.
struct RawPage {
    uint32_t window = 0;
    uint16_t kind = 0;            // page entry kind (0x3E = other text family)
    uint8_t type = 0;             // W+0x28
    uint8_t placement = 0;        // W+0x20 (0..3)
    uint8_t revealed = 0;         // W+0x24 typewriter counter
    uint8_t total = 0;            // W+0x26 entries of the page
    uint8_t state = 0;            // W+0x2B (page-complete / wait-for-key state)
    uint8_t page_index = 0;       // W+0x27
    uint32_t start_ptr = 0;       // captured at the layout call (R2); 0 = layout call not seen
    uint32_t layout_window = 0;   // W seen by the layout call
    int32_t x0 = 0, y0 = 0;       // layout call arguments (R0, R1)
    uint32_t cont_ptr = 0;        // W+0x40: continuation (0 = text ended)
    std::vector<uint16_t> words;  // [start, continuation) or up to the terminator
    bool words_ok = false;        // range was readable and short enough
    uint8_t pool_count = 0;       // byte at kPoolCountAddr
    bool pool_valid = false;      // pool pointer non-null
    std::vector<PoolEntryRaw> pool;   // entries 0..total-1
    uint32_t page_id = 0;
    uint64_t frame = 0;
    uint32_t text_rgba = 0, shadow_rgba = 0;   // OBJ palette bank 0, entries 1/2 (0xRRGGBBAA)
};

struct Env {
    bool enabled = true;          // overlay mode on (hook installed and settings enabled)
    bool battle = false;          // battle arena active: dialogue there is not verified
    bool viewport_ok = true;      // a game viewport / renderer is known
    bool placement_check = true;  // require the known origin placements
};

enum class Reason : int {
    Overlay = 0,                  // the overlay owns this page
    Disabled,
    Battle,
    NoViewport,
    Subtitle,                     // page kind 0x3E
    NoLayout,                     // layout call not captured for this window/page (mid-page start / savestate)
    OutsideCutsceneRange,
    BadRange,                     // continuation before start, unreadable, too long
    UnsupportedWord,
    BadCounters,                  // revealed > total, total != drawable words, pool count mismatch
    NoPool,
    PoolMismatch,                 // pool glyph codes differ from the page words
    BadPlacement,                 // origin is not one of the known placements / differs from the pool
    CannotDraw,                   // render side refused (fonts, fit, ...)
    NoDrawCall,                   // decided overlay but the engine draw call was not the expected one
    Count_
};
const char* reason_name(Reason r);

// Builds the snapshot and evaluates the stand-down predicate (pure). `out` is filled whenever the page could be
// decoded, even for stand-down reasons after the decode (so logs can show it); out->valid is true only for
// Reason::Overlay. can_draw (optional) is asked last.
Reason evaluate(const RawPage& raw, const Env& env, PageSnapshot* out,
                bool (*can_draw)(const PageSnapshot&, void*) = nullptr, void* can_draw_ctx = nullptr);

// ---- runtime ------------------------------------------------------------------------------------
enum class Mode : int { Off = 0, LogOnly = 1, On = 2 };
Mode mode();                       // from overlay::settings().enabled / debug environment (see .cpp)
bool wants_hook();                 // mode() != Off: the function-entry hook must be chained

// Function-entry hook body: cheap early-out on the three watched addresses. Only called while installed.
void on_fn_entry(uint32_t pc);

// Present time: draws the snapshot belonging to the frame about to be shown. `game_dst` is the output rectangle of
// the 240x160 game image. Cheap when no snapshot exists. Never draws in Off/LogOnly.
void on_present(SDL_Renderer* renderer, const SDL_Rect& game_dst);
// The game image was composed this present with this rectangle (called from the render-copy intercept).
void note_game_viewport(const SDL_Rect& dst);

// True when the story dialogue window (0x08073238) was updated within the last few frames: that path owns the text then.
bool story_window_recent();

// Called when the hook is switched off or a savestate/rewind is detected: forgets snapshots and the captured layout.
void reset();

struct Stats {
    uint64_t windows = 0;          // window updates evaluated
    uint64_t hidden = 0;           // draw calls suppressed
    uint64_t standdowns[static_cast<int>(Reason::Count_)] = {};
    uint64_t presents_drawn = 0;
    uint64_t presents_missing = 0; // present in a dialogue frame without a usable snapshot
};
const Stats& stats();

// Test hooks.
struct MemReader {
    uint32_t (*r32)(uint32_t);
    uint16_t (*r16)(uint32_t);
    uint8_t (*r8)(uint32_t);
};
void set_memory_reader_for_tests(const MemReader* reader);   // nullptr = the guest bus
void set_viewport_for_tests(SDL_Renderer* renderer, const SDL_Rect& dst);
void set_frame_source_for_tests(uint64_t (*fn)(), uint64_t (*ppu)() = nullptr);
int pairing_lag();
void set_pairing_lag_for_tests(int lag);
bool last_decision_for_tests(Reason* r, PageSnapshot* snap);

}  // namespace khcom::overlay::engine
