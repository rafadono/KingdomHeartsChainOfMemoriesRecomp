#pragma once

#include <cstdint>

namespace khcom {

enum class HudAnchorMode : int {
    OriginalCentered = 0,
    WidescreenAnchored = 1
};

struct HudAnchorSettings {
    HudAnchorMode mode = HudAnchorMode::WidescreenAnchored;
    int horizontal_offset = 32;
    int vertical_offset = 0;
};

class HudAnchoring {
public:
    static HudAnchoring& instance();

    const HudAnchorSettings& settings() const { return settings_; }
    HudAnchorSettings& settings() { return settings_; }

    void set_mode(HudAnchorMode mode);
    void set_horizontal_offset(int offset);

    // Adjusts sprite coordinate (x, y) if it belongs to HUD elements
    void adjust_sprite_coordinate(int& x, int& y, int sprite_width, int sprite_height, bool is_hud);

    // Repositions OAM attributes during rendering if HUD anchoring is active
    void process_oam_entry(uint16_t& attr0, uint16_t& attr1);

private:
    HudAnchoring() = default;

    HudAnchorSettings settings_;
};

} // namespace khcom
