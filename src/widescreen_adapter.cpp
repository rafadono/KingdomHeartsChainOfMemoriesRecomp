#include "widescreen_adapter.h"
#include "gbaimp/hud_anchoring.h"
#include "gbaimp/widescreen.h"
#include "gbaimp/widescreen_gbarecomp.h"
#include "gbaimp/affine_margin.h"
#include "gbaimp/obj_geometry.h"
#include "gbaimp/view_aspect.h"
#include "view_config.h"
#include "runtime_bus_bridge.h"
#include "gba/gba_bus.h"
#include "gba/gba_io.h"
#include "dialogue_backlog.h"
#include "gbaimp/m4a_music_hook.h"
#include "gbaimp_integration.h"
#include "text_overlay/engine_hooks.h"
#include "armv4t/runtime_arm.h"
#include <algorithm>
#include <atomic>

// Widescreen profile of Kingdom Hearts: Chain of Memories.
//
// Policy, view queries, the viewport, the presented-frame counter and the function-entry hook
// chain live in gbaimp::Widescreen (gbaimp/widescreen.h). This file only supplies what is specific
// to this game:
//   * what "wide context" means here: a battle arena (mode descriptors / mode functions below);
//   * the sprite provider that tells the battle HUD from the rest;
//   * the work done for every function entry (HD music trigger, battle tracking, overlay capture).

extern "C" uint32_t bus_read_u32(uint32_t addr);

namespace {
// True while the dialogue-overlay engine capture is chained in (see install_runtime_hooks). With
// the overlay off nothing of it runs on a function entry.
std::atomic<bool> s_overlay_hooked{false};

// Selected battle view (see widescreen_adapter.h).
std::atomic<int> s_battle_view{0};

// Real arena artwork for the extra rows (BG2 only: BG3 is a translucent overlay that the hardware blends
// with the other layers, a blend that margin rows do not apply).
gbaimp::AffineMarginRenderer& margin_renderer() {
    static gbaimp::AffineMarginRenderer r;
    return r;
}

gbaimp::PpuMemory live_ppu_memory(void*) {
    gbaimp::PpuMemory m;
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) return m;
    const uint8_t* io = bus->io().raw();
    m.io = io;
    m.dispcnt = static_cast<uint16_t>(io[0] | (io[1] << 8));
    m.vram = bus->vram_ptr();
    m.pal = bus->pal_ptr();
    return m;
}

// Battle HUD groups, told apart from the world by priority and position (btl_hpply.c, btl_hpenm.c and the
// card deck sprites). Text glyphs (16x16 square, priority 0) and dialogue boxes (priority 0) never qualify.
enum class Hud { None, PlayerHp, EnemyHp, Deck };

Hud classify_hud(uint16_t attr0, uint16_t attr1, uint16_t attr2) {
    const int sx = gbaimp::obj_signed_x(attr1);
    const int sy = gbaimp::obj_signed_y(attr0);
    const int priority = static_cast<int>((attr2 >> 10) & 0x03u);
    const bool is_16x16_square = ((attr0 & 0xC000u) == 0x0000u) && ((attr1 & 0xC000u) == 0x4000u);
    if (is_16x16_square || priority != 1) return Hud::None;
    if (sx >= -16 && sx <= 85 && sy >= -16 && sy <= 24) return Hud::PlayerHp;
    if (sx >= 160 && sx <= 250 && sy >= -16 && sy <= 24) return Hud::EnemyHp;
    // Card deck, bottom left: a column of 8..32 px pieces from x -18 to about 40 and y 100 to 160.
    if (sx >= -24 && sx <= 40 && sy >= 95 && sy <= 159) return Hud::Deck;
    return Hud::None;
}

inline void set_wide(bool battle) {
    gbaimp::Widescreen::instance().set_wide_context(battle);
}

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

// Pull side of the wide context: the authoritative active / pending mode descriptors in IWRAM
// (0x03007488 / 0x03007494). -1 (unknown) while neither is set yet (early boot, unit tests).
int poll_battle_context(void*) {
    const uint32_t active_mode = bus_read_u32(0x03007488u);
    const uint32_t pending_mode = bus_read_u32(0x03007494u);
    if (active_mode == 0 && pending_mode == 0) return -1;
    return (is_battle_mode(active_mode) || is_battle_mode(pending_mode)) ? 1 : 0;
}

// Push side: follows the game's mode switches from function entries.
void track_battle_mode(uint32_t pc) {
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
            set_wide(true);
        } else if (is_non_battle_mode(target_mode)) {
            set_wide(false);
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
        set_wide(true);  // True widescreen in battle arena (only if the 16:9 view is active)
    } else if (is_battle_exit_func || is_non_battle_func) {
        set_wide(false); // Authentic 3:2 for field, menus, cutscenes
    }
}

// Runs for every guest function entry (after the chain's previous hook).
void on_fn_entry(uint32_t pc, void*) {
    // HD music trigger (m4a song-start / stop entry points); no-op for every other pc.
    gbaimp::M4aMusicHook::instance().on_function_entry(pc, g_cpu.R[0]);
    track_battle_mode(pc);
    // Dialogue-overlay engine capture; chained only while the overlay is on.
    if (s_overlay_hooked.load(std::memory_order_relaxed)) khcom::overlay::engine::on_fn_entry(pc);
}

void configure_once() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    gbaimp::WidescreenProfile p;
    p.tilemap_provider = &gbaimp::default_tilemap_provider;
    // Dialogue windows and UI layers (BG0 / BG1) stay centred; the arena layers (BG2 / BG3) fill the wide view.
    p.bg_x_provider = &gbaimp::default_bg_x_provider;
    p.centered_bg_layers = 0x3u;
    p.obj_attr_x_provider = &khcom_obj_attr_x_provider;
    p.obj_attr_y_provider = &khcom_obj_attr_y_provider;
    p.margin_pixel_provider = &khcom_margin_pixel_provider;
    {
        gbaimp::AffineMarginOptions mo;
        mo.layer_mask = 0x04u;   // BG2
        margin_renderer().set_options(mo);
        margin_renderer().set_memory_source(&live_ppu_memory, nullptr);
    }
    p.bg_x_provider_layers = 0xFu;
    p.authored_margin_layers = 1;
    p.obj_native_clip = 0;
    p.pillarbox_outside_context = true;   // black pillars in 16:9 outside of a battle
    p.initial_wide_context = false;
    p.poll_context = &poll_battle_context;
    p.on_fn_entry = &on_fn_entry;
    gbaimp::Widescreen::instance().configure(gbaimp::gbarecomp_widescreen_bindings(), p);
}

// Game-side hooks that are needed in every view mode: the dialogue backlog bus read and the
// overlay capture switch. The overlay's engine state is reset when it is switched off.
void install_runtime_hooks() {
    khcom::khcom_install_backlog_hook();
    khcom::hd_music_setup();   // locates the m4a entry points once the cartridge is mapped
    const bool overlay = khcom::overlay::engine::wants_hook();
    if (s_overlay_hooked.load(std::memory_order_relaxed) != overlay) {
        s_overlay_hooked.store(overlay, std::memory_order_relaxed);
        if (!overlay) khcom::overlay::engine::reset();
    }
}
} // namespace

extern "C" {

int khcom_obj_attr_x_provider(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x) {
    gbaimp::Widescreen& ws = gbaimp::Widescreen::instance();
    if (!out_x || !ws.view_active()) {
        return 0;
    }

    const int raw_x = static_cast<int>(attr1 & 0x1FFu);
    const int sx = (raw_x >= 256) ? (raw_x - 512) : raw_x;

    // HUD anchoring is a consequence of the 16:9 presentation: only while the arena is shown in
    // the widened view are the HUD sprites moved out to the real screen edges. Everywhere else
    // (3:2 view, field, menus, cutscenes) sprites are never displaced.
    if (ws.wide_context_cached()) {
        // The shift equals the real extra width of each side (extra_left/extra_right), so the
        // authored margin between HUD and screen edge stays exactly what the 3:2 game has: a sprite
        // at sx in the original 240px frame lands at (sx - extra_left) in the widened frame's
        // coordinates, i.e. at distance sx from the real left edge of the screen (likewise right).
        //
        // Player HP gauge and face portrait (priority 1, region sx in [-16, 85], sy in [-16, 24]) go to the
        // left edge; the enemy HP gauge (priority 1, sx in [160, 250], same rows) to the right edge.
        // Dialogue windows and tutorial boxes (priority 0) are never displaced.
        switch (classify_hud(attr0, attr1, attr2)) {
            case Hud::PlayerHp: *out_x = sx - ws.extra_left(); return 1;
            case Hud::EnemyHp:  *out_x = sx + ws.extra_right(); return 1;
            default: break;
        }
    }

    // Everything else: the generic behaviour (sprites parked at raw X >= 256 get their signed X).
    return gbaimp::default_obj_attr_x_provider(oam_index, attr0, attr1, attr2, out_x);
}

}

extern "C" {

// Vertical counterpart: anchors the HUD to the top / bottom edge of the taller view and lets world sprites
// overhang the native band into the extra rows.
int khcom_obj_attr_y_provider(int, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_y) {
    gbaimp::Widescreen& ws = gbaimp::Widescreen::instance();
    if (!out_y || !ws.presentation_active() || !ws.has_vertical_extent()) return 0;
    if (gbaimp::obj_disabled(attr0)) return 0;
    const int sy = gbaimp::obj_signed_y(attr0);
    switch (classify_hud(attr0, attr1, attr2)) {
        case Hud::PlayerHp:
        case Hud::EnemyHp: *out_y = sy - ws.extra_top(); return 1;
        case Hud::Deck:    *out_y = sy + ws.extra_bottom(); return 1;
        default: break;
    }
    // World sprites: only those that really reach into the native band may overhang it. A sprite parked
    // above the screen (raw Y wraps to a negative value) stays hidden.
    int w = 0, h = 0;
    gbaimp::obj_bounds(attr0, attr1, &w, &h);
    if (sy + h > 0) { *out_y = sy; return 1; }
    return 0;
}

// Arena artwork for the extra rows (affine BG2 of the battle arena).
const gbaimp::WsMarginPixel* khcom_margin_pixel_provider(int screen_y, int* left, int* width) {
    gbaimp::Widescreen& ws = gbaimp::Widescreen::instance();
    if (!ws.presentation_active() || !ws.has_vertical_extent()) return nullptr;
    return margin_renderer().provide(screen_y, ws.extra_left(), ws.extra_right(), left, width);
}

void khcom_set_vertical_extras(uint32_t top, uint32_t bottom) {
    gbaimp::gbarecomp_set_vertical_extras(top, bottom);
}

void khcom_set_battle_view(int mode) {
    s_battle_view.store(std::clamp(mode, 0, 4), std::memory_order_relaxed);
}

int khcom_get_battle_view() { return s_battle_view.load(std::memory_order_relaxed); }

void khcom_view_geometry(gbarecomp::ViewGeometry* g, int drawable_w, int drawable_h) {
    if (!g) return;
    gbaimp::AspectSettings a;
    a.max_view_width = gbaimp::kDefaultMaxViewWidth;
    bool apply = false;
    switch (s_battle_view.load(std::memory_order_relaxed)) {
        case 1:   // follow the window; only when the runtime reports one (the fixed views have none)
            a.preset = gbaimp::AspectPreset::Adaptive;
            apply = drawable_w > 0 && drawable_h > 0;
            break;
        case 2: a.preset = gbaimp::AspectPreset::Standard4x3; apply = true; break;
        case 3: a.preset = gbaimp::AspectPreset::Standard5x4; apply = true; break;
        case 4: a.preset = gbaimp::AspectPreset::Console10x7; apply = true; break;
        default: break;
    }
    if (apply) {
        const gbaimp::ViewSize v = gbaimp::resolve_view_size(a, drawable_w, drawable_h);
        int l = 0, r = 0, t = 0, b = 0;
        gbaimp::split_extra(v.width, &l, &r);
        gbaimp::split_extra_rows(v.height, &t, &b);
        g->width = static_cast<uint32_t>(v.width);
        g->height = static_cast<uint32_t>(v.height);
        g->extra_left = static_cast<uint32_t>(l);
        g->extra_right = static_cast<uint32_t>(r);
        g->extra_top = static_cast<uint32_t>(t);
        g->extra_bottom = static_cast<uint32_t>(b);
    } else {
        // Original 160 rows: whatever the runtime asked for, no extra rows.
        g->height = 160;
        g->extra_top = 0;
        g->extra_bottom = 0;
    }
    gbaimp::gbarecomp_set_vertical_extras(g->extra_top, g->extra_bottom);
}

void khcom_configure_widescreen() { configure_once(); }

void khcom_install_widescreen_adapter(uint32_t extra_left, uint32_t extra_right) {
    (void)extra_left;
    (void)extra_right;
    configure_once();
    // The providers are (un)installed by the library according to the live view state; the hooks
    // (battle tracking, backlog, music) are needed in every view mode.
    gbaimp::Widescreen::instance().install();
    install_runtime_hooks();
}

void khcom_update_widescreen_state() {
    configure_once();
    gbaimp::Widescreen::instance().update();
    install_runtime_hooks();
}

bool khcom_is_battle_active() {
    configure_once();
    return gbaimp::Widescreen::instance().wide_context();
}

void khcom_set_battle_active(bool active) {
    configure_once();
    set_wide(active);
}

}
