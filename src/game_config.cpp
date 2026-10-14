#include "game_config.h"
#include "gbaimp_integration.h"
#include "gbaimp/audio_dsp.h"
#include "gbaimp/perf_hud.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/screen_filters.h"
#include "gbaimp/input_enhancements.h"
#include "gbaimp/hd_audio_player.h"
#include "gbaimp/pixel_art_upscaler.h"
#include "gbaimp/dialog_turbo.h"
#include "gbaimp/hud_anchoring.h"
#include "text_overlay/overlay_types.h"
#include "dialogue_backlog.h"
#include "widescreen_adapter.h"
#include "capture_harness.h"
#include "runtime_bus_bridge.h"
#include "gba/gba_bus.h"
#include "gba/gba_io.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#if defined(GBARECOMP_RUNTIME_UI)
#include "recomp_runtime_ui.h"
#include "runtime_ui_standard.h"
#endif

namespace khcom {

namespace {

const char* const kAspectLabels[] = {
    "3:2 (Native)",
    "16:9 (Battles only)"
};

const std::uint16_t kAspectWidths[] = {
    240,
    284
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
    "2x Pixel-Art Edges (Enhanced Edges)",
    "3x Pixel-Art Edges (Crisp Vectors)",
    "4x Pixel-Art Edges (Ultra Detail)",
    "5x Pixel-Art Edges (Retina Smoothing)"
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

const char* const kGamepadProfileChoices[] = {
    "Xbox / Standard (A: Atk, B: Jump, LB/RB: Deck)",
    "Nintendo Style (B: Atk, A: Jump, LB/RB: Deck)",
    "PlayStation Style (Square/X: Atk, Cross/A: Jump)",
    "Swapped Shoulders (RB: Deck L, LB: Deck R)",
    "Triggers for Deck (LT: Deck L, RT: Deck R)",
    "Custom Profile"
};

const char* const kPadButtonChoices[] = {
    "Button A (Bottom / Cross)",
    "Button B (Right / Circle)",
    "Button X (Left / Square)",
    "Button Y (Top / Triangle)",
    "Left Bumper (LB / L1)",
    "Right Bumper (RB / R1)",
    "Left Trigger (LT / L2)",
    "Right Trigger (RT / R2)"
};

const char* const kKeyboardPresetChoices[] = {
    "Default (X, Z, C, V, Return, RShift)",
    "WASD + J/K/U/I (Modern PC)",
    "Classic Emulation (Z, X, A, S)",
    "Arrows + Z/X/Q/W",
    "Custom"
};

const char* const kHdTextModeChoices[] = {
    "Original (GBA font)",
    "HD overlay (output resolution)"
};

// Family names are filled in at start-up from OverlayRenderer::family_names() (built-ins, then fonts/*.ttf).
constexpr std::size_t kMaxFontChoices = 8 + 8;   // built-in families + user fonts (FontLibrary::kMaxUserFonts)
const char* g_font_choice_ptrs[kMaxFontChoices] = {};
std::vector<std::string> g_font_choice_names;

const char* const kHdWrapChoices[] = {
    "Keep engine line breaks",
    "Reflow to the text box"
};

const char* const kHdStyleChoices[] = {
    "Plain",
    "Drop shadow",
    "Outline"
};

const std::uint32_t kHdTextColors[] = {0xFFFFFFFFu, 0xFFF3D6FFu, 0xFFE9A0FFu, 0xBFE3FFFFu, 0xC8F5C8FFu, 0xD8D8D8FFu, 0x202020FFu, 0u};   // 0 = match the game's own colour
const char* const kHdTextColorChoices[] = {
    "White", "Cream", "Light yellow", "Light blue", "Pale green", "Light grey", "Near black", "Match game (brown)"
};

const std::uint32_t kHdShadowColors[] = {0x000000B0u, 0x000000FFu, 0x1A1A3CE0u, 0x3A2410D0u, 0x303030C0u, 0xFFFFFF60u, 0u};
const char* const kHdShadowColorChoices[] = {
    "Black (soft)", "Black", "Navy", "Dark brown", "Dark grey", "White glow", "Match game (tan)"
};

const std::uint32_t kHdOutlineColors[] = {0x000000FFu, 0x1A1A3CFFu, 0x3A2410FFu, 0xFFFFFFFFu};
const char* const kHdOutlineColorChoices[] = {
    "Black", "Navy", "Dark brown", "White"
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

RecompRuntimeUiItem kExtraItems[] = {
    {
        "video.fps_target",
        "Graphics",
        "Target Framerate",
        "Target refresh rate without altering speed",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 4, 1,
        kFpsTargetChoices, 5, nullptr
    },
    {
        "video.motion_smoothing",
        "Graphics",
        "Motion Smoothing",
        "Temporal blending between simulation frames",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 2, 1,
        kMotionSmoothingChoices, 3, nullptr
    },
    {
        "video.xbrz_scale",
        "Graphics",
        "Pixel-Art Upscaler",
        "Edge-aware pixel-art upscaler, Scale2x/EPX style (2x to 5x)",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 4, 1,
        kXbrzChoices, 5, nullptr
    },
    {
        "video.color_profile",
        "Graphics",
        "Color Profile / Gamma",
        "Hardware color grading and gamma curve",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 5, 1,
        kColorProfileChoices, 6, nullptr
    },
    {
        "video.screen_mask",
        "Graphics",
        "Screen Mask Type",
        "LCD grid, RGB subpixel stripes, scanlines",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 6, 1,
        kScreenMaskChoices, 7, nullptr
    },
    {
        "video.mask_intensity",
        "Graphics",
        "Mask Intensity (%)",
        "Grid darkness: 10% (subtle) to 100% (dense)",
        RECOMP_RUNTIME_UI_INT,
        10, 100, 5,
        nullptr, 0, nullptr
    },
    {
        "input.gamepad_profile",
        "Controller (Gamepad)",
        "Controller Preset",
        "Quick layout profile preset",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 5, 1,
        kGamepadProfileChoices, 6, nullptr
    },
    {
        "input.pad_attack",
        "Controller (Gamepad)",
        "Attack / Card Action",
        "Play card, attack, menu confirm",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 7, 1,
        kPadButtonChoices, 8, nullptr
    },
    {
        "input.pad_jump",
        "Controller (Gamepad)",
        "Jump / Dodge Roll",
        "Jump, dodge roll, menu cancel",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 7, 1,
        kPadButtonChoices, 8, nullptr
    },
    {
        "input.pad_deck_l",
        "Controller (Gamepad)",
        "Cycle Deck Left",
        "Scroll deck cards to the left",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 7, 1,
        kPadButtonChoices, 8, nullptr
    },
    {
        "input.pad_deck_r",
        "Controller (Gamepad)",
        "Cycle Deck Right / Lock-on",
        "Scroll deck right and lock-on",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 7, 1,
        kPadButtonChoices, 8, nullptr
    },
    {
        "input.analog_mode",
        "Controller (Gamepad)",
        "Stick Movement Mode",
        "360 walk/run or 8-way digital",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 2, 1,
        kAnalogChoices, 3, nullptr
    },
    {
        "input.deadzone",
        "Controller (Gamepad)",
        "Stick Deadzone (%)",
        "Stick drift margin (5% to 50%)",
        RECOMP_RUNTIME_UI_INT,
        5, 50, 5,
        nullptr, 0, nullptr
    },
    {
        "input.reset_pad",
        "Controller (Gamepad)",
        "Reset Gamepad to Defaults",
        "Restore default gamepad mapping",
        RECOMP_RUNTIME_UI_ACTION,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.kb_preset",
        "Keyboard Controls",
        "Keyboard Preset",
        "Keyboard layout preset",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 4, 1,
        kKeyboardPresetChoices, 5, nullptr
    },
    {
        "input.key_a",
        "Keyboard Controls",
        "Attack / Card Action",
        "Play card, attack, menu confirm",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.key_b",
        "Keyboard Controls",
        "Jump / Dodge Roll",
        "Jump, dodge roll, menu cancel",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.key_l",
        "Keyboard Controls",
        "Cycle Deck Left",
        "Scroll deck cards to the left",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.key_r",
        "Keyboard Controls",
        "Cycle Deck Right / Lock-on",
        "Scroll deck right and lock-on",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.key_start",
        "Keyboard Controls",
        "Pause / Camp Menu",
        "Open journal and pause game",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.key_select",
        "Keyboard Controls",
        "Switch Deck / Reload",
        "Toggle enemy cards and reload",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.key_up",
        "Keyboard Controls",
        "Move Up",
        "Move character upwards",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.key_down",
        "Keyboard Controls",
        "Move Down",
        "Move character downwards",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.key_left",
        "Keyboard Controls",
        "Move Left",
        "Move character to the left",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.key_right",
        "Keyboard Controls",
        "Move Right",
        "Move character to the right",
        RECOMP_RUNTIME_UI_TEXT,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "input.reset_kb",
        "Keyboard Controls",
        "Reset Keyboard to Defaults",
        "Restore default keyboard mapping",
        RECOMP_RUNTIME_UI_ACTION,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "gameplay.turbo_dialog",
        "Gameplay & Assist",
        "Turbo Dialog & Cutscene Skip",
        "Hold Tab or Y to advance text at 60Hz",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "hdtext.enabled",
        "Dialogue Text (HD)",
        "Dialogue Text",
        "Original keeps the game text untouched. HD overlay draws the dialogue at output resolution",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 1, 1,
        kHdTextModeChoices, 2, nullptr
    },
    {
        "hdtext.family",
        "Dialogue Text (HD)",
        "Font Family",
        "Built-in families, then .ttf files placed in the fonts/ folder (listed at start-up)",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 7, 1,
        g_font_choice_ptrs, 8, nullptr
    },
    {
        "hdtext.size",
        "Dialogue Text (HD)",
        "Font Size",
        "In tenths of a GBA pixel (100 = 10 px, about the original cap height)",
        RECOMP_RUNTIME_UI_INT,
        30, 200, 5,
        nullptr, 0, nullptr
    },
    {
        "hdtext.line_spacing",
        "Dialogue Text (HD)",
        "Line Spacing %",
        "Line pitch as a percentage of the font size (120 = 12 px at size 10)",
        RECOMP_RUNTIME_UI_INT,
        80, 200, 5,
        nullptr, 0, nullptr
    },
    {
        "hdtext.letter_spacing",
        "Dialogue Text (HD)",
        "Letter Spacing",
        "Extra advance per letter in tenths of a GBA pixel",
        RECOMP_RUNTIME_UI_INT,
        -10, 30, 1,
        nullptr, 0, nullptr
    },
    {
        "hdtext.wrap",
        "Dialogue Text (HD)",
        "Wrapping",
        "Keep the engine lines (same words per line) or re-wrap the page to the text box",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 1, 1,
        kHdWrapChoices, 2, nullptr
    },
    {
        "hdtext.style",
        "Dialogue Text (HD)",
        "Text Style",
        "Plain, drop shadow or outline",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 2, 1,
        kHdStyleChoices, 3, nullptr
    },
    {
        "hdtext.shadow_offset",
        "Dialogue Text (HD)",
        "Shadow Offset",
        "Tenths of a GBA pixel (the game uses 10)",
        RECOMP_RUNTIME_UI_INT,
        2, 30, 1,
        nullptr, 0, nullptr
    },
    {
        "hdtext.outline_width",
        "Dialogue Text (HD)",
        "Outline Width",
        "Tenths of a GBA pixel",
        RECOMP_RUNTIME_UI_INT,
        2, 30, 1,
        nullptr, 0, nullptr
    },
    {
        "hdtext.color",
        "Dialogue Text (HD)",
        "Text Colour",
        "Preset text colours",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 7, 1,
        kHdTextColorChoices, 8, nullptr
    },
    {
        "hdtext.shadow_color",
        "Dialogue Text (HD)",
        "Shadow Colour",
        "Preset shadow colours",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 6, 1,
        kHdShadowColorChoices, 7, nullptr
    },
    {
        "hdtext.outline_color",
        "Dialogue Text (HD)",
        "Outline Colour",
        "Preset outline colours",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 3, 1,
        kHdOutlineColorChoices, 4, nullptr
    },
    {
        "hdtext.opacity",
        "Dialogue Text (HD)",
        "Text Opacity %",
        "Opacity of the dialogue text",
        RECOMP_RUNTIME_UI_INT,
        20, 100, 5,
        nullptr, 0, nullptr
    },
    {
        "hdtext.padding",
        "Dialogue Text (HD)",
        "Box Padding",
        "Free space kept inside the text area, tenths of a GBA pixel",
        RECOMP_RUNTIME_UI_INT,
        0, 80, 5,
        nullptr, 0, nullptr
    },
    {
        "hdtext.auto_fit",
        "Dialogue Text (HD)",
        "Auto-Fit",
        "Shrink the text until the page fits the box; pages that cannot fit show the original text",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "hdtext.min_size",
        "Dialogue Text (HD)",
        "Minimum Size",
        "Auto-fit lower bound in tenths of a GBA pixel",
        RECOMP_RUNTIME_UI_INT,
        30, 120, 5,
        nullptr, 0, nullptr
    },
    {
        "dialogue.backlog_enable",
        "Dialogue & Story",
        "Conversation Log (Backlog)",
        "Translucent story history (press F2 or the gamepad Back button)",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "dialogue.open_backlog",
        "Dialogue & Story",
        "Open Conversation Log",
        "View transcript and story history",
        RECOMP_RUNTIME_UI_ACTION,
        0, 0, 0,
        nullptr, 0, nullptr
    },
    {
        "audio.hd_enabled",
        "Audio",
        "HD Re:CoM Soundtrack",
        "High-fidelity PS2 Re:CoM soundtrack",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "audio.bgm_volume",
        "Audio",
        "Music (BGM) Volume (%)",
        "Independent music volume (0% to 150%)",
        RECOMP_RUNTIME_UI_INT,
        0, 150, 5,
        nullptr, 0, nullptr
    },
    {
        "audio.sfx_volume",
        "Audio",
        "Effects (SFX) Volume (%)",
        "Independent sound effects volume (0-150%)",
        RECOMP_RUNTIME_UI_INT,
        0, 150, 5,
        nullptr, 0, nullptr
    },
    {
        "perf.mode",
        "Diagnostics & HUD",
        "Display Mode",
        "FPS and frametime overlay (or press F10)",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 3, 1,
        kPerfModeChoices, 4, nullptr
    },
    {
        "perf.position",
        "Diagnostics & HUD",
        "Screen Position",
        "Screen corner or drag anywhere with mouse",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 5, 1,
        kPerfPositionChoices, 6, nullptr
    },
    {
        "perf.theme",
        "Diagnostics & HUD",
        "Visual Theme",
        "Color palette and background opacity",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 3, 1,
        kPerfThemeChoices, 4, nullptr
    },
    {
        "audio.eq_preset",
        "Audio",
        "Equalizer Profile",
        "Real-time DSP sound equalizer profile",
        RECOMP_RUNTIME_UI_CHOICE,
        0, 3, 1,
        kEqChoices, 4, nullptr
    },
    {
        "audio.stereo_width",
        "Audio",
        "Stereo Width (%)",
        "Stereo width: 0% mono to 200% expanded",
        RECOMP_RUNTIME_UI_INT,
        0, 200, 10,
        nullptr, 0, nullptr
    },
    {
        "audio.anti_aliasing",
        "Audio",
        "DAC Anti-Aliasing",
        "Biquad filter to cut GBA audio hiss",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    },
    {
        "audio.limiter",
        "Audio",
        "Dynamic Peak Limiter",
        "Dynamic limiter preventing audio clipping",
        RECOMP_RUNTIME_UI_BOOL,
        0, 1, 1,
        nullptr, 0, nullptr
    }
};


// ---- HD dialogue text (text_overlay) settings <-> integer UI values ---------------------------------
template <size_t N>
int colour_index(const std::uint32_t (&tbl)[N], std::uint32_t rgba) {
    for (size_t i = 0; i < N; ++i) if (tbl[i] == rgba) return static_cast<int>(i);
    return 0;
}

int tenths(float v) { return static_cast<int>(std::lround(v * 10.0f)); }

// Legacy text keys of older config files / UI (dialogue.text_mode, font_*, line_capacity, soft_word_wrap):
// they described the removed in-engine enhanced text and are accepted but ignored.
bool is_legacy_text_key(const char* key) {
    static const char* const kLegacy[] = {
        "dialogue.text_mode", "dialogue.font_family", "dialogue.font_size", "dialogue.font_density",
        "dialogue.font_scale", "dialogue.font_style", "dialogue.line_capacity", "dialogue.soft_word_wrap"};
    for (const char* k : kLegacy) if (std::strcmp(key, k) == 0) return true;
    return false;
}

// Returns 1 when `key` is a hdtext.* key (value written to *out), 0 otherwise.
int hdtext_get(const char* key, int* out) {
    if (std::strncmp(key, "hdtext.", 7) != 0) return 0;
    const overlay::OverlaySettings& o = overlay::settings();
    const char* k = key + 7;
    if (!std::strcmp(k, "enabled")) *out = o.enabled ? 1 : 0;
    else if (!std::strcmp(k, "family")) *out = o.family;
    else if (!std::strcmp(k, "size")) *out = tenths(o.size_em);
    else if (!std::strcmp(k, "line_spacing")) *out = static_cast<int>(std::lround(o.line_spacing * 100.0f));
    else if (!std::strcmp(k, "letter_spacing")) *out = tenths(o.letter_spacing);
    else if (!std::strcmp(k, "wrap")) *out = static_cast<int>(o.wrap);
    else if (!std::strcmp(k, "style")) *out = static_cast<int>(o.style);
    else if (!std::strcmp(k, "shadow_offset")) *out = tenths(o.shadow_offset);
    else if (!std::strcmp(k, "outline_width")) *out = tenths(o.outline_width);
    else if (!std::strcmp(k, "color")) *out = colour_index(kHdTextColors, o.text_rgba);
    else if (!std::strcmp(k, "shadow_color")) *out = colour_index(kHdShadowColors, o.shadow_rgba);
    else if (!std::strcmp(k, "outline_color")) *out = colour_index(kHdOutlineColors, o.outline_rgba);
    else if (!std::strcmp(k, "opacity")) *out = static_cast<int>(std::lround(o.opacity * 100.0f));
    else if (!std::strcmp(k, "padding")) *out = tenths(o.box_padding);
    else if (!std::strcmp(k, "auto_fit")) *out = o.auto_fit ? 1 : 0;
    else if (!std::strcmp(k, "min_size")) *out = tenths(o.min_size_em);
    else return 0;
    return 1;
}

// Returns 1 when handled. Values are clamped to the ranges of the UI items. Layout and glyph caches are
// keyed by the settings, so no explicit invalidation is needed (changes apply live).
int hdtext_set(const char* key, int v) {
    if (std::strncmp(key, "hdtext.", 7) != 0) return 0;
    overlay::OverlaySettings& o = overlay::settings();
    const char* k = key + 7;
    const float f = static_cast<float>(v);
    if (!std::strcmp(k, "enabled")) o.enabled = v != 0;
    else if (!std::strcmp(k, "family")) {
        const int n = static_cast<int>(overlay::OverlayRenderer::instance().family_names().size());
        o.family = std::clamp(v, 0, std::max(0, n - 1));
    }
    else if (!std::strcmp(k, "size")) o.size_em = std::clamp(f, 30.0f, 200.0f) / 10.0f;
    else if (!std::strcmp(k, "line_spacing")) o.line_spacing = std::clamp(f, 80.0f, 200.0f) / 100.0f;
    else if (!std::strcmp(k, "letter_spacing")) o.letter_spacing = std::clamp(f, -10.0f, 30.0f) / 10.0f;
    else if (!std::strcmp(k, "wrap")) o.wrap = v ? overlay::WrapMode::Reflow : overlay::WrapMode::KeepEngineBreaks;
    else if (!std::strcmp(k, "style")) o.style = static_cast<overlay::TextStyle>(std::clamp(v, 0, 2));
    else if (!std::strcmp(k, "shadow_offset")) o.shadow_offset = std::clamp(f, 2.0f, 30.0f) / 10.0f;
    else if (!std::strcmp(k, "outline_width")) o.outline_width = std::clamp(f, 2.0f, 30.0f) / 10.0f;
    else if (!std::strcmp(k, "color")) o.text_rgba = kHdTextColors[std::clamp(v, 0, 7)];
    else if (!std::strcmp(k, "shadow_color")) o.shadow_rgba = kHdShadowColors[std::clamp(v, 0, 6)];
    else if (!std::strcmp(k, "outline_color")) o.outline_rgba = kHdOutlineColors[std::clamp(v, 0, 3)];
    else if (!std::strcmp(k, "opacity")) o.opacity = std::clamp(f, 20.0f, 100.0f) / 100.0f;
    else if (!std::strcmp(k, "padding")) o.box_padding = std::clamp(f, 0.0f, 80.0f) / 10.0f;
    else if (!std::strcmp(k, "auto_fit")) o.auto_fit = v != 0;
    else if (!std::strcmp(k, "min_size")) o.min_size_em = std::clamp(f, 30.0f, 120.0f) / 10.0f;
    else return 0;
    return 1;
}

// ini key <-> ui key of the integer HD text settings (section [HdText]); the font family is stored by name
// (hdtext_font) so adding or removing fonts/*.ttf does not shift the selection.
const struct { const char* ini; const char* ui; } kHdTextKeys[] = {
    {"hdtext_enabled", "hdtext.enabled"},             {"hdtext_size", "hdtext.size"},
    {"hdtext_line_spacing", "hdtext.line_spacing"},   {"hdtext_letter_spacing", "hdtext.letter_spacing"},
    {"hdtext_wrap", "hdtext.wrap"},                   {"hdtext_style", "hdtext.style"},
    {"hdtext_shadow_offset", "hdtext.shadow_offset"}, {"hdtext_outline_width", "hdtext.outline_width"},
    {"hdtext_color", "hdtext.color"},                 {"hdtext_shadow_color", "hdtext.shadow_color"},
    {"hdtext_outline_color", "hdtext.outline_color"}, {"hdtext_opacity", "hdtext.opacity"},
    {"hdtext_padding", "hdtext.padding"},             {"hdtext_auto_fit", "hdtext.auto_fit"},
    {"hdtext_min_size", "hdtext.min_size"},
};

bool hdtext_ini_key(const std::string& key, int value);   // defined after ui_set_callback

// Fills the dynamic family list of the "hdtext.family" menu item (built-in + fonts/ folder).
void init_font_choices() {
    g_font_choice_names = overlay::OverlayRenderer::instance().family_names();
    if (g_font_choice_names.size() > kMaxFontChoices) g_font_choice_names.resize(kMaxFontChoices);
    for (size_t i = 0; i < g_font_choice_names.size(); ++i) g_font_choice_ptrs[i] = g_font_choice_names[i].c_str();
    for (RecompRuntimeUiItem& it : kExtraItems)
        if (it.key && !std::strcmp(it.key, "hdtext.family")) {
            it.choices = g_font_choice_ptrs;
            it.choice_count = g_font_choice_names.size();
            it.maximum = static_cast<int>(g_font_choice_names.size()) - 1;
        }
}

int ui_get_callback(const char* key, int* value_out) {
    if (!key || !value_out) return 0;
    auto& dsp = gbaimp::AudioDsp::instance();
    auto& hud = gbaimp::PerfHud::instance();
    auto& interp = gbaimp::FrameInterpolator::instance();
    auto& filters = gbaimp::ScreenFilters::instance();
    auto& xbrz = gbaimp::PixelArtUpscaler::instance();
    auto& input = gbaimp::InputEnhancements::instance();
    auto& hd_audio = gbaimp::HdAudioPlayer::instance();
    auto& turbo = gbaimp::DialogTurbo::instance();
    auto& backlog = DialogueBacklog::instance();

    if (hdtext_get(key, value_out)) return 1;
    if (is_legacy_text_key(key)) return 0;
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
        int idx = (xbrz.scale() == gbaimp::UpscaleScale::None) ? 0 : (static_cast<int>(xbrz.scale()) - 1);
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
    if (std::strcmp(key, "input.analog_mode") == 0) {
        *value_out = static_cast<int>(input.settings().analog_mode);
        return 1;
    }
    if (std::strcmp(key, "input.deadzone") == 0) {
        *value_out = static_cast<int>(input.settings().deadzone * 100.0f + 0.5f);
        return 1;
    }
    if (std::strcmp(key, "input.gamepad_profile") == 0) {
        *value_out = static_cast<int>(input.settings().gamepad_profile);
        return 1;
    }
    if (std::strcmp(key, "input.pad_attack") == 0) {
        *value_out = static_cast<int>(input.settings().pad_a);
        return 1;
    }
    if (std::strcmp(key, "input.pad_jump") == 0) {
        *value_out = static_cast<int>(input.settings().pad_b);
        return 1;
    }
    if (std::strcmp(key, "input.pad_deck_l") == 0) {
        *value_out = static_cast<int>(input.settings().pad_l);
        return 1;
    }
    if (std::strcmp(key, "input.pad_deck_r") == 0) {
        *value_out = static_cast<int>(input.settings().pad_r);
        return 1;
    }
    if (std::strcmp(key, "input.kb_preset") == 0) {
        *value_out = static_cast<int>(input.settings().kb_preset);
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

static int ui_action_callback(const char* key) {
    if (!key) return 0;
    if (std::strcmp(key, "dialogue.open_backlog") == 0) {
        DialogueBacklog::instance().set_open(true);
        return 1;
    }
    if (std::strcmp(key, "input.reset_controls") == 0) {
        gbaimp::InputEnhancements::instance().reset_to_defaults();
        save_khcom_config();
        return 1;
    }
    if (std::strcmp(key, "input.reset_pad") == 0) {
        gbaimp::InputEnhancements::instance().reset_gamepad_defaults();
        save_khcom_config();
        return 1;
    }
    if (std::strcmp(key, "input.reset_kb") == 0) {
        gbaimp::InputEnhancements::instance().reset_keyboard_defaults();
        save_khcom_config();
        return 1;
    }
    return 0;
}

static int ui_get_text_callback(const char* key, char* buf, std::size_t buf_len) {
    if (!key || !buf || buf_len == 0) return 0;
    auto& input = gbaimp::InputEnhancements::instance();
    if (std::strncmp(key, "input.key_", 10) == 0) {
        const char* name = key + 10;
        gbaimp::GbaKeyIndex idx = input.key_index_from_name(name);
        if (idx != gbaimp::KEY_COUNT) {
            const char* kname = input.get_key_name(idx);
            std::snprintf(buf, buf_len, "%s", kname);
            return 1;
        }
    }
    return 0;
}

static int ui_set_text_callback(const char* key, const char* value) {
    if (!key || !value) return 0;
    auto& input = gbaimp::InputEnhancements::instance();
    if (std::strncmp(key, "input.key_", 10) == 0) {
        const char* name = key + 10;
        gbaimp::GbaKeyIndex idx = input.key_index_from_name(name);
        if (idx != gbaimp::KEY_COUNT) {
            bool ok = input.set_key_from_name(idx, value);
            if (ok) {
                input.save_keybinds();
            }
            return ok ? 1 : 0;
        }
    }
    return 0;
}

static int ui_enabled_callback(const char* key) {
    if (!key) return 1;
    if (std::strcmp(key, "system.resume") == 0) {
        return 0;
    }
    return 1;
}

static bool s_is_loading_config = false;

static void save_khcom_config_impl() {
    if (capture::active()) return;   // harness runs never persist their overrides

    std::ofstream file("khcom_config.ini");
    if (!file.is_open()) return;

    auto& dsp = gbaimp::AudioDsp::instance();
    auto& hud = gbaimp::PerfHud::instance();
    auto& interp = gbaimp::FrameInterpolator::instance();
    auto& filters = gbaimp::ScreenFilters::instance();
    auto& xbrz = gbaimp::PixelArtUpscaler::instance();
    auto& input = gbaimp::InputEnhancements::instance();
    auto& hd_audio = gbaimp::HdAudioPlayer::instance();
    auto& turbo = gbaimp::DialogTurbo::instance();
    auto& backlog = DialogueBacklog::instance();

    file << "[Video]\n";
    file << "fps_target=" << static_cast<int>(interp.settings().mode) << "\n";
    file << "motion_smoothing=" << static_cast<int>(interp.settings().smoothing) << "\n";
    int xbrz_idx = (xbrz.scale() == gbaimp::UpscaleScale::None) ? 0 : (static_cast<int>(xbrz.scale()) - 1);
    file << "xbrz_scale=" << xbrz_idx << "\n";
    file << "color_profile=" << static_cast<int>(filters.settings().color_profile) << "\n";
    file << "screen_mask=" << static_cast<int>(filters.settings().mask_type) << "\n";
    file << "mask_intensity=" << static_cast<int>(filters.settings().mask_intensity * 100.0f + 0.5f) << "\n\n";

    file << "[Audio]\n";
    file << "hd_enabled=" << (hd_audio.settings().enabled ? 1 : 0) << "\n";
    file << "bgm_volume=" << static_cast<int>(hd_audio.settings().bgm_volume * 100.0f + 0.5f) << "\n";
    file << "sfx_volume=" << static_cast<int>(hd_audio.settings().sfx_volume * 100.0f + 0.5f) << "\n";
    file << "eq_preset=" << static_cast<int>(dsp.settings().preset) << "\n";
    file << "stereo_width=" << static_cast<int>(dsp.settings().stereo_width * 100.0f + 0.5f) << "\n";
    file << "anti_aliasing=" << (dsp.settings().anti_aliasing_filter ? 1 : 0) << "\n";
    file << "limiter=" << (dsp.settings().limiter_enabled ? 1 : 0) << "\n\n";

    file << "[Dialogue]\n";
    file << "backlog_enable=" << (backlog.is_enabled() ? 1 : 0) << "\n\n";

    file << "[HdText]\n";
    {
        const std::vector<std::string> fams = overlay::OverlayRenderer::instance().family_names();
        const int fi = overlay::settings().family;
        if (fi >= 0 && fi < static_cast<int>(fams.size())) file << "hdtext_font=" << fams[fi] << "\n";
        for (const auto& m : kHdTextKeys) {
            int v = 0;
            if (hdtext_get(m.ui, &v)) file << m.ini << "=" << v << "\n";
        }
        file << "\n";
    }

    file << "[Controls]\n";
    file << "analog_mode=" << static_cast<int>(input.settings().analog_mode) << "\n";
    file << "analog_deadzone=" << static_cast<int>(input.settings().deadzone * 100.0f + 0.5f) << "\n";
    file << "gamepad_profile=" << static_cast<int>(input.settings().gamepad_profile) << "\n";
    file << "pad_attack=" << static_cast<int>(input.settings().pad_a) << "\n";
    file << "pad_jump=" << static_cast<int>(input.settings().pad_b) << "\n";
    file << "pad_deck_l=" << static_cast<int>(input.settings().pad_l) << "\n";
    file << "pad_deck_r=" << static_cast<int>(input.settings().pad_r) << "\n";
    file << "kb_preset=" << static_cast<int>(input.settings().kb_preset) << "\n\n";

    file << "[Gameplay]\n";
    file << "turbo_dialog=" << (turbo.is_enabled() ? 1 : 0) << "\n\n";

    file << "[Diagnostics]\n";
    file << "perf_mode=" << static_cast<int>(hud.settings().mode) << "\n";
    file << "perf_position=" << static_cast<int>(hud.settings().position) << "\n";
    file << "perf_theme=" << static_cast<int>(hud.settings().theme) << "\n";
    file << "hud_custom_x=" << static_cast<int>(hud.settings().custom_x) << "\n";
    file << "hud_custom_y=" << static_cast<int>(hud.settings().custom_y) << "\n";
}

int ui_set_callback(const char* key, int value) {
    if (!key) return 0;
    auto& dsp = gbaimp::AudioDsp::instance();
    auto& hud = gbaimp::PerfHud::instance();
    auto& interp = gbaimp::FrameInterpolator::instance();
    auto& filters = gbaimp::ScreenFilters::instance();
    auto& xbrz = gbaimp::PixelArtUpscaler::instance();
    auto& input = gbaimp::InputEnhancements::instance();
    auto& hd_audio = gbaimp::HdAudioPlayer::instance();
    auto& turbo = gbaimp::DialogTurbo::instance();
    auto& backlog = DialogueBacklog::instance();

    bool handled = false;
    if (is_legacy_text_key(key)) {
        return 1;   // accepted and ignored: the in-engine enhanced text is no longer reachable
    } else if (hdtext_set(key, value)) {
        handled = true;
    } else if (std::strcmp(key, "dialogue.backlog_enable") == 0) {
        backlog.set_enabled(value != 0);
        handled = true;
    } else if (std::strcmp(key, "video.fps_target") == 0) {
        interp.set_mode(static_cast<gbaimp::FramerateMode>(value));
        handled = true;
    } else if (std::strcmp(key, "video.motion_smoothing") == 0) {
        interp.set_smoothing(static_cast<gbaimp::MotionSmoothingMode>(value));
        handled = true;
    } else if (std::strcmp(key, "video.xbrz_scale") == 0) {
        gbaimp::UpscaleScale scale = (value <= 0) ? gbaimp::UpscaleScale::None : static_cast<gbaimp::UpscaleScale>(value + 1);
        xbrz.set_scale(scale);
        handled = true;
    } else if (std::strcmp(key, "video.color_profile") == 0) {
        filters.set_color_profile(static_cast<gbaimp::ColorProfile>(value));
        handled = true;
    } else if (std::strcmp(key, "video.screen_mask") == 0) {
        filters.set_mask_type(static_cast<gbaimp::ScreenMaskType>(value));
        handled = true;
    } else if (std::strcmp(key, "video.mask_intensity") == 0) {
        filters.set_mask_intensity(static_cast<float>(value) / 100.0f);
        handled = true;
    } else if (std::strcmp(key, "input.analog_mode") == 0) {
        input.set_analog_mode(static_cast<gbaimp::AnalogMode>(value));
        handled = true;
    } else if (std::strcmp(key, "input.deadzone") == 0) {
        input.set_deadzone(static_cast<float>(value) / 100.0f);
        handled = true;
    } else if (std::strcmp(key, "input.gamepad_profile") == 0) {
        input.apply_gamepad_profile(static_cast<gbaimp::GamepadProfile>(value));
        handled = true;
    } else if (std::strcmp(key, "input.pad_attack") == 0) {
        input.set_pad_a(value);
        handled = true;
    } else if (std::strcmp(key, "input.pad_jump") == 0) {
        input.set_pad_b(value);
        handled = true;
    } else if (std::strcmp(key, "input.pad_deck_l") == 0) {
        input.set_pad_l(value);
        handled = true;
    } else if (std::strcmp(key, "input.pad_deck_r") == 0) {
        input.set_pad_r(value);
        handled = true;
    } else if (std::strcmp(key, "input.kb_preset") == 0) {
        input.apply_keyboard_preset(static_cast<gbaimp::KeyboardPreset>(value));
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
        hud.set_mode(static_cast<gbaimp::PerfHudMode>(value));
        handled = true;
    } else if (std::strcmp(key, "perf.position") == 0) {
        hud.set_position(static_cast<gbaimp::PerfHudPosition>(value));
        handled = true;
    } else if (std::strcmp(key, "perf.theme") == 0) {
        hud.set_theme(static_cast<gbaimp::PerfHudTheme>(value));
        handled = true;
    } else if (std::strcmp(key, "audio.eq_preset") == 0) {
        dsp.apply_preset(static_cast<gbaimp::EqPreset>(value));
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

bool hdtext_ini_key(const std::string& key, int value) {
    // Index form of the family, accepted for the capture harness (KHCOM_SET / variants take integers only); the config
    // file itself stores the family by name (hdtext_font) and never writes this key.
    if (key == "hdtext_family") { ui_set_callback("hdtext.family", value); return true; }
    for (const auto& m : kHdTextKeys)
        if (key == m.ini) { ui_set_callback(m.ui, value); return true; }
    return false;
}

static void load_khcom_config_impl() {
    auto& input = gbaimp::InputEnhancements::instance();
    input.load_keybinds();

    std::ifstream file("khcom_config.ini");
    // Headless harness overrides (KHCOM_SET), same keys as the ini; applied after the file.
    std::string overrides;
    for (const auto& kv : capture::settings_overrides()) overrides += kv.first + "=" + std::to_string(kv.second) + '\n';
    if (!file.is_open() && overrides.empty()) return;

    s_is_loading_config = true;
    auto process = [&](std::istream& in) {
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '[' || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        std::string val_str = line.substr(eq + 1);
        if (key == "hdtext_font") {
            while (!val_str.empty() && (val_str.back() == '\r' || val_str.back() == ' ')) val_str.pop_back();
            const int idx = overlay::OverlayRenderer::instance().family_index(val_str);
            if (idx >= 0) overlay::settings().family = idx;
            continue;
        }
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
        else if (key == "hud_anchoring") { /* legacy key: HUD anchoring now follows the 16:9 view mode; ignored */ }
        else if (key == "hd_enabled") ui_set_callback("audio.hd_enabled", val);
        else if (key == "bgm_volume") ui_set_callback("audio.bgm_volume", val);
        else if (key == "sfx_volume") ui_set_callback("audio.sfx_volume", val);
        else if (key == "eq_preset") ui_set_callback("audio.eq_preset", val);
        else if (key == "stereo_width") ui_set_callback("audio.stereo_width", val);
        else if (key == "anti_aliasing") ui_set_callback("audio.anti_aliasing", val);
        else if (key == "limiter") ui_set_callback("audio.limiter", val);
        else if (key == "text_mode" || key == "font_family" || key == "font_size" || key == "font_density" ||
                 key == "font_scale" || key == "font_style" || key == "line_capacity" || key == "soft_word_wrap") {
            /* legacy in-engine enhanced text keys: parsed and ignored */
        }
        else if (key == "backlog_enable") ui_set_callback("dialogue.backlog_enable", val);
        else if (key == "analog_mode") ui_set_callback("input.analog_mode", val);
        else if (key == "analog_deadzone") ui_set_callback("input.deadzone", val);
        else if (key == "gamepad_profile") ui_set_callback("input.gamepad_profile", val);
        else if (key == "pad_attack") ui_set_callback("input.pad_attack", val);
        else if (key == "pad_jump") ui_set_callback("input.pad_jump", val);
        else if (key == "pad_deck_l") ui_set_callback("input.pad_deck_l", val);
        else if (key == "pad_deck_r") ui_set_callback("input.pad_deck_r", val);
        else if (key == "kb_preset") ui_set_callback("input.kb_preset", val);
        else if (hdtext_ini_key(key, val)) { /* HD dialogue text */ }
        else if (key == "turbo_dialog") ui_set_callback("gameplay.turbo_dialog", val);
        else if (key == "perf_mode") ui_set_callback("perf.mode", val);
        else if (key == "perf_position") ui_set_callback("perf.position", val);
        else if (key == "perf_theme") ui_set_callback("perf.theme", val);
        else if (key == "hud_custom_x") gbaimp::PerfHud::instance().settings().custom_x = static_cast<float>(val);
        else if (key == "hud_custom_y") gbaimp::PerfHud::instance().settings().custom_y = static_cast<float>(val);
    }
    };
    if (file.is_open()) process(file);
    std::istringstream ov(overrides);
    process(ov);
    s_is_loading_config = false;
}
#endif

} // namespace

void save_khcom_config() {
#if defined(GBARECOMP_RUNTIME_UI)
    save_khcom_config_impl();
#endif
}

void load_khcom_config() {
#if defined(GBARECOMP_RUNTIME_UI)
    load_khcom_config_impl();
#endif
}

#if defined(GBARECOMP_RUNTIME_UI)
static RecompRuntimeUi*& s_active_runtime_ui = g_active_runtime_ui;  // set by create_standard_ui()

bool is_runtime_menu_open() {
    return s_active_runtime_ui && recomp_runtime_ui_is_open(s_active_runtime_ui);
}

void open_runtime_menu() {
    if (s_active_runtime_ui && !recomp_runtime_ui_is_open(s_active_runtime_ui)) {
        recomp_runtime_ui_open(s_active_runtime_ui);
    }
}

void close_runtime_menu() {
    if (s_active_runtime_ui && recomp_runtime_ui_is_open(s_active_runtime_ui)) {
        recomp_runtime_ui_close(s_active_runtime_ui);
    }
}

void toggle_runtime_menu() {
    if (s_active_runtime_ui) {
        if (recomp_runtime_ui_is_open(s_active_runtime_ui)) {
            recomp_runtime_ui_close(s_active_runtime_ui);
        } else {
            recomp_runtime_ui_open(s_active_runtime_ui);
        }
    }
}
#else
bool is_runtime_menu_open() {
    return false;
}
void open_runtime_menu() {}
void close_runtime_menu() {}
void toggle_runtime_menu() {}
#endif

#if defined(GBARECOMP_RUNTIME_UI)
// Live setting changes requested by the capture harness (KHCOM_CAPTURE_VARIANTS): khcom_config.ini
// keys mapped onto the same ui_set_callback keys the settings overlay uses.
static int harness_set_setting(const char* ini_key, int value) {
    static const struct { const char* ini; const char* ui; } kMap[] = {
        {"screen_mask", "video.screen_mask"},     {"mask_intensity", "video.mask_intensity"},
        {"color_profile", "video.color_profile"}, {"xbrz_scale", "video.xbrz_scale"},
        {"motion_smoothing", "video.motion_smoothing"},
        {"perf_mode", "perf.mode"},
        {"hdtext_family", "hdtext.family"},   // family by index (harness only; the config file stores the name)
    };
    for (const auto& m : kMap)
        if (std::strcmp(ini_key, m.ini) == 0) return ui_set_callback(m.ui, value);
    for (const auto& m : kHdTextKeys)
        if (std::strcmp(ini_key, m.ini) == 0) return ui_set_callback(m.ui, value);
    return 0;
}
#endif

gbarecomp::RunOptions create_run_options() {
#if defined(GBARECOMP_RUNTIME_UI)
    if (capture::active()) capture::set_setting_callback(&harness_set_setting);
#endif
    // Connect the gba-improvements library (hooks + HUD anchoring zones) before any
    // module can run or persist settings.
    register_gbaimp_integration();

    gbarecomp::RunOptions opts;
    opts.builtin_game_name = GAME_TITLE.data();
    opts.builtin_rom_sha1 = ROM_SHA1_USA.data();
    opts.launcher_region = "USA";
    opts.launcher_game_config = "game.toml";

    // Assist tools: save states, fast-forward, and rewind
    opts.expose_assist_tools = true;
    opts.assist_tools_enabled_by_default = true;
    opts.assist_fast_forward_multiplier_default = 4;
    opts.save_state_slot_count = 10;
    opts.rewind_history_seconds = 60;
    opts.rewind_capture_interval_frames = 15;

    // Video display and viewport configuration:
    // Fixed 16:9 widescreen active ONLY during battle arenas; authentic 3:2 elsewhere
    opts.freely_resizable_window = true;
    opts.resize_driven_view = false;
    opts.max_view_width = 284;
    opts.max_resize_view_width = 284;
    opts.max_resize_view_height = 160;
    opts.launcher_expose_widescreen = true;
    opts.launcher_expose_adaptive_view = false;
    opts.widescreen_view_width = 284;
    opts.launcher_aspect_labels = kAspectLabels;
    opts.launcher_aspect_view_widths = kAspectWidths;
    opts.launcher_num_aspects = sizeof(kAspectWidths) / sizeof(kAspectWidths[0]);
    opts.launcher_expose_sharp_filter = true;
    opts.launcher_default_sharp_filter = true;
    opts.extended_view_init = &khcom_install_widescreen_adapter;
    opts.extended_view_frame = [](const gbarecomp::ExtendedViewFrameInfo* info) {
        khcom_update_widescreen_state();
        auto* bus = gbarecomp::active_bus();
        if (bus) {
            uint16_t host = bus->io().host_keyinput();
            uint16_t modified = gbaimp::InputEnhancements::instance().process_keyinput(host);
            if (capture::active()) capture::on_emulated_frame(info ? info->frame_count : 0);   // headless harness
            bus->io().set_keyinput(modified);
        }
    };

    // Immediately install widescreen hooks so battle state tracking is active from frame 0
    khcom_install_widescreen_adapter(22, 22);

#if defined(GBARECOMP_RUNTIME_UI)
    init_font_choices();
    opts.ui_extra_items = kExtraItems;
    opts.ui_extra_item_count = sizeof(kExtraItems) / sizeof(kExtraItems[0]);
    opts.ui_get = ui_get_callback;
    opts.ui_set = ui_set_callback;
    opts.ui_action = ui_action_callback;
    opts.ui_enabled = ui_enabled_callback;
    opts.ui_get_text = ui_get_text_callback;
    opts.ui_set_text = ui_set_text_callback;

    // Restore saved user configurations across game sessions
    load_khcom_config();
#endif

    return opts;
}

} // namespace khcom

#if defined(GBARECOMP_RUNTIME_UI) && (defined(__GNUC__) || defined(__clang__))
// GNU ld: `-Wl,--wrap=recomp_runtime_ui_create_standard` (CMakeLists.txt). On MSVC the same
// function is interposed by a compile-time symbol rename, see runtime_ui_standard.cpp.
extern "C" {
RecompRuntimeUi* __real_recomp_runtime_ui_create_standard(const RecompRuntimeUiStandardConfig* standard);
RecompRuntimeUi* __wrap_recomp_runtime_ui_create_standard(const RecompRuntimeUiStandardConfig* standard) {
    return khcom::create_standard_ui(standard, &__real_recomp_runtime_ui_create_standard);
}
}
#endif
