#include "hud_anchoring.h"
#include <algorithm>

namespace khcom {

HudAnchoring& HudAnchoring::instance() {
    static HudAnchoring s_instance;
    return s_instance;
}

void HudAnchoring::set_mode(HudAnchorMode mode) {
    settings_.mode = mode;
}

void HudAnchoring::set_horizontal_offset(int offset) {
    settings_.horizontal_offset = std::clamp(offset, 0, 96);
}

void HudAnchoring::adjust_sprite_coordinate(int& x, int& y, int sprite_width, int sprite_height, bool is_hud) {
    if (settings_.mode != HudAnchorMode::WidescreenAnchored) {
        return;
    }

    // Health bar region (top-left)
    if (x <= 90 && y <= 45) {
        x -= settings_.horizontal_offset;
    }
    // Card deck / active deck selection (bottom-right)
    else if (x >= 130 && y >= 95) {
        x += settings_.horizontal_offset;
    }
}

void HudAnchoring::process_oam_entry(uint16_t& attr0, uint16_t& attr1) {
    if (settings_.mode != HudAnchorMode::WidescreenAnchored) {
        return;
    }

    int y = attr0 & 0x00FF;
    int x = attr1 & 0x01FF;

    // Wrap-around handling for offscreen negative coordinates in GBA OAM
    if (x >= 256) x -= 512;
    if (y >= 160) y -= 256;

    // Top-left HUD (HP gauge, boss bar)
    if (x >= 0 && x <= 85 && y >= 0 && y <= 40) {
        x = std::max(-40, x - settings_.horizontal_offset);
        attr1 = (attr1 & ~0x01FF) | (static_cast<uint16_t>(x) & 0x01FF);
    }
    // Bottom-right HUD (Card deck, reload counter)
    else if (x >= 135 && x <= 240 && y >= 100 && y <= 160) {
        x = std::min(300, x + settings_.horizontal_offset);
        attr1 = (attr1 & ~0x01FF) | (static_cast<uint16_t>(x) & 0x01FF);
    }
}

} // namespace khcom
