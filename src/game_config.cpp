#include "game_config.h"
#include "audio_dsp.h"
#include "perf_hud.h"
#include "frame_interpolator.h"
#include "screen_filters.h"
#include "input_enhancements.h"
#include "hd_audio_player.h"
#include "xbrz_filter.h"
#include "dialog_turbo.h"
#include "hud_anchoring.h"
#include "dialogue_enhancer.h"
#include "dialogue_backlog.h"
#include "widescreen_adapter.h"
#include <cstring>
#include <fstream>
#include <string>

#if defined(GBARECOMP_RUNTIME_UI)
#include "recomp_runtime_ui.h"
#endif

namespace khcom {

namespace {

const char* const kAspectLabels[] = {
    "3:2 (Classic 240px)",
    "16:10 (Wide 256px)",
    "16:9 (Standard 284px)",
    "16:9 (High-Density 480px)",
    "16:9 (Full Arena 576px)"
};

const std::uint16_t kAspectWidths[] = {
    240,
    256,
    284,
    480,
    576
};

#if defined(GBARECOMP_RUNTIME_UI)
const char* const kFpsTargetChoices[] = {
    "60 FPS (Original GBA)",
    "120 FPS (2x High Rate)",
    "144 FPS",
    "Display Native (Auto)",
    "Uncapped"
};

const char* const kMotionSmoothingChoices[] = {
    "Off (Direct Presentation)",
    "Smooth Blend (50% Lerp)",
    "Motion-Adaptive"
};

const char* const kXbrzChoices[] = {
    "Off (Original Pixels)",
    "2x xBRZ (Enhanced Edges)",
    "3x xBRZ (Crisp Vectors)",
    "4x xBRZ (Ultra Detail)",
    "5x xBRZ (Retina Smoothing)"
};

const char* const kColorProfileChoices[] = {
    "Raw (Uncorrected)",
    "AGB-001 (Reflective TFT)",
    "AGS-001 (SP Frontlit)",
    "AGS-101 (SP Backlit)",
    "MiSTer Gamma 1.6",
    "MiSTer Gamma 2.2"
};

const char* const kScreenMaskChoices[] = {
    "Off (Clean Pixels)",
    "MiSTer LCD Grid (TFT Matrix)",
    "MiSTer Subpixel RGB (Vertical)",
    "MiSTer Subpixel BGR",
    "MiSTer Diffusion (AGS-001)",
    "Game Boy Player Scanlines",
    "CRT Trinitron Aperture Grille"
};

const char* const kAnalogChoices[] = {
    "Digital (Original 8-Way)",
    "Analog 8-Way (Smooth Stick)",
    "True 360 Walk & Run"
};

const char* const kHudAnchorChoices[] = {
    "Original Centered (GBA 3:2)",
    "Widescreen Anchored (Corners)"
};

const char* const kFontDensityChoices[] = {
    "Original (100% GBA Spacing)",
    "Compact (80% Spacing)",
    "High-Density (65% Spacing)"
};

const char* const kEqChoices[] = {
    "Flat (Authentic)",
    "Warm Retro",
    "Crisp Modern",
    "Bass Boost"
};

const char* const kPerfModeChoices[] = {
    "Disabled",
    "FPS Counter Only",
    "FPS + Frametime ms",
    "Full HUD + Frametime Graph"
};

const char* const kPerfPositionChoices[] = {
    "Top-Left",
    "Top-Right",
    "Bottom-Left",
    "Bottom-Right",
    "Bottom Black Bar (Letterbox)",
    "Custom (Drag with Mouse)"
};

const char* const kPerfThemeChoices[] = {
    "Glassmorphism Dark",
    "Neon Cyberpunk",
    "Solid Dark",
    "Minimal (Transparent)"
};

const RecompRuntimeUiItem kExtraItems[] = {
    {
        "video.fps_target",
        "Graphics",
        "Target Framerate",
        "High refresh rate presentation without altering gameplay speed",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 4, 1,
        kFpsTargetChoices, 5, nullptr
    },
    {
        "video.motion_smoothing",
        "Graphics",
        "Motion Smoothing",
        "GPU temporal blending between simulation frames for high-Hz displays",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 2, 1,
        kMotionSmoothingChoices, 3, nullptr
    },
    {
        "video.xbrz_scale",
        "Graphics",
        "xBRZ Pixel Scaler",
        "High-precision geometric edge upscaler (2x to 5x)",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 4, 1,
        kXbrzChoices, 5, nullptr
    },
    {
        "video.color_profile",
        "Graphics",
        "Color Profile / Gamma",
        "Hardware color grading and gamma correction curves",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 5, 1,
        kColorProfileChoices, 6, nullptr
    },
    {
        "video.screen_mask",
        "Graphics",
        "Screen Mask Type",
        "LCD pixel grid, RGB subpixel stripes, or CRT scanlines from MiSTer FPGA",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 6, 1,
        kScreenMaskChoices, 7, nullptr
    },
    {
        "video.mask_intensity",
        "Graphics",
        "Mask Intensity (%)",
        "Grid and scanline darkness: 10% (subtle) to 100% (pronounced)",
        RECOMP_RUNTIME_UI_INT,
        10, 100, 5,
        nullptr, 0, nullptr
    },
    {
        "video.hud_anchoring",
        "Display",
        "HUD Dynamic Anchoring",
        "Shift health bar to top-left and card deck to bottom-right in widescreen",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 1, 1,
        kHudAnchorChoices, 2, nullptr
    },
    {
        "input.analog_mode",
        "Controls",
        "Analog Movement Mode",
        "Map controller stick to true 360 walk/run or 8-way digital",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 2, 1,
        kAnalogChoices, 3, nullptr
    },
    {
        "gameplay.turbo_dialog",
        "Gameplay & Assist",
        "Turbo Dialog & Cutscene Skip",
        "Hold Tab or Gamepad Y to auto-advance dialogue boxes at 60Hz",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "dialogue.font_density",
        "Dialogue & Story",
        "Font Density",
        "Compact text spacing allows more words per line without altering typewriter speed",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 2, 1,
        kFontDensityChoices, 3, nullptr
    },
    {
        "dialogue.soft_word_wrap",
        "Dialogue & Story",
        "Grammar-Aware Word-Wrap",
        "Continuously joins mid-sentence lines using case and punctuation heuristics",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "dialogue.backlog_enable",
        "Dialogue & Story",
        "Conversation Log (Backlog)",
        "Enable translucent story history sidebar (press L, F2, or Gamepad Back)",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "audio.hd_enabled",
        "Audio",
        "HD Re:CoM Soundtrack",
        "Replace GBA chiptunes with high-fidelity PS2 Re:CoM tracks",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "audio.bgm_volume",
        "Audio",
        "Music (BGM) Volume (%)",
        "Independent BGM volume for HD soundtrack (0% to 150%)",
        RECOMP_RUNTIME_UI_INT,
        0, 150, 5,
        nullptr, 0, nullptr
    },
    {
        "audio.sfx_volume",
        "Audio",
        "Effects (SFX) Volume (%)",
        "Independent sound effects and combat audio volume (0% to 150%)",
        RECOMP_RUNTIME_UI_INT,
        0, 150, 5,
        nullptr, 0, nullptr
    },
    {
        "perf.mode",
        "Diagnostics & HUD",
        "Display Mode",
        "Real-time FPS and frametime tracking (or press F10 to cycle)",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 3, 1,
        kPerfModeChoices, 4, nullptr
    },
    {
        "perf.position",
        "Diagnostics & HUD",
        "Screen Position",
        "Anchor corner, letterbox black bar docking, or free mouse drag",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 5, 1,
        kPerfPositionChoices, 6, nullptr
    },
    {
        "perf.theme",
        "Diagnostics & HUD",
        "Visual Theme",
        "Color palette and background transparency styling",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 3, 1,
        kPerfThemeChoices, 4, nullptr
    },
    {
        "audio.eq_preset",
        "Audio",
        "Equalizer Profile",
        "Real-time DSP equalization profile",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 3, 1,
        kEqChoices, 4, nullptr
    },
    {
        "audio.stereo_width",
        "Audio",
        "Stereo Width (%)",
        "Stereo field width: 0% mono to 200% expanded stereo",
        RECOMP_RUNTIME_UI_INT,
        0, 200, 10,
        nullptr, 0, nullptr
    },
    {
        "audio.anti_aliasing",
        "Audio",
        "DAC Anti-Aliasing",
        "Biquad low-pass filter to remove GBA DAC high-frequency hiss",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "audio.limiter",
        "Audio",
        "Dynamic Peak Limiter",
        "Soft-knee limiter to prevent audio clipping on multi-card hits",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    }
};

int ui_get_callback(const char* key, int* value_out) {
    if (!key || !value_out) return 0;
    auto& dsp = AudioDsp::instance();
    auto& hud = PerfHud::instance();
    auto& interp = FrameInterpolator::instance();
    auto& filters = ScreenFilters::instance();
    auto& xbrz = XbrzUpscaler::instance();
    auto& input = InputEnhancements::instance();
    auto& hd_audio = HdAudioPlayer::instance();
    auto& turbo = DialogTurbo::instance();
    auto& anchoring = HudAnchoring::instance();
    auto& enhancer = DialogueEnhancer::instance();
    auto& backlog = DialogueBacklog::instance();

    if (std::strcmp(key, "dialogue.font_density") == 0) {
        *value_out = static_cast<int>(enhancer.settings().density);
        return 1;
    }
    if (std::strcmp(key, "dialogue.soft_word_wrap") == 0) {
        *value_out = enhancer.settings().soft_word_wrap ? 1 : 0;
        return 1;
    }
    if (std::strcmp(key, "dialogue.backlog_enable") == 0) {
        *value_out = backlog.is_enabled() ? 1 : 0;
        return 1;
    }

    if (std::strcmp(key, "video.fps_target") == 0) {
        *value_out = static_cast<int>(interp.settings().mode);
        return 1;
    }
    if (std::strcmp(key, "video.motion_smoothing") == 0) {
        *value_out = static_cast<int>(interp.settings().smoothing);
        return 1;
    }
    if (std::strcmp(key, "video.xbrz_scale") == 0) {
        int idx = (xbrz.scale() == XbrzScale::None) ? 0 : (static_cast<int>(xbrz.scale()) - 1);
        *value_out = idx;
        return 1;
    }
    if (std::strcmp(key, "video.color_profile") == 0) {
        *value_out = static_cast<int>(filters.settings().color_profile);
        return 1;
    }
    if (std::strcmp(key, "video.screen_mask") == 0) {
        *value_out = static_cast<int>(filters.settings().mask_type);
        return 1;
    }
    if (std::strcmp(key, "video.mask_intensity") == 0) {
        *value_out = static_cast<int>(filters.settings().mask_intensity * 100.0f + 0.5f);
        return 1;
    }
    if (std::strcmp(key, "video.hud_anchoring") == 0) {
        *value_out = static_cast<int>(anchoring.settings().mode);
        return 1;
    }
    if (std::strcmp(key, "input.analog_mode") == 0) {
        *value_out = static_cast<int>(input.settings().analog_mode);
        return 1;
    }
    if (std::strcmp(key, "gameplay.turbo_dialog") == 0) {
        *value_out = turbo.is_enabled() ? 1 : 0;
        return 1;
    }
    if (std::strcmp(key, "audio.hd_enabled") == 0) {
        *value_out = hd_audio.settings().enabled ? 1 : 0;
        return 1;
    }
    if (std::strcmp(key, "audio.bgm_volume") == 0) {
        *value_out = static_cast<int>(hd_audio.settings().bgm_volume * 100.0f + 0.5f);
        return 1;
    }
    if (std::strcmp(key, "audio.sfx_volume") == 0) {
        *value_out = static_cast<int>(hd_audio.settings().sfx_volume * 100.0f + 0.5f);
        return 1;
    }
    if (std::strcmp(key, "perf.mode") == 0) {
        *value_out = static_cast<int>(hud.settings().mode);
        return 1;
    }
    if (std::strcmp(key, "perf.position") == 0) {
        *value_out = static_cast<int>(hud.settings().position);
        return 1;
    }
    if (std::strcmp(key, "perf.theme") == 0) {
        *value_out = static_cast<int>(hud.settings().theme);
        return 1;
    }
    if (std::strcmp(key, "audio.eq_preset") == 0) {
        *value_out = static_cast<int>(dsp.settings().preset);
        return 1;
    }
    if (std::strcmp(key, "audio.stereo_width") == 0) {
        *value_out = static_cast<int>(dsp.settings().stereo_width * 100.0f + 0.5f);
        return 1;
    }
    if (std::strcmp(key, "audio.anti_aliasing") == 0) {
        *value_out = dsp.settings().anti_aliasing_filter ? 1 : 0;
        return 1;
    }
    if (std::strcmp(key, "audio.limiter") == 0) {
        *value_out = dsp.settings().limiter_enabled ? 1 : 0;
        return 1;
    }
    return 0;
}

static bool s_is_loading_config = false;

void save_khcom_config() {
    std::ofstream file("khcom_config.ini");
    if (!file.is_open()) return;

    auto& dsp = AudioDsp::instance();
    auto& hud = PerfHud::instance();
    auto& interp = FrameInterpolator::instance();
    auto& filters = ScreenFilters::instance();
    auto& xbrz = XbrzUpscaler::instance();
    auto& input = InputEnhancements::instance();
    auto& hd_audio = HdAudioPlayer::instance();
    auto& turbo = DialogTurbo::instance();
    auto& anchoring = HudAnchoring::instance();
    auto& enhancer = DialogueEnhancer::instance();
    auto& backlog = DialogueBacklog::instance();

    file << "[Video]\n";
    file << "fps_target=" << static_cast<int>(interp.settings().mode) << "\n";
    file << "motion_smoothing=" << static_cast<int>(interp.settings().smoothing) << "\n";
    int xbrz_idx = (xbrz.scale() == XbrzScale::None) ? 0 : (static_cast<int>(xbrz.scale()) - 1);
    file << "xbrz_scale=" << xbrz_idx << "\n";
    file << "color_profile=" << static_cast<int>(filters.settings().color_profile) << "\n";
    file << "screen_mask=" << static_cast<int>(filters.settings().mask_type) << "\n";
    file << "mask_intensity=" << static_cast<int>(filters.settings().mask_intensity * 100.0f + 0.5f) << "\n";
    file << "hud_anchoring=" << static_cast<int>(anchoring.settings().mode) << "\n\n";

    file << "[Audio]\n";
    file << "hd_enabled=" << (hd_audio.settings().enabled ? 1 : 0) << "\n";
    file << "bgm_volume=" << static_cast<int>(hd_audio.settings().bgm_volume * 100.0f + 0.5f) << "\n";
    file << "sfx_volume=" << static_cast<int>(hd_audio.settings().sfx_volume * 100.0f + 0.5f) << "\n";
    file << "eq_preset=" << static_cast<int>(dsp.settings().preset) << "\n";
    file << "stereo_width=" << static_cast<int>(dsp.settings().stereo_width * 100.0f + 0.5f) << "\n";
    file << "anti_aliasing=" << (dsp.settings().anti_aliasing_filter ? 1 : 0) << "\n";
    file << "limiter=" << (dsp.settings().limiter_enabled ? 1 : 0) << "\n\n";

    file << "[Dialogue]\n";
    file << "font_density=" << static_cast<int>(enhancer.settings().density) << "\n";
    file << "soft_word_wrap=" << (enhancer.settings().soft_word_wrap ? 1 : 0) << "\n";
    file << "backlog_enable=" << (backlog.is_enabled() ? 1 : 0) << "\n\n";

    file << "[Controls]\n";
    file << "analog_mode=" << static_cast<int>(input.settings().analog_mode) << "\n\n";

    file << "[Gameplay]\n";
    file << "turbo_dialog=" << (turbo.is_enabled() ? 1 : 0) << "\n\n";

    file << "[Diagnostics]\n";
    file << "perf_mode=" << static_cast<int>(hud.settings().mode) << "\n";
    file << "perf_position=" << static_cast<int>(hud.settings().position) << "\n";
    file << "perf_theme=" << static_cast<int>(hud.settings().theme) << "\n";
}

int ui_set_callback(const char* key, int value) {
    if (!key) return 0;
    auto& dsp = AudioDsp::instance();
    auto& hud = PerfHud::instance();
    auto& interp = FrameInterpolator::instance();
    auto& filters = ScreenFilters::instance();
    auto& xbrz = XbrzUpscaler::instance();
    auto& input = InputEnhancements::instance();
    auto& hd_audio = HdAudioPlayer::instance();
    auto& turbo = DialogTurbo::instance();
    auto& anchoring = HudAnchoring::instance();
    auto& enhancer = DialogueEnhancer::instance();
    auto& backlog = DialogueBacklog::instance();

    bool handled = false;
    if (std::strcmp(key, "dialogue.font_density") == 0) {
        enhancer.set_density(static_cast<FontDensity>(value));
        handled = true;
    } else if (std::strcmp(key, "dialogue.soft_word_wrap") == 0) {
        enhancer.set_soft_word_wrap(value != 0);
        handled = true;
    } else if (std::strcmp(key, "dialogue.backlog_enable") == 0) {
        backlog.set_enabled(value != 0);
        handled = true;
    } else if (std::strcmp(key, "video.fps_target") == 0) {
        interp.set_mode(static_cast<FramerateMode>(value));
        handled = true;
    } else if (std::strcmp(key, "video.motion_smoothing") == 0) {
        interp.set_smoothing(static_cast<MotionSmoothingMode>(value));
        handled = true;
    } else if (std::strcmp(key, "video.xbrz_scale") == 0) {
        XbrzScale scale = (value <= 0) ? XbrzScale::None : static_cast<XbrzScale>(value + 1);
        xbrz.set_scale(scale);
        handled = true;
    } else if (std::strcmp(key, "video.color_profile") == 0) {
        filters.set_color_profile(static_cast<ColorProfile>(value));
        handled = true;
    } else if (std::strcmp(key, "video.screen_mask") == 0) {
        filters.set_mask_type(static_cast<ScreenMaskType>(value));
        handled = true;
    } else if (std::strcmp(key, "video.mask_intensity") == 0) {
        filters.set_mask_intensity(static_cast<float>(value) / 100.0f);
        handled = true;
    } else if (std::strcmp(key, "video.hud_anchoring") == 0) {
        anchoring.set_mode(static_cast<HudAnchorMode>(value));
        handled = true;
    } else if (std::strcmp(key, "input.analog_mode") == 0) {
        input.set_analog_mode(static_cast<AnalogMode>(value));
        handled = true;
    } else if (std::strcmp(key, "gameplay.turbo_dialog") == 0) {
        turbo.set_enabled(value != 0);
        handled = true;
    } else if (std::strcmp(key, "audio.hd_enabled") == 0) {
        hd_audio.set_enabled(value != 0);
        handled = true;
    } else if (std::strcmp(key, "audio.bgm_volume") == 0) {
        hd_audio.set_bgm_volume(static_cast<float>(value) / 100.0f);
        handled = true;
    } else if (std::strcmp(key, "audio.sfx_volume") == 0) {
        hd_audio.set_sfx_volume(static_cast<float>(value) / 100.0f);
        handled = true;
    } else if (std::strcmp(key, "perf.mode") == 0) {
        hud.set_mode(static_cast<PerfHudMode>(value));
        handled = true;
    } else if (std::strcmp(key, "perf.position") == 0) {
        hud.set_position(static_cast<PerfHudPosition>(value));
        handled = true;
    } else if (std::strcmp(key, "perf.theme") == 0) {
        hud.set_theme(static_cast<PerfHudTheme>(value));
        handled = true;
    } else if (std::strcmp(key, "audio.eq_preset") == 0) {
        dsp.apply_preset(static_cast<EqPreset>(value));
        handled = true;
    } else if (std::strcmp(key, "audio.stereo_width") == 0) {
        dsp.set_stereo_width(static_cast<float>(value) / 100.0f);
        handled = true;
    } else if (std::strcmp(key, "audio.anti_aliasing") == 0) {
        dsp.set_anti_aliasing(value != 0);
        handled = true;
    } else if (std::strcmp(key, "audio.limiter") == 0) {
        dsp.set_limiter(value != 0);
        handled = true;
    }

    if (handled) {
        if (!s_is_loading_config) {
            save_khcom_config();
        }
        return 1;
    }
    return 0;
}

void load_khcom_config() {
    std::ifstream file("khcom_config.ini");
    if (!file.is_open()) return;

    s_is_loading_config = true;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '[' || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        std::string val_str = line.substr(eq + 1);
        int val = 0;
        try {
            val = std::stoi(val_str);
        } catch (...) {
            continue;
        }

        if (key == "fps_target") ui_set_callback("video.fps_target", val);
        else if (key == "motion_smoothing") ui_set_callback("video.motion_smoothing", val);
        else if (key == "xbrz_scale") ui_set_callback("video.xbrz_scale", val);
        else if (key == "color_profile") ui_set_callback("video.color_profile", val);
        else if (key == "screen_mask") ui_set_callback("video.screen_mask", val);
        else if (key == "mask_intensity") ui_set_callback("video.mask_intensity", val);
        else if (key == "hud_anchoring") ui_set_callback("video.hud_anchoring", val);
        else if (key == "hd_enabled") ui_set_callback("audio.hd_enabled", val);
        else if (key == "bgm_volume") ui_set_callback("audio.bgm_volume", val);
        else if (key == "sfx_volume") ui_set_callback("audio.sfx_volume", val);
        else if (key == "eq_preset") ui_set_callback("audio.eq_preset", val);
        else if (key == "stereo_width") ui_set_callback("audio.stereo_width", val);
        else if (key == "anti_aliasing") ui_set_callback("audio.anti_aliasing", val);
        else if (key == "limiter") ui_set_callback("audio.limiter", val);
        else if (key == "font_density") ui_set_callback("dialogue.font_density", val);
        else if (key == "soft_word_wrap") ui_set_callback("dialogue.soft_word_wrap", val);
        else if (key == "backlog_enable") ui_set_callback("dialogue.backlog_enable", val);
        else if (key == "analog_mode") ui_set_callback("input.analog_mode", val);
        else if (key == "turbo_dialog") ui_set_callback("gameplay.turbo_dialog", val);
        else if (key == "perf_mode") ui_set_callback("perf.mode", val);
        else if (key == "perf_position") ui_set_callback("perf.position", val);
        else if (key == "perf_theme") ui_set_callback("perf.theme", val);
    }
    s_is_loading_config = false;
}
#endif

} // namespace

gbarecomp::RunOptions create_run_options() {
    gbarecomp::RunOptions opts;
    opts.builtin_game_name = GAME_TITLE.data();
    opts.builtin_rom_sha1 = ROM_SHA1_USA.data();
    opts.launcher_region = "USA";

    // Assist tools: save states, fast-forward, and rewind
    opts.expose_assist_tools = true;
    opts.assist_tools_enabled_by_default = true;
    opts.assist_fast_forward_multiplier_default = 4;
    opts.save_state_slot_count = 10;
    opts.rewind_history_seconds = 60;
    opts.rewind_capture_interval_frames = 15;

    // Video display and real-time viewport expansion
    opts.freely_resizable_window = true;
    opts.resize_driven_view = true;
    opts.max_view_width = 576;
    opts.max_resize_view_width = 576;
    opts.max_resize_view_height = 324;
    opts.launcher_expose_widescreen = true;
    opts.launcher_expose_adaptive_view = true;
    opts.widescreen_view_width = 284;
    opts.launcher_aspect_labels = kAspectLabels;
    opts.launcher_aspect_view_widths = kAspectWidths;
    opts.launcher_num_aspects = sizeof(kAspectWidths) / sizeof(kAspectWidths[0]);
    opts.extended_view_init = &khcom_install_widescreen_adapter;

#if defined(GBARECOMP_RUNTIME_UI)
    opts.ui_extra_items = kExtraItems;
    opts.ui_extra_item_count = sizeof(kExtraItems) / sizeof(kExtraItems[0]);
    opts.ui_get = ui_get_callback;
    opts.ui_set = ui_set_callback;

    // Restore saved user configurations across game sessions
    load_khcom_config();
#endif

    return opts;
}

} // namespace khcom
