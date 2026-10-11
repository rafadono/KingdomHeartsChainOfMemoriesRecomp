#include "gbaimp/hooks.h"
#include "gbaimp/hud_anchoring.h"
#include "gbaimp_integration.h"
#include "dialogue_backlog.h"
#include "widescreen_adapter.h"
#include "music_hook.h"
#include "gbaimp/hd_audio_player.h"
#include "armv4t/runtime_arm.h"
#include "capture_harness.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include <SDL.h>

using namespace khcom;

int run_overlay_render_tests(const char* png_dir, bool verbose);
int run_overlay_engine_tests(bool verbose);

// Widescreen globals defined by gbarecomp (C linkage; must be declared at file scope so
// MSVC does not mangle them as C++ symbols).
extern "C" {
extern unsigned g_ws_active;
extern unsigned g_ws_extra;
extern unsigned g_ws_extra_left;
extern unsigned g_ws_extra_right;
extern unsigned g_ws_view_width;
extern int (*g_ws_tilemap_provider)(int, int, int, uint16_t*);
extern int (*g_ws_bg_x_provider)(int, int, int, int*);
extern unsigned g_ws_bg_x_provider_layers;
extern int (*g_ws_obj_attr_x_provider)(int, uint16_t, uint16_t, uint16_t, int*);
extern int g_ws_authored_margin_layers;
extern int g_ws_obj_native_clip;
extern int g_ws_pillarbox;
extern int g_ws_pillarbox_left;
extern int g_ws_pillarbox_right;
extern void (*g_runtime_fn_entry_hook)(uint32_t entry_pc);
}

// Reference copy of the pre-migration KH HUD anchoring logic (before the zones moved to
// gbaimp::HudZone). The zones registered by register_gbaimp_integration() must reproduce it.
static void legacy_adjust_sprite_coordinate(int& x, int y, int offset) {
    if (x <= 90 && y <= 45) x -= offset;
    else if (x >= 130 && y >= 95) x += offset;
}

static void legacy_process_oam_entry(uint16_t& attr1, uint16_t attr0, int offset) {
    int y = attr0 & 0x00FF;
    int x = attr1 & 0x01FF;
    if (x >= 256) x -= 512;
    if (y >= 160) y -= 256;
    if (x >= 0 && x <= 85 && y >= 0 && y <= 40) {
        x = std::max(-40, x - offset);
        attr1 = (attr1 & ~0x01FF) | (static_cast<uint16_t>(x) & 0x01FF);
    } else if (x >= 135 && x <= 240 && y >= 100 && y <= 160) {
        x = std::min(300, x + offset);
        attr1 = (attr1 & ~0x01FF) | (static_cast<uint16_t>(x) & 0x01FF);
    }
}

// HD music trigger: decisions of the m4a song hook (src/music_hook.cpp) against the library.
static void test_music_hook() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "khcom_music_hook_test";
    fs::create_directories(dir);
    const fs::path wav = dir / "t.wav";
    {
        std::ofstream f(wav, std::ios::binary);
        auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        const uint32_t frames = 2000, bytes = frames * 4;
        f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
        f.write("fmt ", 4); u32(16); u16(1); u16(2); u32(65536); u32(65536 * 4); u16(4); u16(16);
        f.write("data", 4); u32(bytes);
        for (uint32_t i = 0; i < frames; ++i) { u16(3000); u16(3000); }
    }
    const fs::path ini = dir / "tracks.ini";
    {
        std::ofstream f(ini);
        f << "[tracks]\n7 = " << wav.generic_string() << "\n8 = " << (dir / "missing.wav").generic_string() << "\n";
    }
    auto& hd = gbaimp::HdAudioPlayer::instance();
    hd.settings().target_sample_rate = 65536;
    hd.set_enabled(true);
    hd.load_track_mapping(ini.generic_string());
    music_hook_reset_for_tests();

    music_on_song_start(7, 0);
    assert(hd.current_song_id() == 7);                 // mapped song starts the HD track
    music_on_song_start(100, 5);                       // unmapped, other player (e.g. a sound effect)
    assert(hd.current_song_id() == 7);
    music_on_song_start(101, 0);                       // unmapped, same player: the music was replaced
    assert(hd.current_song_id() == 0);
    music_on_song_start(8, 1);                         // mapped but the file is missing: silent, no crash
    assert(hd.current_song_id() == 0);
    music_on_song_start(7, 2);
    assert(hd.current_song_id() == 7);
    music_on_song_stop(9);                             // some other song stopped: HD stays
    assert(hd.current_song_id() == 7);
    music_on_song_stop(7);
    assert(hd.current_song_id() == 0);
    music_on_song_start(7, 2);
    music_on_all_stop();
    assert(hd.current_song_id() == 0);

    // Through the function-entry dispatch (R0 is a u16 argument; other pcs are ignored).
    music_set_song_player_reader([](uint32_t) -> uint32_t { return 3; });
    g_cpu.R[0] = 0x00010007u;
    music_on_fn_entry(0x08000240u);
    assert(hd.current_song_id() == 0);
    music_on_fn_entry(kM4aSongNumStartOrChange);
    assert(hd.current_song_id() == 7);
    music_on_fn_entry(kM4aSongNumStop);
    assert(hd.current_song_id() == 0);
    g_cpu.R[0] = 0;
    music_set_song_player_reader(nullptr);

    // Disabled HD audio never triggers.
    hd.set_enabled(false);
    music_on_song_start(7, 0);
    assert(hd.current_song_id() == 0);
    hd.set_enabled(true);

    const MusicHookStats st = music_hook_stats();
    assert(st.starts >= 5 && st.mapped_triggers >= 4 && st.hd_stops >= 3);
    hd.stop_music();
    hd.set_enabled(false);
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << "[PASS] C++ music hook (m4a song trigger) unit test passed" << std::endl;
}

static void test_gbaimp_integration() {
    test_music_hook();
    register_gbaimp_integration();
    register_gbaimp_integration(); // idempotent: still exactly two zones
    auto& hud = gbaimp::HudAnchoring::instance();
    assert(hud.zones().size() == 2);
    assert(gbaimp::hooks().save_settings);
    assert(gbaimp::hooks().is_input_blocked);
    assert(gbaimp::hooks().frame_counter);

    hud.set_mode(gbaimp::HudAnchorMode::WidescreenAnchored);
    const int offsets[] = { 0, 1, 32, 96 };
    for (int offset : offsets) {
        hud.set_horizontal_offset(offset);
        for (int y = -20; y <= 180; ++y) {
            for (int x = -60; x <= 300; ++x) {
                int nx = x, ny = y, ox = x;
                hud.adjust_sprite_coordinate(nx, ny, 16, 16, true);
                legacy_adjust_sprite_coordinate(ox, y, offset);
                assert(nx == ox && ny == y);
            }
        }
        for (int y = 0; y < 256; ++y) {
            for (int x = 0; x < 512; ++x) {
                uint16_t a0 = static_cast<uint16_t>(y), a1 = static_cast<uint16_t>(x | 0x4000);
                uint16_t l1 = a1;
                hud.process_oam_entry(a0, a1);
                legacy_process_oam_entry(l1, a0, offset);
                assert(a1 == l1);
            }
        }
    }
    // Explicit clamp cases
    hud.set_horizontal_offset(96);
    uint16_t a0 = 10, a1 = 20;
    hud.process_oam_entry(a0, a1);
    assert((a1 & 0x1FF) == static_cast<unsigned>((-40) & 0x1FF));
    a0 = 120; a1 = 240;
    hud.process_oam_entry(a0, a1);
    assert((a1 & 0x1FF) == 300);

    hud.set_mode(gbaimp::HudAnchorMode::OriginalCentered);
    hud.set_horizontal_offset(32);
    std::cout << "[PASS] C++ gba-improvements integration (hooks + HUD zones) unit test passed" << std::endl;
}

static void test_hud_anchoring() {
    register_gbaimp_integration();
    auto& hud = gbaimp::HudAnchoring::instance();

    hud.set_mode(gbaimp::HudAnchorMode::WidescreenAnchored);
    hud.set_horizontal_offset(32);
    assert(hud.settings().horizontal_offset == 32);

    // Top-left Health Bar (x=24, y=10)
    int hp_x = 24, hp_y = 10;
    hud.adjust_sprite_coordinate(hp_x, hp_y, 16, 16, true);
    assert(hp_x == 24 - 32);

    // Bottom-right Card Deck (x=200, y=120)
    int deck_x = 200, deck_y = 120;
    hud.adjust_sprite_coordinate(deck_x, deck_y, 16, 16, true);
    assert(deck_x == 200 + 32);

    // Center sprite (x=100, y=50) -> must not shift
    int char_x = 100, char_y = 50;
    hud.adjust_sprite_coordinate(char_x, char_y, 16, 16, false);
    assert(char_x == 100 && char_y == 50);

    // OAM processing
    uint16_t attr0 = 10;
    uint16_t attr1 = 24;
    hud.process_oam_entry(attr0, attr1);
    int shifted_oam_x = static_cast<int16_t>(attr1 & 0x01FF);
    if (shifted_oam_x >= 256) shifted_oam_x -= 512;
    assert(shifted_oam_x == -8);

    // Real-time khcom_obj_attr_x_provider verification:
    // Ensure dialogue glyphs, combat actors, and cards are never displaced into widescreen margins,
    // while the authentic HP gauge is properly anchored.
    g_ws_active = 1;
    g_ws_extra_left = 40;
    g_ws_extra_right = 40;
    khcom_set_battle_active(true);

    // 1. Dialogue text glyph at y=20, x=40 (16x16 square, priority 0) -> MUST NOT DISPLACE
    uint16_t dlg_attr0 = 20;               // shape=00 (square), y=20
    uint16_t dlg_attr1 = 40 | 0x4000;      // size=01 (16x16), x=40
    uint16_t dlg_attr2 = 0x0000;           // priority=0
    int out_x = 0;
    int res = khcom_obj_attr_x_provider(0, dlg_attr0, dlg_attr1, dlg_attr2, &out_x);
    assert(res == 0);

    // 2. Dialogue text glyph on line 2 at y=32, x=80 -> MUST NOT DISPLACE
    uint16_t dlg2_attr0 = 32;
    uint16_t dlg2_attr1 = 80 | 0x4000;
    uint16_t dlg2_attr2 = 0x0000;
    res = khcom_obj_attr_x_provider(1, dlg2_attr0, dlg2_attr1, dlg2_attr2, &out_x);
    assert(res == 0);

    // 3. Dialogue text glyph on line 3 at y=44, x=24 -> MUST NOT DISPLACE
    uint16_t dlg3_attr0 = 44;
    uint16_t dlg3_attr1 = 24 | 0x4000;
    uint16_t dlg3_attr2 = 0x0000;
    res = khcom_obj_attr_x_provider(2, dlg3_attr0, dlg3_attr1, dlg3_attr2, &out_x);
    assert(res == 0);

    // 4. Authentic player HP gauge sprite at y=2, x=20 (horizontal 32x8, priority 1) -> SHIFT LEFT
    uint16_t hp_attr0 = 2 | 0x4000;        // shape=01 (horizontal), y=2
    uint16_t hp_attr1 = 20 | 0x4000;       // size=01 (32x8), x=20
    uint16_t hp_attr2 = 0x0400;            // priority=1
    res = khcom_obj_attr_x_provider(3, hp_attr0, hp_attr1, hp_attr2, &out_x);
    assert(res == 1 && out_x == 20 - 40);

    // 4b. Authentic Sora face portrait sprite (32x32 square, priority 1, x=1, y=-7 = 249 unsigned) -> SHIFT LEFT
    uint16_t sora_attr0 = static_cast<uint16_t>((256 - 7) & 0xFF); // shape=00 (square), y=-7
    uint16_t sora_attr1 = 1 | 0x8000;                              // size=10 (32x32), x=1
    uint16_t sora_attr2 = 0x0400;                                  // priority=1
    res = khcom_obj_attr_x_provider(7, sora_attr0, sora_attr1, sora_attr2, &out_x);
    assert(res == 1 && out_x == 1 - 40);

    // 4c. Authentic Sora face side piece (8x16 tall, priority 1, x=-6 = 506 unsigned, y=1) -> SHIFT LEFT
    uint16_t sora_side_attr0 = 1 | 0x8000;                         // shape=10 (tall), y=1
    uint16_t sora_side_attr1 = static_cast<uint16_t>((512 - 6) & 0x1FF) | 0x0000; // size=00 (8x16), x=-6
    uint16_t sora_side_attr2 = 0x0400;                             // priority=1
    res = khcom_obj_attr_x_provider(8, sora_side_attr0, sora_side_attr1, sora_side_attr2, &out_x);
    assert(res == 1 && out_x == -6 - 40);

    // 4d. Tutorial / Dialogue window frame piece (64x64 square, priority 0, x=15, y=10) -> MUST NOT DISPLACE
    uint16_t win_attr0 = 10;                                       // shape=00, y=10
    uint16_t win_attr1 = 15 | 0xC000;                              // size=11 (64x64), x=15
    uint16_t win_attr2 = 0x0000;                                   // priority=0
    res = khcom_obj_attr_x_provider(9, win_attr0, win_attr1, win_attr2, &out_x);
    assert(res == 0);

    // 5. Authentic enemy HP gauge sprite at y=2, x=200 (horizontal 32x8, priority 1) -> SHIFT RIGHT
    uint16_t ehp_attr0 = 2 | 0x4000;
    uint16_t ehp_attr1 = 200 | 0x4000;
    uint16_t ehp_attr2 = 0x0400;
    res = khcom_obj_attr_x_provider(4, ehp_attr0, ehp_attr1, ehp_attr2, &out_x);
    assert(res == 1 && out_x == 200 + 40);

    // 6. Combat actor / boss in right arena area at y=100, x=180 (priority 0) -> MUST NOT DISPLACE
    uint16_t boss_attr0 = 100;
    uint16_t boss_attr1 = 180 | 0x8000;    // 32x32 square
    uint16_t boss_attr2 = 0x0000;
    res = khcom_obj_attr_x_provider(5, boss_attr0, boss_attr1, boss_attr2, &out_x);
    assert(res == 0);

    // 7. Negative offscreen coordinate unwrapping (raw_x=500 -> -12) -> UNWRAP
    uint16_t wrap_attr0 = 80;
    uint16_t wrap_attr1 = 500;
    uint16_t wrap_attr2 = 0x0000;
    res = khcom_obj_attr_x_provider(6, wrap_attr0, wrap_attr1, wrap_attr2, &out_x);
    assert(res == 1 && out_x == -12);

    std::cout << "[PASS] C++ HudAnchoring + widescreen OAM provider unit test passed" << std::endl;
}

static void test_dialogue_components() {
    auto& backlog = DialogueBacklog::instance();

    // Backlog tests
    backlog.set_open(false);
    assert(!backlog.is_open());

    backlog.set_open(true);
    assert(backlog.is_open());

    backlog.toggle_open();
    assert(!backlog.is_open());

    backlog.clear();
    assert(backlog.entries().empty());

    // Mock bus reader for Donald and cutscene dialogues
    auto prev_override = g_runtime_bus_read_override;
    auto mock_bus = [](uint32_t, uint32_t addr, uint32_t, uint32_t, uint32_t* overridden) -> int {
        static const char donald_str[] = "Looks like nobody's home.";
        if (addr >= 0x08FBD378 && addr < 0x08FBD378 + sizeof(donald_str) * 2) {
            size_t idx = (addr - 0x08FBD378) / 2;
            if (overridden) {
                *overridden = (idx < sizeof(donald_str) - 1) ? static_cast<uint8_t>(donald_str[idx]) : 0;
            }
            return 1;
        }
        return 0;
    };
    g_runtime_bus_read_override = mock_bus;

    // Out of range read: must be ignored
    backlog.on_bus_read_u16(0x08000000);
    assert(backlog.entries().empty());

    // Script table read (0x08FBE000): must be ignored, outside dialogue string bounds
    backlog.on_bus_read_u16(0x08FBE000);
    assert(backlog.entries().empty());

    // Character 0 read at start of dialogue 0x08FBD378 ("Looks like nobody's home.")
    backlog.on_bus_read_u16(0x08FBD378);
    // Not confirmed yet until sequential typewriter character 1 is read
    assert(backlog.entries().empty());

    // Character 1 read at 0x08FBD37A: confirms active rendering on screen!
    backlog.on_bus_read_u16(0x08FBD37A);
    assert(!backlog.entries().empty());
    assert(backlog.entries().back().speaker == "Donald");
    assert(backlog.entries().back().text.find("nobody's home") != std::string::npos);

    // Duplicate character read of the same active line must not create duplicate entries
    size_t count_before = backlog.entries().size();
    backlog.on_bus_read_u16(0x08FBD37A);
    assert(backlog.entries().size() == count_before);

    // Unrendered phantom dialogue (e.g. Marluxia 0x08FBDF74 char 0 without char 1) must never push
    backlog.on_bus_read_u16(0x08FBDF74);
    assert(backlog.entries().size() == count_before);

    // Push duplicate testing
    backlog.push_entry("Donald", "We have to if we're gonna find the king...");
    size_t count_after_donald = backlog.entries().size();
    assert(count_after_donald == count_before + 1);

    // Immediate duplicate rejection
    backlog.push_entry("Donald", "We have to if we're gonna find the king...");
    assert(backlog.entries().size() == count_after_donald);

    // Ping-pong duplicate rejection (within 3 entries)
    backlog.push_entry("Goofy", "You sure we should just barge in like this?");
    assert(backlog.entries().size() == count_after_donald + 1);
    backlog.push_entry("Donald", "We have to if we're gonna find the king...");
    assert(backlog.entries().size() == count_after_donald + 1);

    backlog.clear();
    backlog.push_entry("Naminé", "I'm sorry, Sora... I altered your memories.");
    assert(!backlog.entries().empty());
    assert(backlog.entries().back().speaker == "Naminé");
    assert(backlog.entries().back().text.find("altered your memories") != std::string::npos);

    g_runtime_bus_read_override = prev_override;

    std::cout << "[PASS] C++ DialogueBacklog unit test passed" << std::endl;
}

static void test_widescreen_view_modes() {
    khcom_install_widescreen_adapter(22, 22);

    // Initial state: not in battle
    khcom_set_battle_active(false);
    assert(!khcom_is_battle_active());

    // 1. Test 16:9 view mode outside of battle:
    // Should crop source to authentic central 240x160 (from x=22) and fit to 3:2 layout
    SDL_Rect src{}, dst{};
    int base_w = 0, base_h = 0;
    khcom_compute_effective_viewport(284, 160, 1920, 1080, &src, &dst, &base_w, &base_h);
    assert(src.x == 22 && src.y == 0 && src.w == 240 && src.h == 160);
    assert(base_w == 240 && base_h == 160);
    // In 1920x1080, 3:2 aspect fit fills height (1080) and width is 1080 * 3 / 2 = 1620
    assert(dst.h == 1080);
    assert(dst.w == 1620);
    assert(dst.x == (1920 - 1620) / 2); // 150

    // 2. Test 16:9 view mode during battle:
    // Should use full 284x160 arena and fit to 16:9 layout
    khcom_set_battle_active(true);
    assert(khcom_is_battle_active());
    khcom_compute_effective_viewport(284, 160, 1920, 1080, &src, &dst, &base_w, &base_h);
    assert(src.x == 0 && src.y == 0 && src.w == 284 && src.h == 160);
    assert(base_w == 284 && base_h == 160);
    assert(dst.w > 0 && dst.h > 0);
    assert(dst.w <= 1920 && dst.h <= 1080);

    // 3. Test Native 3:2 view mode everywhere:
    // Should always be 3:2 regardless of battle state
    khcom_compute_effective_viewport(240, 160, 1920, 1080, &src, &dst, &base_w, &base_h);
    assert(src.x == 0 && src.y == 0 && src.w == 240 && src.h == 160);
    assert(base_w == 240 && base_h == 160);
    assert(dst.h == 1080 && dst.w == 1620);

    // 4. Test integer prescaled texture (e.g. sharp scaler 2x: 568x320)
    khcom_set_battle_active(false);
    khcom_compute_effective_viewport(568, 320, 1920, 1080, &src, &dst, &base_w, &base_h);
    assert(src.x == 44 && src.y == 0 && src.w == 480 && src.h == 320);
    assert(dst.h == 1080 && dst.w == 1620);

    // 5. Test authoritative mode table transitions and hook detections:
    extern void (*g_runtime_fn_entry_hook)(uint32_t entry_pc);
    assert(g_runtime_fn_entry_hook != nullptr);

    // Mode switch to Battle Arena (table 0x09ECEB40):
    g_cpu.R[0] = 0x09ECEB40u;
    g_runtime_fn_entry_hook(0x080010CCu);
    assert(khcom_is_battle_active());

    // Executing battle frame loop (mode_battle_1: 0x0800A4BC):
    g_runtime_fn_entry_hook(0x0800A4BCu);
    assert(khcom_is_battle_active());

    // Battle cleanup/exit (mode_battle_2: 0x0800A5A4):
    g_runtime_fn_entry_hook(0x0800A5A4u);
    assert(!khcom_is_battle_active());

    // Mode switch to Room / Field Exploration (table 0x09ECEB50):
    g_cpu.R[0] = 0x09ECEB50u;
    g_runtime_fn_entry_hook(0x080010CCu);
    assert(!khcom_is_battle_active());

    // Executing field room loop (mode_chkbtl_1: 0x0800A774):
    g_runtime_fn_entry_hook(0x0800A774u);
    assert(!khcom_is_battle_active());

    // Mode switch to Story Cutscenes (table 0x09EDE4D0):
    g_cpu.R[0] = 0x09EDE4D0u;
    g_runtime_fn_entry_hook(0x080010CCu);
    assert(!khcom_is_battle_active());

    // Executing cutscene loop (mode_movie_1: 0x0805EDFC):
    g_runtime_fn_entry_hook(0x0805EDFCu);
    assert(!khcom_is_battle_active());

    // Mode switch to Title Screen (table 0x09EF4E50):
    g_cpu.R[0] = 0x09EF4E50u;
    g_runtime_fn_entry_hook(0x080010CCu);
    assert(!khcom_is_battle_active());

    // Mode switch to VS Battle Arena (table 0x09ED9B98):
    g_cpu.R[0] = 0x09ED9B98u;
    g_runtime_fn_entry_hook(0x080010CCu);
    assert(khcom_is_battle_active());

    // Mode switch to World Map (table 0x09EF4DB0):
    g_cpu.R[0] = 0x09EF4DB0u;
    g_runtime_fn_entry_hook(0x080010CCu);
    assert(!khcom_is_battle_active());

    std::cout << "[PASS] C++ WidescreenViewModes unit test passed" << std::endl;
}

// ---------------------------------------------------------------------------------------------
// View-mode return-to-original tests. These model what gbarecomp's host does on a view change
// (runtime.cpp: run_game() resets the g_ws_* policy globals once at start, then
// apply_runtime_view_geometry() rewrites g_ws_active/extra/view_width on every switch and calls
// the extended_view_frame hook = khcom_update_widescreen_state()).
// ---------------------------------------------------------------------------------------------
namespace {
struct WsSnapshot {
    void* tilemap; void* bg_x; unsigned bg_layers; void* obj_attr;
    int authored, native_clip, pillar, pillar_l, pillar_r;
    bool operator==(const WsSnapshot& o) const {
        return tilemap == o.tilemap && bg_x == o.bg_x && bg_layers == o.bg_layers &&
               obj_attr == o.obj_attr && authored == o.authored && native_clip == o.native_clip &&
               pillar == o.pillar && pillar_l == o.pillar_l && pillar_r == o.pillar_r;
    }
};

WsSnapshot ws_snapshot() {
    return { reinterpret_cast<void*>(g_ws_tilemap_provider), reinterpret_cast<void*>(g_ws_bg_x_provider),
             g_ws_bg_x_provider_layers, reinterpret_cast<void*>(g_ws_obj_attr_x_provider),
             g_ws_authored_margin_layers, g_ws_obj_native_clip, g_ws_pillarbox,
             g_ws_pillarbox_left, g_ws_pillarbox_right };
}

void ws_print(const char* what, const WsSnapshot& s) {
    std::cout << "    " << what << ": tilemap=" << s.tilemap << " bg_x=" << s.bg_x
              << " obj_attr=" << s.obj_attr << " authored=" << s.authored
              << " native_clip=" << s.native_clip << " pillarbox=" << s.pillar << std::endl;
}

// What run_game() leaves in the policy globals for a faithful (native 3:2) run.
void ws_runtime_boot_defaults() {
    g_ws_tilemap_provider = nullptr;
    g_ws_bg_x_provider = nullptr;
    g_ws_bg_x_provider_layers = 0xFu;
    g_ws_obj_attr_x_provider = nullptr;
    g_ws_authored_margin_layers = 0;
    g_ws_obj_native_clip = 0;
    g_ws_pillarbox = 0;
    g_ws_pillarbox_left = 0;
    g_ws_pillarbox_right = 0;
}

// apply_runtime_view_geometry() + publish_extended_view_frame().
void ws_host_set_view(bool wide) {
    g_ws_extra_left = wide ? 22u : 0u;
    g_ws_extra_right = wide ? 22u : 0u;
    g_ws_extra = wide ? 22u : 0u;
    g_ws_view_width = wide ? 284u : 240u;
    g_ws_active = wide ? 1u : 0u;
    khcom_update_widescreen_state();
}

void ws_game_mode_switch(uint32_t descriptor) {
    g_cpu.R[0] = descriptor;
    g_runtime_fn_entry_hook(0x080010CCu);
}

constexpr uint32_t kModeBattle = 0x09ECEB40u;
constexpr uint32_t kModeField = 0x09ECEB50u;

// Player HP gauge sprite (32x8, priority 1, x=20, y=2): the anchored HUD element.
int ws_hp_gauge_x(bool* moved) {
    int out_x = 0;
    const int r = khcom_obj_attr_x_provider(3, 2 | 0x4000, 20 | 0x4000, 0x0400, &out_x);
    *moved = (r == 1 && out_x != 20);
    return r == 1 ? out_x : 20;
}
int ws_enemy_hp_x(bool* moved) {
    int out_x = 0;
    const int r = khcom_obj_attr_x_provider(4, 2 | 0x4000, 200 | 0x4000, 0x0400, &out_x);
    *moved = (r == 1 && out_x != 200);
    return r == 1 ? out_x : 200;
}
} // namespace

static void test_view_mode_return_to_original() {
    // Default configuration of a fresh install: the removed user option used to default to
    // "Original Centered", so the merged behaviour must not depend on it at all.
    gbaimp::HudAnchoring::instance().set_mode(gbaimp::HudAnchorMode::OriginalCentered);
    gbaimp::HudAnchoring::instance().set_horizontal_offset(32);

    g_ws_extra = g_ws_extra_left = g_ws_extra_right = 0; g_ws_view_width = 240; g_ws_active = 0;
    khcom_set_battle_active(false);
    ws_runtime_boot_defaults();
    const WsSnapshot boot = ws_snapshot();
    int failures = 0;
    auto expect = [&](bool cond, const char* what) {
        if (!cond) { ++failures; std::cout << "  [FAIL] " << what << std::endl; }
    };
    auto expect_boot = [&](const char* when) {
        const WsSnapshot now = ws_snapshot();
        if (!(now == boot)) {
            ++failures;
            std::cout << "  [FAIL] policy globals differ from native boot state: " << when << std::endl;
            ws_print("boot", boot); ws_print("now ", now);
        }
    };
    bool moved = false;

    // 1. Boot in 3:2: the frame hook runs every frame; nothing may be installed/changed.
    ws_host_set_view(false);
    expect_boot("3:2 boot after frame hook");
    expect(ws_hp_gauge_x(&moved) == 20 && !moved, "3:2 boot: HP gauge untouched");
    int out = 0;

    int el = -1, er = -1;
    expect(!khcom_widescreen_view_active() && !khcom_widescreen_presentation_active(), "3:2 boot: no widescreen state");
    khcom_effective_extra_pixels(&el, &er);
    expect(el == 0 && er == 0, "3:2 boot: effective extra pixels are 0/0");

    // 2. 3:2 -> 16:9 outside battle: pillarboxed, HUD untouched.
    ws_host_set_view(true);
    expect(khcom_widescreen_view_active() && !khcom_widescreen_presentation_active(), "16:9 field: view active, not presented");
    khcom_effective_extra_pixels(&el, &er);
    expect(el == 0 && er == 0, "16:9 field: effective extra pixels are 0/0 (dialogue/menus stay 240px)");
    expect(g_ws_pillarbox == 1, "16:9 field: margins pillarboxed");
    expect(g_ws_tilemap_provider != nullptr && g_ws_obj_attr_x_provider != nullptr, "16:9 field: providers installed");
    expect(ws_hp_gauge_x(&moved) == 20 && !moved, "16:9 field: HP gauge untouched");

    // 3. Enter battle in 16:9 with the (removed) HUD option at its default: HUD anchored automatically.
    ws_game_mode_switch(kModeBattle);
    expect(khcom_is_battle_active(), "battle flag set");
    expect(g_ws_pillarbox == 0, "16:9 battle: true widescreen (no pillarbox)");
    expect(khcom_widescreen_presentation_active(), "16:9 battle: presentation active");
    khcom_effective_extra_pixels(&el, &er);
    expect(el == 22 && er == 22, "16:9 battle: effective extra pixels are 22/22");
    expect(gbaimp::HudAnchoring::instance().settings().mode == gbaimp::HudAnchorMode::WidescreenAnchored &&
           gbaimp::HudAnchoring::instance().settings().horizontal_offset == 22,
           "16:9 battle: HudAnchoring state derived from the view (offset = real extra width)");
    expect(ws_hp_gauge_x(&moved) == 20 - 22 && moved, "16:9 battle: player HUD anchored to left screen edge");
    expect(ws_enemy_hp_x(&moved) == 200 + 22 && moved, "16:9 battle: enemy HUD anchored to right screen edge");

    // 4. Switch to 3:2 WHILE the battle runs: everything back to the original.
    ws_host_set_view(false);
    expect(khcom_is_battle_active(), "battle itself is still running");
    expect_boot("3:2 mid-battle");
    expect(!khcom_widescreen_view_active() && !khcom_widescreen_presentation_active(), "3:2 mid-battle: no widescreen state");
    khcom_effective_extra_pixels(&el, &er);
    expect(el == 0 && er == 0, "3:2 mid-battle: effective extra pixels are 0/0");
    expect(ws_hp_gauge_x(&moved) == 20 && !moved, "3:2 mid-battle: HP gauge untouched");
    expect(ws_enemy_hp_x(&moved) == 200 && !moved, "3:2 mid-battle: enemy gauge untouched");
    expect(khcom_tilemap_provider(2, 0, 0, nullptr) == 0, "3:2 mid-battle: tilemap provider declines");
    expect(khcom_bg_x_provider(0, 5, 0, &out) == 0, "3:2 mid-battle: bg provider declines");
    SDL_Rect src{}, dst{}; int bw = 0, bh = 0;
    khcom_compute_effective_viewport(240, 160, 1920, 1080, &src, &dst, &bw, &bh);
    expect(src.x == 0 && src.w == 240 && src.h == 160 && bw == 240 && dst.w == 1620 && dst.h == 1080,
           "3:2 mid-battle: original 3:2 viewport");

    // 5. Back to 16:9 mid-battle: anchoring resumes.
    ws_host_set_view(true);
    expect(g_ws_pillarbox == 0, "16:9 again mid-battle: widescreen");
    expect(ws_hp_gauge_x(&moved) == 20 - 22 && moved, "16:9 again: HUD anchored");

    // 6. Leave the battle in 16:9: HUD back in place, margins pillarboxed.
    ws_game_mode_switch(kModeField);
    expect(!khcom_is_battle_active(), "battle left");
    expect(g_ws_pillarbox == 1, "16:9 after battle: pillarboxed");
    expect(ws_hp_gauge_x(&moved) == 20 && !moved, "16:9 after battle: HUD untouched");

    // 7. Battle again, then leave battle while in 3:2, then 16:9 again (no latched state).
    ws_game_mode_switch(kModeBattle);
    ws_host_set_view(false);
    ws_game_mode_switch(kModeField);
    expect_boot("3:2 after leaving a battle");
    ws_host_set_view(true);
    expect(g_ws_pillarbox == 1 && !khcom_is_battle_active(), "16:9 field after 3:2 round trip: pillarboxed");
    expect(ws_hp_gauge_x(&moved) == 20 && !moved, "16:9 field after round trip: HUD untouched");
    ws_host_set_view(false);
    expect_boot("3:2 final");

    // 7b. Wider views (e.g. 21:9 = 22/21 style odd splits): each HUD side uses its own real extra width.
    g_ws_active = 1; g_ws_extra_left = 60; g_ws_extra_right = 59; g_ws_extra = 60; g_ws_view_width = 359;
    ws_game_mode_switch(kModeBattle);
    khcom_update_widescreen_state();
    expect(ws_hp_gauge_x(&moved) == 20 - 60 && moved, "asymmetric view: player HUD uses extra_left");
    expect(ws_enemy_hp_x(&moved) == 200 + 59 && moved, "asymmetric view: enemy HUD uses extra_right");
    ws_game_mode_switch(kModeField);
    ws_host_set_view(false);
    expect_boot("3:2 after asymmetric view");

    // 8. HUD anchoring bookkeeping of gba-improvements is back to its default in 3:2.
    expect(gbaimp::HudAnchoring::instance().settings().mode == gbaimp::HudAnchorMode::OriginalCentered,
           "3:2: HudAnchoring mode is the original one");
    expect(gbaimp::HudAnchoring::instance().settings().horizontal_offset == 32, "3:2: HudAnchoring offset default");
    uint16_t a0 = 10, a1 = 20;
    gbaimp::HudAnchoring::instance().process_oam_entry(a0, a1);
    expect(a0 == 10 && a1 == 20, "3:2: HudAnchoring::process_oam_entry is the identity");

    khcom_set_battle_active(false);
    ws_runtime_boot_defaults();
    g_ws_extra = g_ws_extra_left = g_ws_extra_right = 0; g_ws_view_width = 240; g_ws_active = 0;
    if (failures) {
        std::cout << "[FAIL] view-mode return-to-original: " << failures << " check(s) failed" << std::endl;
        std::abort();
    }
    std::cout << "[PASS] C++ view-mode return-to-original / HUD auto-anchoring unit test passed" << std::endl;
}

static void test_capture_harness() {
    // Harness OFF (no KHCOM_* variables): inert, no argv injection.
    const char* vars[] = {"KHCOM_CAPTURE_DIR", "KHCOM_CAPTURE_FRAMES", "KHCOM_INPUT_SCRIPT", "KHCOM_SET",
                          "KHCOM_WINDOW_SIZE", "KHCOM_SAVESTATE_LOAD", "KHCOM_SAVESTATE_SAVE", "KHCOM_EXIT_FRAME",
                          "KHCOM_CAPTURE_VARIANTS", "KHCOM_FAST"};
    bool any_env = false;
    for (const char* v : vars) if (std::getenv(v)) any_env = true;
    if (!any_env) {
        assert(!khcom::capture::active());
        assert(khcom::capture::extra_runtime_args().empty());
        assert(khcom::capture::settings_overrides().empty());
    }

    // PNG writer: 3x2 RGB with padded pitch; verify signature, IHDR, CRCs, stored-deflate payload and Adler-32.
    const std::uint8_t px[2 * 12] = {
        255, 0, 0,   0, 255, 0,   0, 0, 255,   9, 9, 9,
        1, 2, 3,     4, 5, 6,     7, 8, 9,     9, 9, 9};
    const std::string path = (std::filesystem::temp_directory_path() / "khcom_capture_test.png").string();
    assert(khcom::capture::write_png_rgb(path, px, 3, 2, 12));
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::filesystem::remove(path);
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    assert(d.size() > 8 && std::memcmp(d.data(), sig, 8) == 0);
    auto be32 = [&](size_t o) { return (std::uint32_t(d[o]) << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]; };
    auto crc = [](const std::uint8_t* p, size_t n) {
        std::uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i) {
            c ^= p[i];
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
        }
        return ~c;
    };
    size_t pos = 8;
    std::vector<std::uint8_t> idat;
    bool saw_ihdr = false, saw_iend = false;
    while (pos + 12 <= d.size()) {
        const std::uint32_t len = be32(pos);
        const std::string tag(reinterpret_cast<const char*>(&d[pos + 4]), 4);
        assert(pos + 12 + len <= d.size());
        assert(be32(pos + 8 + len) == crc(&d[pos + 4], 4 + len));
        if (tag == "IHDR") { assert(be32(pos + 8) == 3 && be32(pos + 12) == 2 && d[pos + 16] == 8 && d[pos + 17] == 2); saw_ihdr = true; }
        if (tag == "IDAT") idat.insert(idat.end(), d.begin() + pos + 8, d.begin() + pos + 8 + len);
        if (tag == "IEND") saw_iend = true;
        pos += 12 + len;
    }
    assert(saw_ihdr && saw_iend && pos == d.size());
    assert(idat.size() > 6 && idat[0] == 0x78);
    // one stored block: [BFINAL=1, LEN, NLEN, raw...], then Adler-32
    assert(idat[2] == 1);
    const size_t blen = idat[3] | (idat[4] << 8);
    assert(blen == 2 * (1 + 9) && static_cast<std::uint16_t>(idat[5] | (idat[6] << 8)) == static_cast<std::uint16_t>(~blen));
    const std::uint8_t* raw = &idat[7];
    assert(raw[0] == 0 && std::memcmp(raw + 1, px, 9) == 0);
    assert(raw[10] == 0 && std::memcmp(raw + 11, px + 12, 9) == 0);
    std::uint32_t a = 1, b = 0;
    for (size_t i = 0; i < blen; ++i) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    const size_t ad = 7 + blen;
    assert(idat.size() == ad + 4 && ((std::uint32_t(idat[ad]) << 24) | (idat[ad + 1] << 16) | (idat[ad + 2] << 8) | idat[ad + 3]) == ((b << 16) | a));
    std::cout << "[PASS] Capture harness: inert without env, PNG writer structure/CRC/Adler verified" << std::endl;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "           KHCOMRecomp Native C++ Unit Test Runner             " << std::endl;
    std::cout << "================================================================" << std::endl;

    int modules_run = 0;
    ++modules_run; test_gbaimp_integration();
    ++modules_run; test_hud_anchoring();
    ++modules_run; test_dialogue_components();
    ++modules_run; test_widescreen_view_modes();
    ++modules_run; test_view_mode_return_to_original();
    ++modules_run; test_capture_harness();
    assert(run_overlay_render_tests(nullptr, false) == 0);
    assert(run_overlay_engine_tests(false) == 0);
    ++modules_run;
    std::cout << "[PASS] HD dialogue text overlay: fonts, layout, fit, reveal, caches, drawing (tests/test_overlay_render.cpp)" << std::endl;

    std::cout << "================================================================" << std::endl;
    std::cout << "ALL NATIVE C++ UNIT TESTS PASSED SUCCESSFULLY (" << modules_run << "/" << modules_run << " MODULES)." << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}
