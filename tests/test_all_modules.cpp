#include "gbaimp/hooks.h"
#include "gbaimp/hud_anchoring.h"
#include "gbaimp_integration.h"
#include "dialogue_backlog.h"
#include "widescreen_adapter.h"
#include "gbaimp/widescreen.h"
#include "gbaimp/m4a_music_hook.h"
#include "gbaimp/hd_audio_player.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/perf_hud.h"
#include "armv4t/runtime_arm.h"
#include "gbaimp/capture_harness.h"
#include "game_config.h"
#include "recomp_runtime_ui.h"

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

int run_text_adapter_tests(bool verbose);
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

// HD music trigger: this game's m4a table and wiring against the library hook (gbaimp::M4aMusicHook).
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
    // The game's table (used when the locator does not recognise the ROM) in an independent hook; the sink is the shared player.
    gbaimp::M4aMusicHook hook;
    gbaimp::M4aAddresses addr;
    addr.song_num_start = kM4aSongNumStart;
    addr.song_num_start_or_change = kM4aSongNumStartOrChange;
    addr.song_num_start_or_continue = kM4aSongNumStartOrContinue;
    addr.song_num_stop = kM4aSongNumStop;
    addr.mplay_all_stop = kM4aMPlayAllStop;
    addr.song_table = kM4aSongTable;
    addr.song_table_entries = kM4aSongTableEntries;
    assert(addr.complete());
    hook.set_addresses(addr);

    hook.on_song_start(7, 0);
    assert(hd.current_song_id() == 7);                 // mapped song starts the HD track
    hook.on_song_start(100, 5);                        // unmapped, other player (e.g. a sound effect)
    assert(hd.current_song_id() == 7);
    hook.on_song_start(101, 0);                        // unmapped, same player: the music was replaced
    assert(hd.current_song_id() == 0);
    hook.on_song_start(8, 1);                          // mapped but the file is missing: silent, no crash
    assert(hd.current_song_id() == 0);
    hook.on_song_start(7, 2);
    assert(hd.current_song_id() == 7);
    hook.on_song_stop(9);                              // some other song stopped: HD stays
    assert(hd.current_song_id() == 7);
    hook.on_song_stop(7);
    assert(hd.current_song_id() == 0);
    hook.on_song_start(7, 2);
    hook.on_all_stop();
    assert(hd.current_song_id() == 0);

    // Through the function-entry dispatch with this game's addresses (R0 is a u16 argument; other pcs are ignored).
    hook.set_song_player_reader([](uint32_t) -> uint32_t { return 3; });
    hook.on_function_entry(0x08000240u, 0x00010007u);
    assert(hd.current_song_id() == 0);
    hook.on_function_entry(kM4aSongNumStartOrChange, 0x00010007u);
    assert(hd.current_song_id() == 7);
    hook.on_function_entry(kM4aSongNumStop, 0x00010007u);
    assert(hd.current_song_id() == 0);
    hook.set_song_player_reader(nullptr);

    // Disabled HD audio never triggers.
    hd.set_enabled(false);
    hook.on_song_start(7, 0);
    assert(hd.current_song_id() == 0);
    hd.set_enabled(true);

    const gbaimp::M4aHookStats st = hook.stats();
    assert(st.starts >= 5 && st.mapped_triggers >= 4 && st.hd_stops >= 3);
    hd.stop_music();
    hd.set_enabled(false);
    std::error_code ec;
    fs::remove_all(dir, ec);

    // The locator finds this ROM's m4a entry points by code shape and agrees with the game's table (checked when the
    // cartridge image is next to the sources; the repository does not ship it).
    const fs::path rom_path = fs::path("roms") / "Kingdom Hearts - Chain of Memories (U)(Venom).gba";
    if (fs::exists(rom_path)) {
        std::ifstream rf(rom_path, std::ios::binary);
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
        const gbaimp::M4aAddresses found = gbaimp::locate_m4a(rom.data(), rom.size());
        assert(found.complete());
        assert(found.song_num_start == kM4aSongNumStart && found.song_num_start_or_change == kM4aSongNumStartOrChange);
        assert(found.song_num_start_or_continue == kM4aSongNumStartOrContinue && found.song_num_stop == kM4aSongNumStop);
        assert(found.mplay_all_stop == kM4aMPlayAllStop && found.song_table == kM4aSongTable);
        assert(found.song_table_entries == kM4aSongTableEntries);
    }
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
    gbaimp::widescreen_compute_viewport(284, 160, 1920, 1080, &src, &dst, &base_w, &base_h);
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
    gbaimp::widescreen_compute_viewport(284, 160, 1920, 1080, &src, &dst, &base_w, &base_h);
    assert(src.x == 0 && src.y == 0 && src.w == 284 && src.h == 160);
    assert(base_w == 284 && base_h == 160);
    assert(dst.w > 0 && dst.h > 0);
    assert(dst.w <= 1920 && dst.h <= 1080);

    // 3. Test Native 3:2 view mode everywhere:
    // Should always be 3:2 regardless of battle state
    gbaimp::widescreen_compute_viewport(240, 160, 1920, 1080, &src, &dst, &base_w, &base_h);
    assert(src.x == 0 && src.y == 0 && src.w == 240 && src.h == 160);
    assert(base_w == 240 && base_h == 160);
    assert(dst.h == 1080 && dst.w == 1620);

    // 4. Test integer prescaled texture (e.g. sharp scaler 2x: 568x320)
    khcom_set_battle_active(false);
    gbaimp::widescreen_compute_viewport(568, 320, 1920, 1080, &src, &dst, &base_w, &base_h);
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
    expect(!gbaimp::Widescreen::instance().view_active() && !gbaimp::Widescreen::instance().presentation_active(), "3:2 boot: no widescreen state");
    gbaimp::Widescreen::instance().effective_extra_pixels(&el, &er);
    expect(el == 0 && er == 0, "3:2 boot: effective extra pixels are 0/0");

    // 2. 3:2 -> 16:9 outside battle: pillarboxed, HUD untouched.
    ws_host_set_view(true);
    expect(gbaimp::Widescreen::instance().view_active() && !gbaimp::Widescreen::instance().presentation_active(), "16:9 field: view active, not presented");
    gbaimp::Widescreen::instance().effective_extra_pixels(&el, &er);
    expect(el == 0 && er == 0, "16:9 field: effective extra pixels are 0/0 (dialogue/menus stay 240px)");
    expect(g_ws_pillarbox == 1, "16:9 field: margins pillarboxed");
    expect(g_ws_tilemap_provider != nullptr && g_ws_obj_attr_x_provider != nullptr, "16:9 field: providers installed");
    expect(ws_hp_gauge_x(&moved) == 20 && !moved, "16:9 field: HP gauge untouched");

    // 3. Enter battle in 16:9 with the (removed) HUD option at its default: HUD anchored automatically.
    ws_game_mode_switch(kModeBattle);
    expect(khcom_is_battle_active(), "battle flag set");
    expect(g_ws_pillarbox == 0, "16:9 battle: true widescreen (no pillarbox)");
    expect(gbaimp::Widescreen::instance().presentation_active(), "16:9 battle: presentation active");
    gbaimp::Widescreen::instance().effective_extra_pixels(&el, &er);
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
    expect(!gbaimp::Widescreen::instance().view_active() && !gbaimp::Widescreen::instance().presentation_active(), "3:2 mid-battle: no widescreen state");
    gbaimp::Widescreen::instance().effective_extra_pixels(&el, &er);
    expect(el == 0 && er == 0, "3:2 mid-battle: effective extra pixels are 0/0");
    expect(ws_hp_gauge_x(&moved) == 20 && !moved, "3:2 mid-battle: HP gauge untouched");
    expect(ws_enemy_hp_x(&moved) == 200 && !moved, "3:2 mid-battle: enemy gauge untouched");
    expect(gbaimp::default_tilemap_provider(2, 0, 0, nullptr) == 0, "3:2 mid-battle: tilemap provider declines");
    expect(gbaimp::default_bg_x_provider(0, 5, 0, &out) == 0, "3:2 mid-battle: bg provider declines");
    SDL_Rect src{}, dst{}; int bw = 0, bh = 0;
    gbaimp::widescreen_compute_viewport(240, 160, 1920, 1080, &src, &dst, &bw, &bh);
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
    // The harness itself (parsers, PNG writer, hooks) is tested in gba-improvements. Here: this game's wiring. The
    // variables are KHCOM_*; without any of them the harness is inert and injects nothing into the runtime's argv.
    const char* vars[] = {"KHCOM_CAPTURE_DIR", "KHCOM_CAPTURE_FRAMES", "KHCOM_INPUT_SCRIPT", "KHCOM_SET",
                          "KHCOM_WINDOW_SIZE", "KHCOM_SAVESTATE_LOAD", "KHCOM_SAVESTATE_SAVE", "KHCOM_EXIT_FRAME",
                          "KHCOM_CAPTURE_VARIANTS", "KHCOM_FAST", "KHCOM_PUSH_KEYS", "KHCOM_EXIT_AFTER_CAPTURE"};
    bool any_env = false;
    for (const char* v : vars) if (std::getenv(v)) any_env = true;
    khcom::configure_capture_harness();
    if (!any_env) {
        assert(!gbaimp::capture::active());
        assert(!gbaimp::capture::wants_native_frame());
        assert(gbaimp::capture::extra_runtime_args().empty());
        assert(gbaimp::capture::settings_overrides().empty());
    }
    std::cout << "[PASS] Capture harness: configured for the KHCOM_* variables, inert without them" << std::endl;
}

// khcom_config.ini through the settings registry (game_config.cpp + gbaimp/host_settings.h): a file written by an earlier
// build loads into the module settings and is written back unchanged (same sections, key order and values); legacy and
// unknown keys are read and dropped. Runs in a scratch directory so the real khcom_config.ini / keybinds.ini stay untouched.
static void test_config_ini_roundtrip() {
    namespace fs = std::filesystem;
    const char* canonical =
        "[Video]\n"
        "fps_target=2\nmotion_smoothing=2\nxbrz_scale=2\ncolor_profile=2\nscreen_mask=1\nmask_intensity=40\n\n"
        "[Audio]\n"
        "hd_enabled=1\nbgm_volume=135\nsfx_volume=100\neq_preset=2\nstereo_width=130\nanti_aliasing=1\nlimiter=1\n\n"
        "[Dialogue]\n"
        "backlog_enable=1\n\n"
        "[HdText]\n"
        "hdtext_font=Rounded Sans\nhdtext_enabled=1\nhdtext_size=90\nhdtext_line_spacing=115\nhdtext_letter_spacing=5\n"
        "hdtext_wrap=1\nhdtext_style=1\nhdtext_shadow_offset=6\nhdtext_outline_width=8\nhdtext_color=1\n"
        "hdtext_shadow_color=0\nhdtext_outline_color=0\nhdtext_opacity=100\nhdtext_padding=0\nhdtext_auto_fit=1\n"
        "hdtext_min_size=50\n\n"
        "[Controls]\n"
        "analog_mode=2\nanalog_deadzone=18\ngamepad_profile=5\npad_attack=0\npad_jump=1\npad_deck_l=4\npad_deck_r=5\n"
        "kb_preset=4\n\n"
        "[Gameplay]\n"
        "turbo_dialog=1\n\n"
        "[Diagnostics]\n"
        "perf_mode=1\nperf_position=5\nperf_theme=0\nhud_custom_x=1394\nhud_custom_y=54\n";

    const fs::path dir = fs::temp_directory_path() / "khcom_config_roundtrip";
    fs::create_directories(dir);
    const fs::path prev = fs::current_path();
    fs::current_path(dir);
    auto write = [](const std::string& text) { std::ofstream f("khcom_config.ini", std::ios::binary); f << text; };
    auto same = [&](const std::string& got, const std::string& want, const char* what) {
        if (got == want) return;
        std::cout << "[FAIL] khcom_config.ini " << what << ": written file differs\n--- got ---\n" << got
                  << "--- expected ---\n" << want << "-----" << std::endl;
        std::abort();
    };
    auto read = [] {
        std::ifstream f("khcom_config.ini", std::ios::binary);
        std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());   // the file is written in text mode (CRLF on Windows)
        return t;
    };

    // 1. A file in the layout of an earlier build comes back byte for byte (the volume above 100 % is kept: range 0..150).
    write(canonical);
    load_khcom_config();
    save_khcom_config();
    same(read(), canonical, "round trip");

    // The runtime menu built from the registry: one row for every setting of the game's menu, in this game's section order,
    // each one answering ui_get / ui_get_text / ui_action through the callbacks handed to the runtime.
    {
        gbarecomp::RunOptions opts = create_run_options();
        assert(opts.ui_extra_item_count == 55);
        assert(opts.ui_get && opts.ui_set && opts.ui_action && opts.ui_get_text && opts.ui_set_text && opts.ui_enabled);
        const char* last_section = "";
        std::vector<std::string> section_order;
        for (size_t i = 0; i < opts.ui_extra_item_count; ++i) {
            const auto& it = static_cast<const RecompRuntimeUiItem*>(opts.ui_extra_items)[i];
            if (std::strcmp(it.section, last_section) != 0) { section_order.push_back(it.section); last_section = it.section; }
            int v = 0;
            char text[64];
            switch (it.type) {
                case RECOMP_RUNTIME_UI_ACTION: break;
                case RECOMP_RUNTIME_UI_TEXT: assert(opts.ui_get_text(it.key, text, sizeof(text)) == 1); break;
                default: assert(opts.ui_get(it.key, &v) == 1); break;
            }
        }
        const std::vector<std::string> expected_sections = {"Graphics", "Controller (Gamepad)", "Keyboard Controls",
            "Gameplay & Assist", "Dialogue Text (HD)", "Dialogue & Story", "Audio", "Diagnostics & HUD"};
        assert(section_order == expected_sections);
        assert(opts.ui_enabled("system.resume") == 0 && opts.ui_enabled("video.fps_target") == 1);
        // legacy ids sent by an old menu are accepted by ui_set and unknown for ui_get
        int v = 0;
        assert(opts.ui_set("dialogue.text_mode", 1) == 1 && opts.ui_get("dialogue.text_mode", &v) == 0);
        // the volumes go up to 150 %, the sample rate / master volume of the library are not offered
        assert(opts.ui_set("audio.bgm_volume", 150) == 1 && opts.ui_get("audio.bgm_volume", &v) == 1 && v == 150);
        assert(opts.ui_get("audio.master_volume", &v) == 0 && opts.ui_get("audio.sample_rate", &v) == 0);
    }

    // 2. CRLF endings, legacy text keys, the retired hud_anchoring key, junk lines and an unknown key never fail the load;
    //    none of them is written back and the known values are unchanged.
    std::string messy;
    for (const char* p = canonical; *p; ++p) { if (*p == '\n') messy += '\r'; messy += *p; }
    messy += "[Dialogue]\r\ntext_mode=2\r\nfont_family=3\r\nfont_size=1\r\nfont_density=1\r\nfont_scale=2\r\n"
             "font_style=1\r\nline_capacity=2\r\nsoft_word_wrap=1\r\nhud_anchoring=1\r\nnot_a_key=7\r\nthis line has no equals\r\n"
             "fps_target=abc\r\n; comment\r\n";
    write(messy);
    load_khcom_config();
    save_khcom_config();
    same(read(), canonical, "messy file");

    // 3. Values apply through the settings: another file changes the live module state and is written back as given.
    std::string changed = canonical;
    auto replace = [&](const char* a, const char* b) {
        const size_t i = changed.find(a);
        assert(i != std::string::npos);
        changed.replace(i, std::strlen(a), b);
    };
    replace("fps_target=2", "fps_target=1");
    replace("bgm_volume=135", "bgm_volume=60");
    replace("hdtext_size=90", "hdtext_size=120");
    replace("perf_mode=1", "perf_mode=3");
    write(changed);
    load_khcom_config();
    assert(static_cast<int>(gbaimp::FrameInterpolator::instance().settings().mode) == 1);
    assert(gbaimp::HdAudioPlayer::instance().settings().bgm_volume.load() > 0.59f &&
           gbaimp::HdAudioPlayer::instance().settings().bgm_volume.load() < 0.61f);
    assert(static_cast<int>(gbaimp::PerfHud::instance().settings().mode) == 3);
    save_khcom_config();
    same(read(), changed, "changed values");

    // 4. A value outside the range of a setting is clamped, not stored as is.
    replace("bgm_volume=60", "bgm_volume=900");
    write(changed);
    load_khcom_config();
    save_khcom_config();
    replace("bgm_volume=900", "bgm_volume=150");
    same(read(), changed, "clamped value");

    fs::current_path(prev);
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << "[PASS] khcom_config.ini: earlier layout loads and is written back unchanged, legacy keys ignored, values clamped" << std::endl;
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
    assert(run_text_adapter_tests(false) == 0);
    assert(run_overlay_engine_tests(false) == 0);
    ++modules_run;
    std::cout << "[PASS] HD dialogue text: embedded fonts, word/page conversion, game colours and settings (tests/test_text_adapter.cpp)" << std::endl;
    ++modules_run; test_config_ini_roundtrip();

    std::cout << "================================================================" << std::endl;
    std::cout << "ALL NATIVE C++ UNIT TESTS PASSED SUCCESSFULLY (" << modules_run << "/" << modules_run << " MODULES)." << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}
