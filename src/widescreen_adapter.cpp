#include "widescreen_adapter.h"
#include "hud_anchoring.h"
#include "dialogue_backlog.h"
#include "input_enhancements.h"
#include "armv4t/runtime_arm.h"
#include <algorithm>

extern "C" {
extern int (*g_ws_tilemap_provider)(int bg, int hw_x, int screen_y, uint16_t* out_entry);
extern int (*g_ws_bg_x_provider)(int bg, int output_x, int screen_y, int* out_hw_x);
extern unsigned g_ws_bg_x_provider_layers;
extern int (*g_ws_obj_attr_x_provider)(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x);
extern int g_ws_authored_margin_layers;
extern int g_ws_obj_native_clip;
extern int g_ws_pillarbox;
extern int g_ws_pillarbox_left;
extern int g_ws_pillarbox_right;
extern unsigned g_ws_active;
extern unsigned g_ws_extra;
extern unsigned g_ws_extra_left;
extern unsigned g_ws_extra_right;
extern unsigned g_ws_view_width;
extern void (*g_runtime_fn_entry_hook)(uint32_t entry_pc);
uint32_t bus_read_u32(uint32_t addr);
}

namespace {
constexpr int kWsTilemapKeepWrapped = 2;
bool s_is_menu = true;
bool s_is_battle = false;
uint64_t s_frame_counter = 0;
uint64_t s_last_battle_frame = 0;
uint64_t s_last_non_battle_frame = 0;
void (*s_prev_fn_entry_hook)(uint32_t entry_pc) = nullptr;

inline bool is_battle_mode(uint32_t mode_or_desc) {
    const uint32_t m = mode_or_desc & ~1u;
    if (m == 0) return false;

    // 1. Battle Mode Descriptor Tables in ROM (0x09xxxxxx)
    if (m == 0x09ECEB40u || // Single-player combat arena descriptor
        m == 0x09ED9B98u || // VS multiplayer combat arena descriptor
        m == 0x09EF1308u || // SIO wireless battle connect descriptor
        m == 0x09EF14DCu || // SIO wireless battle option descriptor
        m == 0x09EF14ECu) { // SIO wireless battle cardget descriptor
        return true;
    }

    // 2. Battle Mode Functions in ROM (0x08xxxxxx)
    if (m == 0x080099D0u || m == 0x0800A4BCu || // mode_battle_0, mode_battle_1
        m == 0x0800C428u || m == 0x0800C5F8u || // mode_vsbattle_0, mode_vsbattle_1
        (m >= 0x080AEBA0u && m <= 0x080B1844u && // mode_sio_btl_*
         m != 0x080AEE24u && m != 0x080B04F0u && m != 0x080B1844u)) {
        return true;
    }

    // 3. If m is a pointer to a descriptor table in ROM, check table function entries
    if (m >= 0x08000000u && m < 0x09FFFFF0u) {
        const uint32_t fn1 = bus_read_u32(m + 4) & ~1u;
        const uint32_t fn2 = bus_read_u32(m + 8) & ~1u;
        if (fn1 == 0x080099D0u || fn2 == 0x0800A4BCu ||
            fn1 == 0x0800C428u || fn2 == 0x0800C5F8u ||
            fn1 == 0x080AEBA0u || fn1 == 0x080AEF38u || fn1 == 0x080B1498u) {
            return true;
        }
    }

    return false;
}

inline bool is_non_battle_mode(uint32_t mode_or_desc) {
    const uint32_t m = mode_or_desc & ~1u;
    if (m == 0) return false;
    if (is_battle_mode(m)) return false;

    // Explicit non-battle descriptors in ROM
    if (m == 0x09ECEB50u || m == 0x09ECEB54u || // mode_chkbtl (room / field exploration)
        m == 0x09ECEB64u ||                     // room transition / debug
        m == 0x09ED9B78u || m == 0x09ED9B88u || // vs setup / menu
        m == 0x09EDE430u ||                     // mode_jiminy (Jiminy's journal)
        m == 0x09EDE4D0u ||                     // mode_movie (story cutscenes)
        m == 0x09EE47ACu ||                     // mode_eventselect
        m == 0x09EF4DB0u ||                     // mode_allmap (world map)
        m == 0x09EF4E50u ||                     // mode_title (title screen)
        m == 0x09EF4EC0u || m == 0x09EF4ED0u || // mode_copyright1 & 2
        m == 0x09EFA9C4u) {                     // mode_StaffRoll (credits)
        return true;
    }

    // Any valid ROM address that is not a battle mode is non-battle
    if (m >= 0x08000000u && m < 0x0A000000u) {
        return true;
    }

    return false;
}

void khcom_fn_entry_hook(uint32_t pc) {
    if (s_prev_fn_entry_hook) {
        s_prev_fn_entry_hook(pc);
    }

    // Fast reject for addresses outside GBA code ranges
    const uint32_t top = pc >> 24;
    if (top != 0x08 && top != 0x02 && top != 0x03) {
        return;
    }

    // 1. Central mode switch calls (gf_func_080010CC and gf_func_080010E0)
    // R0 contains the target mode descriptor pointer
    if (pc == 0x080010CCu || pc == 0x080010E0u) {
        const uint32_t target_mode = g_cpu.R[0];
        if (is_battle_mode(target_mode)) {
            s_is_battle = true;
            s_is_menu = false;
            s_last_battle_frame = s_frame_counter;
            if (g_ws_active && g_ws_view_width > 240) {
                g_ws_pillarbox = 0;
            }
        } else if (is_non_battle_mode(target_mode)) {
            s_is_battle = false;
            s_is_menu = true;
            s_last_non_battle_frame = s_frame_counter;
            g_ws_pillarbox = 1;
        }
        return;
    }

    // 2. Direct execution of mode handlers
    const bool is_battle_mode_func =
        (pc == 0x080099D0u || pc == 0x0800A4BCu ||  // mode_battle_0, mode_battle_1 (Single-player arena)
         pc == 0x0800C428u || pc == 0x0800C5F8u ||  // mode_vsbattle_0, mode_vsbattle_1 (VS battle arena)
         (pc >= 0x080AEBA0u && pc <= 0x080B1844u &&  // mode_sio_btl_* (Wireless multiplayer battle)
          pc != 0x080AEE24u && pc != 0x080B04F0u && pc != 0x080B1844u));

    const bool is_battle_exit_func =
        (pc == 0x0800A5A4u ||                       // mode_battle_2 (single-player battle exit/cleanup)
         pc == 0x0800C678u ||                       // mode_vsbattle_2 (vs battle exit/cleanup)
         pc == 0x080AEE24u || pc == 0x080B04F0u || pc == 0x080B1844u); // sio battle exit

    const bool is_non_battle_func =
        (pc >= 0x0800A608u && pc <= 0x0800AB80u) || // mode_chkbtl_* (Field room exploration)
        (pc >= 0x0800AC34u && pc <= 0x0800B2E4u) || // mode_debug_*
        (pc >= 0x0805ACE4u && pc <= 0x0805C910u) || // mode_jiminy_* (Jiminy's Journal)
        (pc >= 0x0805E8B4u && pc <= 0x0805F0D8u) || // mode_movie_* (Cutscenes)
        (pc >= 0x080750D4u && pc <= 0x08075354u) || // mode_eventselect_*
        (pc >= 0x080D31C4u && pc <= 0x080D4CF8u) || // mode_allmap_* (World Map)
        (pc >= 0x080D5B70u && pc <= 0x080D6FFCu) || // mode_title_* (Title Screen)
        (pc >= 0x080D7014u && pc <= 0x080D7288u) || // mode_copyright1_*, mode_copyright2_*
        (pc >= 0x080D7410u && pc <= 0x080D8B54u) || // mode_status_* (Pause / Status / Deck Menu)
        (pc >= 0x080FFC0Cu && pc <= 0x08109AA0u) || // mode_worldinspect_*, mode_ms_* (Card shop)
        (pc >= 0x08112B3Cu && pc <= 0x08115484u);   // mode_StaffRoll_* (Credits)

    if (is_battle_mode_func) {
        s_is_battle = true;
        s_is_menu = false;
        s_last_battle_frame = s_frame_counter;
        if (g_ws_active && g_ws_view_width > 240) {
            g_ws_pillarbox = 0; // True widescreen in battle arena
        }
    } else if (is_battle_exit_func || is_non_battle_func) {
        s_is_battle = false;
        s_is_menu = true;
        s_last_non_battle_frame = s_frame_counter;
        g_ws_pillarbox = 1; // Authentic 3:2 pillarboxed view for field, menus, cutscenes
    }
}
} // namespace

int khcom_tilemap_provider(int bg, int hw_x, int screen_y, uint16_t* out_entry) {
    (void)bg;
    (void)hw_x;
    (void)screen_y;
    (void)out_entry;
    if (!s_is_battle) {
        return 0;
    }
    // Accept authentic VRAM tilemap data beyond 240px for true widescreen in battle
    return kWsTilemapKeepWrapped;
}

int khcom_bg_x_provider(int bg, int output_x, int screen_y, int* out_hw_x) {
    if (!out_hw_x || !g_ws_active) {
        return 0;
    }

    const int left = static_cast<int>(g_ws_extra_left);
    const int right = static_cast<int>(g_ws_extra_right);
    if (left == 0 && right == 0) {
        return 0;
    }

    const int extra = left + right;
    const bool in_left_margin = (output_x < left);
    const bool in_right_margin = (output_x >= left + 240);
    const bool in_margin = in_left_margin || in_right_margin;

    // When outside of battle, all margin columns are pillarboxed in 3:2
    if (!s_is_battle) {
        if (in_margin) {
            return -1;
        }
        return 0;
    }

    if (bg == 0 || bg == 1) {
        // Dialogue windows and UI layers stay centered in 240px; never bleed into margins
        if (in_margin) {
            return -1;
        }
        return 0;
    }

    if (bg == 2 || bg == 3) {
        // Authentic battle arena background layers render across the full widescreen view
        return 0;
    }

    return 0;
}

int khcom_obj_attr_x_provider(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x) {
    (void)oam_index;
    if (!out_x || !g_ws_active) {
        return 0;
    }

    const int raw_x = static_cast<int>(attr1 & 0x1FFu);
    const int sx = (raw_x >= 256) ? (raw_x - 512) : raw_x;

    // Never displace sprites outside of battle or in menus/cutscenes
    if (!s_is_battle || s_is_menu) {
        if (raw_x >= 256) {
            *out_x = sx;
            return 1;
        }
        return 0;
    }

    // Never displace sprites in menus, cutscenes, or when HUD anchoring is disabled
    const auto anchor_mode = khcom::HudAnchoring::instance().settings().mode;
    if (!s_is_menu && s_is_battle && anchor_mode == khcom::HudAnchorMode::WidescreenAnchored) {
        const int raw_y = static_cast<int>(attr0 & 0xFFu);
        const int sy = (raw_y >= 160) ? (raw_y - 256) : raw_y;

        const uint16_t shape = (attr0 & 0xC000u);
        const uint16_t size  = (attr1 & 0xC000u);
        const int priority = static_cast<int>((attr2 >> 10) & 0x03u);

        // Text glyph sprites in KH:CoM are strictly 16x16 square sprites with Priority 0.
        // Dialogue text, message windows, and font glyphs must NEVER be displaced into margins.
        const bool is_16x16_square = (shape == 0x0000u) && (size == 0x4000u);

        // Authentic player HP gauge & face portrait sprites (btl_hpply.c):
        // - HP bar, gauge outline, and Sora/Riku face portrait pieces all have Priority 1 (SPRITE_PRIORITY(1))
        // - Region: sx in [-16, 85], sy in [-16, 24]
        // - None of the HP gauge sprites are 16x16 square (Sora face is 32x32 + 8x16, bar pieces are 32x8/32x16)
        // - Dialogue windows and tutorial boxes (event_message.c) strictly use Priority 0 and must NEVER be displaced.
        const bool is_player_hp = !is_16x16_square &&
            (priority == 1 && sx >= -16 && sx <= 85 && sy >= -16 && sy <= 24);

        if (is_player_hp) {
            *out_x = sx - static_cast<int>(g_ws_extra_left);
            return 1;
        }

        // Authentic enemy HP gauge sprites (btl_hpenm.c):
        // - Priority is strictly 1 (SPRITE_PRIORITY(1))
        // - Top-right health bar region: sy in [-16, 24], sx in [160, 250]
        // - Gauge pieces are horizontal (32x8 / 32x16)
        if (!is_16x16_square && priority == 1 && sx >= 160 && sx <= 250 && sy >= -16 && sy <= 24) {
            *out_x = sx + static_cast<int>(g_ws_extra_right);
            return 1;
        }
    }

    if (raw_x >= 256) {
        *out_x = sx;
        return 1;
    }

    return 0;
}

void khcom_install_widescreen_adapter(uint32_t extra_left, uint32_t extra_right) {
    (void)extra_left;
    (void)extra_right;
    g_ws_tilemap_provider = &khcom_tilemap_provider;
    g_ws_bg_x_provider = &khcom_bg_x_provider;
    g_ws_bg_x_provider_layers = 0xFu;
    g_ws_obj_attr_x_provider = &khcom_obj_attr_x_provider;
    g_ws_authored_margin_layers = 1;
    g_ws_obj_native_clip = 0;
    g_ws_pillarbox = (s_is_battle && g_ws_active && g_ws_view_width > 240) ? 0 : 1;

    khcom::khcom_install_backlog_hook();

    if (g_runtime_fn_entry_hook != &khcom_fn_entry_hook) {
        s_prev_fn_entry_hook = g_runtime_fn_entry_hook;
        g_runtime_fn_entry_hook = &khcom_fn_entry_hook;
    }
}

void khcom_update_widescreen_state() {
    khcom_is_battle_active();
    g_ws_tilemap_provider = &khcom_tilemap_provider;
    g_ws_bg_x_provider = &khcom_bg_x_provider;
    g_ws_bg_x_provider_layers = 0xFu;
    g_ws_obj_attr_x_provider = &khcom_obj_attr_x_provider;
    g_ws_authored_margin_layers = 1;
    g_ws_obj_native_clip = 0;
    g_ws_pillarbox = (s_is_battle && g_ws_active && g_ws_view_width > 240) ? 0 : 1;
    khcom::khcom_install_backlog_hook();

    if (g_runtime_fn_entry_hook != &khcom_fn_entry_hook) {
        s_prev_fn_entry_hook = g_runtime_fn_entry_hook;
        g_runtime_fn_entry_hook = &khcom_fn_entry_hook;
    }
}

bool khcom_is_battle_active() {
    // 1. Authoritative check: active and pending mode descriptors in GBA IWRAM (0x03007488 / 0x03007494)
    const uint32_t active_mode = bus_read_u32(0x03007488u);
    const uint32_t pending_mode = bus_read_u32(0x03007494u);

    if (active_mode != 0 || pending_mode != 0) {
        const bool mode_is_battle = is_battle_mode(active_mode) || is_battle_mode(pending_mode);
        s_is_battle = mode_is_battle;
        s_is_menu = !mode_is_battle;
        g_ws_pillarbox = (mode_is_battle && g_ws_active && g_ws_view_width > 240) ? 0 : 1;
        return s_is_battle;
    }

    // 2. Fallback check for unit tests or early boot before IWRAM mode is set
    return s_is_battle;
}

void khcom_set_battle_active(bool active) {
    s_is_battle = active;
    s_is_menu = !active;
    if (active) {
        s_last_battle_frame = s_frame_counter;
        if (g_ws_active && g_ws_view_width > 240) {
            g_ws_pillarbox = 0;
        }
    } else {
        s_last_non_battle_frame = s_frame_counter;
        g_ws_pillarbox = 1;
    }
}

void khcom_widescreen_notify_present() {
    ++s_frame_counter;
}

uint64_t khcom_get_frame_counter() {
    return s_frame_counter;
}

void khcom_compute_effective_viewport(int tex_w, int tex_h, int win_w, int win_h,
                                      SDL_Rect* out_src, SDL_Rect* out_dst,
                                      int* out_base_w, int* out_base_h) {
    if (!out_src || !out_dst || tex_w <= 0 || tex_h <= 0 || win_w <= 0 || win_h <= 0) return;

    const bool is_expanded = (tex_w * 2 > tex_h * 3);
    const int factor = (tex_h / 160 > 0) ? (tex_h / 160) : 1;
    const int base_w_val = tex_w / factor;
    const bool in_battle = khcom_is_battle_active();

    if (is_expanded && in_battle) {
        // Mode 16:9 during battle arena: render full wide texture into wide layout
        out_src->x = 0;
        out_src->y = 0;
        out_src->w = tex_w;
        out_src->h = tex_h;

        if (out_base_w) *out_base_w = base_w_val;
        if (out_base_h) *out_base_h = 160;

        float s = std::min(static_cast<float>(win_w) / static_cast<float>(base_w_val),
                           static_cast<float>(win_h) / 160.0f);
        int dw = static_cast<int>(static_cast<float>(base_w_val) * s);
        int dh = static_cast<int>(160.0f * s);
        out_dst->x = (win_w - dw) / 2;
        out_dst->y = (win_h - dh) / 2;
        out_dst->w = dw;
        out_dst->h = dh;
    } else if (is_expanded && !in_battle) {
        // Mode 16:9 outside battle: crop central authentic 240x160 area and fit to authentic 3:2 layout
        const int margin_left = (base_w_val - 240) / 2;
        out_src->x = margin_left * factor;
        out_src->y = 0;
        out_src->w = 240 * factor;
        out_src->h = 160 * factor;

        if (out_base_w) *out_base_w = 240;
        if (out_base_h) *out_base_h = 160;

        float s = std::min(static_cast<float>(win_w) / 240.0f, static_cast<float>(win_h) / 160.0f);
        int dw = static_cast<int>(240.0f * s);
        int dh = static_cast<int>(160.0f * s);
        out_dst->x = (win_w - dw) / 2;
        out_dst->y = (win_h - dh) / 2;
        out_dst->w = dw;
        out_dst->h = dh;
    } else {
        // Mode Native (3:2) everywhere
        out_src->x = 0;
        out_src->y = 0;
        out_src->w = tex_w;
        out_src->h = tex_h;

        if (out_base_w) *out_base_w = 240;
        if (out_base_h) *out_base_h = 160;

        float s = std::min(static_cast<float>(win_w) / 240.0f, static_cast<float>(win_h) / 160.0f);
        int dw = static_cast<int>(240.0f * s);
        int dh = static_cast<int>(160.0f * s);
        out_dst->x = (win_w - dw) / 2;
        out_dst->y = (win_h - dh) / 2;
        out_dst->w = dw;
        out_dst->h = dh;
    }
}

