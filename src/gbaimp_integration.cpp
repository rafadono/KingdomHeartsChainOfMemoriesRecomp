#include "gbaimp_integration.h"
#include "gbaimp/hooks.h"
#include "gbaimp/hud_anchoring.h"
#include "dialogue_backlog.h"
#include "game_config.h"
#include "gbaimp/widescreen.h"

namespace khcom {

void register_gbaimp_integration() {
    auto& h = gbaimp::hooks();
    h.save_settings = [] { save_khcom_config(); };
    h.is_input_blocked = [] {
        return is_runtime_menu_open() || DialogueBacklog::instance().is_open();
    };
    h.frame_counter = [] { return gbaimp::Widescreen::instance().frame_counter(); };

    auto& anchoring = gbaimp::HudAnchoring::instance();
    anchoring.clear_zones();

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

} // namespace khcom
