#include "gbaimp_integration.h"
#include "gbaimp/hooks.h"
#include "gbaimp/hud_anchoring.h"
#include "gbaimp/text_history.h"
#include "game_config.h"
#include "gbaimp/widescreen.h"
#include "gbaimp/m4a_music_hook.h"
#include "armv4t/runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "gba/gba_bus.h"

#include <cstdio>

namespace khcom {

void register_gbaimp_integration() {
    auto& h = gbaimp::hooks();
    h.save_settings = [] { save_khcom_config(); };
    h.is_input_blocked = [] {
        return is_runtime_menu_open() || gbaimp::TextHistory::instance().is_open();
    };
    h.frame_counter = [] { return gbaimp::Widescreen::instance().frame_counter(); };

    auto& anchoring = gbaimp::HudAnchoring::instance();
    anchoring.clear_zones();

    // Conversation log: the library defaults (panel, colors, captions, F2 / Back, 200 entries) are
    // this game's look; only the two named speakers get their own color.
    gbaimp::TextHistoryStyle& style = gbaimp::TextHistory::instance().config().style;
    style.speaker_colors = {{"Sora", {56, 189, 248, 255}}, {"Marluxia", {244, 114, 182, 255}}};

    // Health bar region (top-left): pushed to the left edge of the widescreen view.
    gbaimp::HudZone health;
    health.direction = -1;
    health.coord_x_max = 90;
    health.coord_y_max = 45;
    health.oam_x_min = 0;
    health.oam_x_max = 85;
    health.oam_y_min = 0;
    health.oam_y_max = 40;
    health.oam_clamp_enabled = true;
    health.oam_clamp_x = -40;
    anchoring.add_zone(health);

    // Card deck / active deck selection (bottom-right): pushed to the right edge.
    gbaimp::HudZone deck;
    deck.direction = +1;
    deck.coord_x_min = 130;
    deck.coord_y_min = 95;
    deck.oam_x_min = 135;
    deck.oam_x_max = 240;
    deck.oam_y_min = 100;
    deck.oam_y_max = 160;
    deck.oam_clamp_enabled = true;
    deck.oam_clamp_x = 300;
    anchoring.add_zone(deck);
}

void hd_music_setup() {
    static bool s_done = false;
    if (s_done) return;
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus || !bus->rom_ptr() || bus->rom_size() == 0) return;   // not mapped yet: try again at the next call
    s_done = true;

    gbaimp::M4aMusicHook& hook = gbaimp::M4aMusicHook::instance();
    hook.set_log_tag("khcom");
    hook.set_logging_from_env("KHCOM_");   // KHCOM_LOG_INTERPOSE=1 prints the first song starts
    if (!hook.locate(bus->rom_ptr(), bus->rom_size())) {
        gbaimp::M4aAddresses a;
        a.song_num_start = kM4aSongNumStart;
        a.song_num_start_or_change = kM4aSongNumStartOrChange;
        a.song_num_start_or_continue = kM4aSongNumStartOrContinue;
        a.song_num_stop = kM4aSongNumStop;
        a.mplay_all_stop = kM4aMPlayAllStop;
        a.song_table = kM4aSongTable;
        a.song_table_entries = kM4aSongTableEntries;
        hook.set_addresses(a);
    }
    hook.set_bus_reader(&bus_read_u32);   // song table lookups through the guest bus
}

void hd_music_print_stats(const char* tag) {
    const gbaimp::M4aHookStats m = gbaimp::M4aMusicHook::instance().stats();
    std::fprintf(stderr, "[%s] m4a song hook: starts=%llu hd_mapped=%llu hd_stops=%llu\n", tag,
                 static_cast<unsigned long long>(m.starts),
                 static_cast<unsigned long long>(m.mapped_triggers),
                 static_cast<unsigned long long>(m.hd_stops));
}

} // namespace khcom
