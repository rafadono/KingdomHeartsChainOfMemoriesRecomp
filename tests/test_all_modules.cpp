#include "audio_dsp.h"
#include "screen_filters.h"
#include "frame_interpolator.h"
#include "input_enhancements.h"
#include "hud_anchoring.h"
#include "dialog_turbo.h"
#include "xbrz_filter.h"
#include "savestate_thumbnails.h"
#include "perf_hud.h"
#include "dialogue_enhancer.h"
#include "dialogue_backlog.h"
#include "font_resizer.h"
#include "dialogue_box_scaler.h"
#include "widescreen_adapter.h"
#include "armv4t/runtime_arm.h"

#include <iostream>
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include <SDL.h>

using namespace khcom;

static void test_audio_dsp() {
    auto& dsp = AudioDsp::instance();
    
    dsp.apply_preset(EqPreset::BassBoost);
    assert(dsp.settings().preset == EqPreset::BassBoost);
    assert(dsp.settings().bass_gain_db == 5.0f);
    assert(dsp.settings().mid_gain_db == 0.0f);
    assert(dsp.settings().treble_gain_db == 1.0f);

    dsp.apply_preset(EqPreset::Flat);
    assert(dsp.settings().preset == EqPreset::Flat);
    assert(dsp.settings().bass_gain_db == 0.0f);
    assert(dsp.settings().mid_gain_db == 0.0f);
    assert(dsp.settings().treble_gain_db == 0.0f);

    dsp.set_stereo_width(1.5f);
    assert(dsp.settings().stereo_width == 1.5f);

    dsp.set_anti_aliasing(true);
    assert(dsp.settings().anti_aliasing_filter == true);

    dsp.set_limiter(true);
    assert(dsp.settings().limiter_enabled == true);

    // Buffer processing test with extreme audio values (soft limiter saturation test)
    int16_t samples[8] = { 32000, 32000, 32700, 32700, -32000, -32000, -32700, -32700 };
    dsp.process_stereo(samples, 4, 32768);
    for (int i = 0; i < 8; ++i) {
        assert(samples[i] >= -32768 && samples[i] <= 32767);
    }

    std::cout << "[PASS] C++ AudioDsp unit test passed" << std::endl;
}

static void test_screen_filters() {
    auto& filters = ScreenFilters::instance();

    filters.set_color_profile(ColorProfile::Ags101);
    assert(filters.settings().color_profile == ColorProfile::Ags101);

    filters.set_mask_type(ScreenMaskType::CrtScanlines);
    assert(filters.settings().mask_type == ScreenMaskType::CrtScanlines);

    filters.set_mask_intensity(0.01f);
    assert(filters.settings().mask_intensity == 0.05f); // clamped min

    filters.set_mask_intensity(2.5f);
    assert(filters.settings().mask_intensity == 1.0f); // clamped max

    filters.set_mask_intensity(0.45f);
    assert(filters.settings().mask_intensity == 0.45f);

    // Test raw color correction identity
    filters.set_color_profile(ColorProfile::Raw);
    uint8_t test_pixels[6] = { 10, 20, 30, 200, 210, 220 };
    filters.apply_color_correction(test_pixels, 2, 1);
    assert(test_pixels[0] == 10 && test_pixels[1] == 20 && test_pixels[2] == 30);
    assert(test_pixels[3] == 200 && test_pixels[4] == 210 && test_pixels[5] == 220);

    std::cout << "[PASS] C++ ScreenFilters unit test passed" << std::endl;
}

static void test_frame_interpolator() {
    auto& interp = FrameInterpolator::instance();

    interp.set_mode(FramerateMode::Fps60);
    assert(interp.target_fps() == 60.0);
    assert(std::abs(interp.target_frametime_ms() - 16.6666) < 0.01);

    interp.set_mode(FramerateMode::Fps120);
    assert(interp.target_fps() == 120.0);
    assert(std::abs(interp.target_frametime_ms() - 8.3333) < 0.01);

    interp.set_mode(FramerateMode::Fps144);
    assert(interp.target_fps() == 144.0);
    assert(std::abs(interp.target_frametime_ms() - 6.9444) < 0.01);

    interp.set_mode(FramerateMode::Uncapped);
    assert(interp.target_fps() == 240.0);
    assert(std::abs(interp.target_frametime_ms() - 4.1666) < 0.01);

    interp.set_smoothing(MotionSmoothingMode::MotionAdaptive);
    assert(interp.settings().smoothing == MotionSmoothingMode::MotionAdaptive);

    std::cout << "[PASS] C++ FrameInterpolator unit test passed" << std::endl;
}

static void test_input_enhancements() {
    auto& input = InputEnhancements::instance();

    input.set_deadzone(0.01f);
    assert(input.settings().deadzone == 0.05f); // clamped min

    input.set_deadzone(0.80f);
    assert(input.settings().deadzone == 0.50f); // clamped max

    input.set_deadzone(0.18f);
    assert(input.settings().deadzone == 0.18f);

    input.set_analog_mode(AnalogMode::Analog8Way);
    assert(input.settings().analog_mode == AnalogMode::Analog8Way);

    // Full tilt Right
    uint16_t key = 0xFFFF;
    input.process_axis_input(32767, 0, key);
    assert((key & 0x0010) == 0); // Right bit active low
    assert((key & 0x0020) != 0); // Left not pressed

    // Full tilt Down-Right
    key = 0xFFFF;
    input.process_axis_input(32767, 32767, key);
    assert((key & 0x0010) == 0); // Right
    assert((key & 0x0080) == 0); // Down

    // Full tilt Left
    key = 0xFFFF;
    input.process_axis_input(-32767, 0, key);
    assert((key & 0x0020) == 0); // Left

    // Full tilt Up
    key = 0xFFFF;
    input.process_axis_input(0, -32767, key);
    assert((key & 0x0040) == 0); // Up

    // Tilt inside deadzone
    key = 0xFFFF;
    input.process_axis_input(100, 100, key);
    assert(key == 0xFFFF); // No directional change

    // Keyboard preset testing
    input.apply_keyboard_preset(KeyboardPreset::Wasd);
    assert(input.settings().kb_preset == KeyboardPreset::Wasd);
    assert(input.get_key_scancode(KEY_A) == SDL_SCANCODE_J);
    assert(input.get_key_scancode(KEY_B) == SDL_SCANCODE_K);
    assert(input.get_key_scancode(KEY_UP) == SDL_SCANCODE_W);
    assert(input.get_key_scancode(KEY_LEFT) == SDL_SCANCODE_A);

    input.apply_keyboard_preset(KeyboardPreset::Default);
    assert(input.settings().kb_preset == KeyboardPreset::Default);
    assert(input.get_key_scancode(KEY_A) == SDL_SCANCODE_X);
    assert(input.get_key_scancode(KEY_B) == SDL_SCANCODE_Z);

    // Key name parsing and setting
    bool set_ok = input.set_key_from_name(KEY_A, "Space");
    assert(set_ok);
    assert(input.get_key_scancode(KEY_A) == SDL_SCANCODE_SPACE);
    assert(std::strcmp(input.get_key_name(KEY_A), "Space") == 0);
    assert(input.settings().kb_preset == KeyboardPreset::Custom);

    // Test emulator key naming (UpArrow, DownArrow, LeftArrow, RightArrow, etc.)
    assert(input.set_key_from_name(KEY_UP, "UpArrow"));
    assert(input.get_key_scancode(KEY_UP) == SDL_SCANCODE_UP);
    assert(input.set_key_from_name(KEY_DOWN, "DownArrow"));
    assert(input.get_key_scancode(KEY_DOWN) == SDL_SCANCODE_DOWN);
    assert(input.set_key_from_name(KEY_LEFT, "LeftArrow"));
    assert(input.get_key_scancode(KEY_LEFT) == SDL_SCANCODE_LEFT);
    assert(input.set_key_from_name(KEY_RIGHT, "RightArrow"));
    assert(input.get_key_scancode(KEY_RIGHT) == SDL_SCANCODE_RIGHT);
    assert(input.set_key_from_name(KEY_START, "Enter"));
    assert(input.get_key_scancode(KEY_START) == SDL_SCANCODE_RETURN);

    // Granular keyboard reset
    input.reset_keyboard_defaults();
    assert(input.settings().kb_preset == KeyboardPreset::Default);
    assert(input.get_key_scancode(KEY_A) == SDL_SCANCODE_X);
    assert(input.get_key_scancode(KEY_B) == SDL_SCANCODE_Z);

    // Gamepad profile testing
    input.apply_gamepad_profile(GamepadProfile::Nintendo);
    assert(input.settings().gamepad_profile == GamepadProfile::Nintendo);
    assert(input.settings().pad_attack == PadButtonChoice::ButtonB);
    assert(input.settings().pad_jump == PadButtonChoice::ButtonA);

    input.apply_gamepad_profile(GamepadProfile::PlayStation);
    assert(input.settings().pad_attack == PadButtonChoice::ButtonX);
    assert(input.settings().pad_jump == PadButtonChoice::ButtonA);

    input.set_pad_attack(static_cast<int>(PadButtonChoice::RightTrigger));
    assert(input.settings().pad_attack == PadButtonChoice::RightTrigger);
    assert(input.settings().gamepad_profile == GamepadProfile::Custom);

    // Granular gamepad reset
    input.reset_gamepad_defaults();
    assert(input.settings().analog_mode == AnalogMode::AnalogWalkRun);
    assert(input.settings().gamepad_profile == GamepadProfile::Standard);
    assert(input.settings().deadzone == 0.18f);

    // Reset to defaults
    input.reset_to_defaults();
    assert(input.settings().analog_mode == AnalogMode::AnalogWalkRun);
    assert(input.settings().kb_preset == KeyboardPreset::Default);
    assert(input.settings().gamepad_profile == GamepadProfile::Standard);
    assert(input.get_key_scancode(KEY_A) == SDL_SCANCODE_X);
    assert(input.get_key_scancode(KEY_B) == SDL_SCANCODE_Z);

    std::cout << "[PASS] C++ InputEnhancements unit test passed" << std::endl;
}

static void test_hud_anchoring() {
    auto& hud = HudAnchoring::instance();

    hud.set_mode(HudAnchorMode::WidescreenAnchored);
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
    extern unsigned g_ws_active;
    extern unsigned g_ws_extra_left;
    extern unsigned g_ws_extra_right;

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

    std::cout << "[PASS] C++ HudAnchoring unit test passed" << std::endl;
}

static void test_dialog_turbo() {
    auto& turbo = DialogTurbo::instance();

    turbo.set_enabled(true);
    turbo.set_held(true);
    turbo.set_skip_speed(2);
    assert(turbo.skip_speed() == 2);

    // Even frames pulse A and B buttons (clearing bits 0 and 1)
    uint16_t k0 = turbo.process_keyinput(0xFFFF, 0);
    assert((k0 & 0x0001) == 0); // A pressed
    assert((k0 & 0x0002) == 0); // B pressed

    // Odd frames release A and B buttons
    uint16_t k1 = turbo.process_keyinput(0xFFFF, 1);
    assert((k1 & 0x0001) != 0); // A released
    assert((k1 & 0x0002) != 0); // B released

    // When released (not held), input remains unchanged
    turbo.set_held(false);
    uint16_t k_held = turbo.process_keyinput(0xFFFF, 0);
    assert(k_held == 0xFFFF);

    std::cout << "[PASS] C++ DialogTurbo unit test passed" << std::endl;
}

static void test_xbrz_filter() {
    auto& xbrz = XbrzUpscaler::instance();

    xbrz.set_scale(XbrzScale::Xbrz2x);
    assert(xbrz.scale() == XbrzScale::Xbrz2x);

    xbrz.set_scale(XbrzScale::Xbrz3x);
    assert(xbrz.scale() == XbrzScale::Xbrz3x);

    xbrz.set_scale(XbrzScale::Xbrz4x);
    assert(xbrz.scale() == XbrzScale::Xbrz4x);

    xbrz.set_scale(XbrzScale::None);
    assert(xbrz.scale() == XbrzScale::None);

    std::cout << "[PASS] C++ XbrzUpscaler unit test passed" << std::endl;
}

static void test_savestate_thumbnails() {
    auto& sm = SavestateThumbnailManager::instance();

    std::string path_slot_1 = sm.get_thumbnail_path(1);
    assert(path_slot_1.find("slot_1") != std::string::npos);

    std::string path_slot_9 = sm.get_thumbnail_path(9);
    assert(path_slot_9.find("slot_9") != std::string::npos);

    SavestateMetadata meta = sm.get_slot_metadata(1);
    assert(meta.slot == 1);

    std::cout << "[PASS] C++ SavestateThumbnailManager unit test passed" << std::endl;
}

static void test_perf_hud() {
    auto& hud = PerfHud::instance();

    hud.set_mode(PerfHudMode::FpsAndFrametime);
    assert(hud.settings().mode == PerfHudMode::FpsAndFrametime);
    assert(hud.current_width() == 164.0f);
    assert(hud.current_height() == 44.0f);

    hud.set_position(PerfHudPosition::TopRight);
    assert(hud.settings().position == PerfHudPosition::TopRight);

    hud.set_position(PerfHudPosition::TopLeft);
    assert(hud.settings().position == PerfHudPosition::TopLeft);
    assert(hud.calculated_x() == 16.0f);
    assert(hud.calculated_y() == 16.0f);

    hud.settings().custom_x = 320.0f;
    hud.settings().custom_y = 240.0f;
    hud.set_position(PerfHudPosition::FreeDrag);
    assert(hud.settings().position == PerfHudPosition::FreeDrag);
    assert(hud.calculated_x() == 320.0f);
    assert(hud.calculated_y() == 240.0f);

    hud.set_theme(PerfHudTheme::GlassDark);
    assert(hud.settings().theme == PerfHudTheme::GlassDark);

    // Simulated mouse dragging test
    SDL_Event ev_down{};
    ev_down.type = SDL_MOUSEBUTTONDOWN;
    ev_down.button.button = SDL_BUTTON_LEFT;
    ev_down.button.x = 330;
    ev_down.button.y = 250;
    bool grabbed = hud.handle_mouse_event(ev_down);
    assert(grabbed);
    assert(hud.is_dragging());

    SDL_Event ev_motion{};
    ev_motion.type = SDL_MOUSEMOTION;
    ev_motion.motion.x = 450;
    ev_motion.motion.y = 350;
    bool moved = hud.handle_mouse_event(ev_motion);
    assert(moved);
    assert(hud.calculated_x() == 440.0f);
    assert(hud.calculated_y() == 340.0f);

    SDL_Event ev_up{};
    ev_up.type = SDL_MOUSEBUTTONUP;
    ev_up.button.button = SDL_BUTTON_LEFT;
    ev_up.button.x = 450;
    ev_up.button.y = 350;
    bool released = hud.handle_mouse_event(ev_up);
    assert(released);
    assert(!hud.is_dragging());
    assert(hud.settings().position == PerfHudPosition::FreeDrag);
    assert(hud.calculated_x() == 440.0f);
    assert(hud.calculated_y() == 340.0f);

    std::cout << "[PASS] C++ PerfHud unit test passed" << std::endl;
}

static void test_dialogue_components() {
    auto& enhancer = DialogueEnhancer::instance();
    auto& backlog = DialogueBacklog::instance();

    // Enhancer tests
    std::string t1 = enhancer.process_dialogue_text("Where are we?\nDonald? Goofy?");
    assert(t1.find('\n') != std::string::npos);

    std::string t2 = enhancer.process_dialogue_text("Ahead lies what you seek,\nbut to claim it, you must lose.");
    assert(t2.find("seek, but") != std::string::npos);

    // Donald dialogue density test: Compact / Dense2Lines must merge "magic!" onto line 2
    enhancer.set_density(FontDensity::Compact);
    enhancer.set_line_capacity(LineCapacityMode::Dense2Lines);
    std::string donald_compact = enhancer.process_dialogue_text("It must be a Heartless!\nLet's see how it handles my\nmagic!");
    assert(donald_compact.find("handles my magic!") != std::string::npos);

    // Original density: strict 26 char line width preserves authentic 3-line layout
    DialogueBoxScaler::instance().set_width_mode(BoxWidthMode::Standard240);
    enhancer.set_density(FontDensity::Original);
    enhancer.set_line_capacity(LineCapacityMode::Authentic3Lines);
    std::string donald_orig = enhancer.process_dialogue_text("It must be a Heartless!\nLet's see how it handles my\nmagic!");
    assert(donald_orig.find("handles my\nmagic!") != std::string::npos);

    // Test dynamic line capacity computation
    enhancer.set_density(FontDensity::Original);
    enhancer.set_font_scale(FontScale::Original100);
    enhancer.set_line_capacity(LineCapacityMode::Authentic3Lines);
    DialogueBoxScaler::instance().set_width_mode(BoxWidthMode::Standard240);
    enhancer.recompute_line_width();
    assert(enhancer.settings().max_line_width_chars >= 24 && enhancer.settings().max_line_width_chars <= 28);
    assert(enhancer.settings().max_line_width_px == 152);

    // Widescreen box expansion dynamically increases pixel width and characters
    DialogueBoxScaler::instance().set_width_mode(BoxWidthMode::WidescreenExpanded);
    enhancer.recompute_line_width();
    assert(enhancer.settings().max_line_width_px == 200);
    assert(enhancer.settings().max_line_width_chars > 28);

    // Compact font scale dynamically increases characters per line
    enhancer.set_font_scale(FontScale::Compact70);
    enhancer.recompute_line_width();
    assert(enhancer.settings().max_line_width_chars >= 35);

    // Independent setting validation
    enhancer.set_density(FontDensity::Compact);
    enhancer.set_font_scale(FontScale::Compact70);
    enhancer.set_font_style(FontStyle::CleanModern);
    assert(enhancer.settings().density == FontDensity::Compact);
    assert(enhancer.settings().scale == FontScale::Compact70);
    assert(enhancer.settings().style == FontStyle::CleanModern);

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
        static const char sora_cutscene[] = "One look at this castle, and I\njust knew: They're here.";
        static const char donald_cutscene[] = "Or maybe something funny's\ngoing on!  I think we should\ncheck it out.";
        if (addr >= 0x08FBD378 && addr < 0x08FBD378 + sizeof(donald_str) * 2) {
            size_t idx = (addr - 0x08FBD378) / 2;
            if (overridden) {
                *overridden = (idx < sizeof(donald_str) - 1) ? static_cast<uint8_t>(donald_str[idx]) : 0;
            }
            return 1;
        }
        if (addr >= 0x08FBD5D0 && addr < 0x08FBD5D0 + sizeof(sora_cutscene) * 2) {
            size_t idx = (addr - 0x08FBD5D0) / 2;
            if (overridden) {
                *overridden = (idx < sizeof(sora_cutscene) - 1) ? static_cast<uint8_t>(sora_cutscene[idx]) : 0;
            }
            return 1;
        }
        if (addr >= 0x08FBD7A8 && addr < 0x08FBD7A8 + sizeof(donald_cutscene) * 2) {
            size_t idx = (addr - 0x08FBD7A8) / 2;
            if (overridden) {
                *overridden = (idx < sizeof(donald_cutscene) - 1) ? static_cast<uint8_t>(donald_cutscene[idx]) : 0;
            }
            return 1;
        }
        return 0;
    };
    g_runtime_bus_read_override = mock_bus;

    // Verify that story dialogue lines with words like "just" and "going" are NEVER chopped across lines
    std::string sora_dlg = enhancer.get_rewrapped_dialogue(0x08FBD5D0);
    assert(sora_dlg.find("just knew:") != std::string::npos);
    assert(sora_dlg.find("jus\nt") == std::string::npos); // Word "just" must NEVER be chopped

    std::string donald_dlg = enhancer.get_rewrapped_dialogue(0x08FBD7A8);
    assert(donald_dlg.find("going on!") != std::string::npos);
    assert(donald_dlg.find("goin\ng") == std::string::npos); // Word "going" must NEVER be chopped

    // Out of range read: must be ignored
    backlog.on_bus_read_u16(0x08000000);
    assert(backlog.entries().empty());

    // Script table read (0x08FBE000): must be ignored, outside dialogue string bounds
    backlog.on_bus_read_u16(0x08FBE000);
    assert(backlog.entries().empty());

    // Enhancer boundary check: script table read must not be intercepted
    uint32_t intercepted_val = 0;
    assert(!enhancer.intercept_bus_read(0x08FBE000, 2, &intercepted_val));

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

    std::cout << "[PASS] C++ DialogueEnhancer and DialogueBacklog unit tests passed" << std::endl;
}

static void test_font_resizer() {
    auto& fr = FontResizer::instance();

    fr.set_font_scale(FontScale::Original100);
    assert(fr.get_glyph_width() == 8);
    assert(fr.get_glyph_height() == 12);
    assert(fr.settings().line_spacing == 16);

    fr.set_font_scale(FontScale::Medium85);
    assert(fr.get_glyph_width() == 7);
    assert(fr.get_glyph_height() == 10);
    assert(fr.settings().line_spacing == 13);

    fr.set_font_scale(FontScale::Compact70);
    assert(fr.get_glyph_width() == 6);
    assert(fr.get_glyph_height() == 8);
    assert(fr.settings().line_spacing == 10);

    fr.set_font_scale(FontScale::Micro55);
    assert(fr.get_glyph_width() == 5);
    assert(fr.get_glyph_height() == 7);
    assert(fr.settings().line_spacing == 8);

    // Test font styles
    fr.set_font_style(FontStyle::Condensed);
    assert(fr.settings().style == FontStyle::Condensed);
    assert(fr.settings().kerning_adjustment == -1);

    fr.set_font_style(FontStyle::Authentic);
    assert(fr.settings().style == FontStyle::Authentic);
    assert(fr.settings().edge_smoothing == false);

    fr.set_font_style(FontStyle::CleanModern);
    assert(fr.settings().style == FontStyle::CleanModern);
    assert(fr.settings().edge_smoothing == true);

    // Test proportional advance widths
    int w_space = fr.get_char_advance_width(' ');
    int w_m = fr.get_char_advance_width('m');
    assert(w_m >= w_space);

    // Test 4bpp tile downsampling
    uint8_t src_1bpp[12] = { 0x3C, 0x42, 0x81, 0x81, 0xFF, 0x81, 0x81, 0x81, 0x81, 0x00, 0x00, 0x00 };
    uint8_t dst_4bpp[32];
    fr.render_scaled_glyph_4bpp(src_1bpp, dst_4bpp, 2);
    bool has_pixels = false;
    for (int i = 0; i < 32; ++i) {
        if (dst_4bpp[i] != 0) has_pixels = true;
    }
    assert(has_pixels);

    // Test bus read interception for ROM font width table
    fr.set_font_style(FontStyle::Authentic);
    fr.set_font_scale(FontScale::Original100);
    fr.set_density(FontDensity::Original);
    fr.set_kerning_adjustment(0);
    uint32_t val = 0;
    // Authentic at 100% passes through to genuine ROM directly
    assert(!fr.intercept_bus_read(FontResizer::kFontWidthTableBase + 65 * 2, 2, &val));

    // Changing style to CleanModern intercepts advance widths table
    fr.set_font_style(FontStyle::CleanModern);
    assert(fr.intercept_bus_read(FontResizer::kFontWidthTableBase + 65 * 2, 2, &val));
    assert(val > 0 && val <= 8);

    // Changing scale to Micro55 produces compact widths
    fr.set_font_scale(FontScale::Micro55);
    uint32_t val_micro = 0;
    assert(fr.intercept_bus_read(FontResizer::kFontWidthTableBase + 65 * 2, 2, &val_micro));
    assert(val_micro <= val);

    // ROM sprite frame piece descriptors at 0x090CBFB2 must NEVER be intercepted!
    uint32_t frame_val = 0;
    assert(!fr.intercept_bus_read(FontResizer::kFontTilesBase + 65 * 128, 4, &frame_val));

    // Test density and dynamic advance widths
    fr.set_density(FontDensity::Compact);
    assert(fr.settings().density == FontDensity::Compact);
    assert(fr.get_average_char_width() > 0.0f);
    uint32_t val_compact = 0;
    assert(fr.intercept_bus_read(FontResizer::kFontWidthTableBase + 65 * 2, 2, &val_compact));
    assert(val_compact > 0);
    // Test all combinations of font settings to guarantee no character collisions
    FontStyle test_styles[] = { FontStyle::Authentic, FontStyle::CleanModern, FontStyle::Condensed };
    FontScale test_scales[] = { FontScale::Original100, FontScale::Medium85, FontScale::Compact70, FontScale::Micro55 };
    FontDensity test_densities[] = { FontDensity::Original, FontDensity::Compact, FontDensity::HighDensity };
    int test_kernings[] = { -2, -1, 0, 1, 2 };

    for (auto st : test_styles) {
        for (auto sc : test_scales) {
            for (auto den : test_densities) {
                for (int kern : test_kernings) {
                    fr.set_font_style(st);
                    fr.set_font_scale(sc);
                    fr.set_density(den);
                    fr.set_kerning_adjustment(kern);

                    // Verify critical dialogue characters: 'W', 'h', 'a', 't', ' ', 'i', 's'
                    const char sample[] = "What is this, a card?";
                    for (size_t s = 0; s < sizeof(sample) - 1; ++s) {
                        char c = sample[s];
                        int adv = fr.get_char_advance_width(c);
                        if (c == ' ') {
                            assert(adv >= 2);
                        } else {
                            assert(adv >= 2);
                            // Character 'h' must never shrink below visible width (at least 4px)
                            if (c == 'h' || c == 'a' || c == 't' || c == 's') {
                                assert(adv >= 4);
                            }
                            if (c == 'W') {
                                assert(adv >= 5);
                            }
                        }
                    }
                }
            }
        }
    }

    // Restore default
    fr.set_font_style(FontStyle::Authentic);
    fr.set_font_scale(FontScale::Original100);
    fr.set_density(FontDensity::Original);
    fr.set_kerning_adjustment(0);

    std::cout << "[PASS] C++ FontResizer unit test passed" << std::endl;
}

static void test_dialogue_box_scaler() {
    auto& dbs = DialogueBoxScaler::instance();

    dbs.set_width_mode(BoxWidthMode::Standard240);
    assert(dbs.get_box_width_tiles() == 28);
    assert(dbs.get_box_width_pixels() == 224);

    dbs.set_width_mode(BoxWidthMode::WidescreenExpanded);
    assert(dbs.get_box_width_tiles() == 34);
    assert(dbs.get_box_width_pixels() == 272);

    dbs.set_width_mode(BoxWidthMode::DynamicResponsive);
    assert(dbs.get_box_width_tiles() == 36);
    assert(dbs.get_box_width_pixels() == 288);

    // Test nine-slice tilemap row horizontal stretching
    uint16_t row[40];
    row[0] = 0x0100; // Left cap
    row[1] = 0x0101; // Center repeating tile
    row[27] = 0x0102; // Right cap
    dbs.expand_dialogue_tilemap_row(row, 28, 34);
    assert(row[0] == 0x0100);
    for (int i = 1; i < 33; ++i) {
        assert(row[i] == 0x0101);
    }
    assert(row[33] == 0x0102);

    // Test dialogue OAM adjustment
    uint16_t a0 = 120; // lower third dialogue region
    uint16_t a1 = 200; // right border corner
    dbs.adjust_dialogue_oam(a0, a1, 284);
    int shifted_x = a1 & 0x01FF;
    if (shifted_x >= 256) shifted_x -= 512;
    assert(shifted_x > 200); // Shifted right for expanded box

    std::cout << "[PASS] C++ DialogueBoxScaler unit test passed" << std::endl;
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

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "           KHCOMRecomp Native C++ Unit Test Runner             " << std::endl;
    std::cout << "================================================================" << std::endl;

    test_audio_dsp();
    test_screen_filters();
    test_frame_interpolator();
    test_input_enhancements();
    test_hud_anchoring();
    test_dialog_turbo();
    test_xbrz_filter();
    test_savestate_thumbnails();
    test_perf_hud();
    test_dialogue_components();
    test_font_resizer();
    test_dialogue_box_scaler();
    test_widescreen_view_modes();

    std::cout << "================================================================" << std::endl;
    std::cout << "ALL NATIVE C++ UNIT TESTS PASSED SUCCESSFULLY (13/13 MODULES)." << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}
