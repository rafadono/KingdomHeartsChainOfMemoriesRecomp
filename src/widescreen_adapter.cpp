#include "widescreen_adapter.h"
#include "hud_anchoring.h"
#include <algorithm>

extern "C" {
extern int (*g_ws_tilemap_provider)(int bg, int hw_x, int screen_y, uint16_t* out_entry);
extern int (*g_ws_bg_x_provider)(int bg, int output_x, int screen_y, int* out_hw_x);
extern unsigned g_ws_bg_x_provider_layers;
extern int (*g_ws_obj_attr_x_provider)(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x);
extern int g_ws_authored_margin_layers;
extern int g_ws_pillarbox;
extern int g_ws_pillarbox_left;
extern int g_ws_pillarbox_right;
extern unsigned g_ws_active;
extern unsigned g_ws_extra;
extern unsigned g_ws_extra_left;
extern unsigned g_ws_extra_right;
extern unsigned g_ws_view_width;
}

namespace {
constexpr int kWsTilemapUnavailable = 0;
}

int khcom_tilemap_provider(int bg, int hw_x, int screen_y, uint16_t* out_entry) {
    (void)bg;
    (void)hw_x;
    (void)screen_y;
    (void)out_entry;
    return kWsTilemapUnavailable;
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

    if (bg == 0) {
        if (in_margin) {
            return -1;
        }
        return 0;
    }

    if (bg == 1) {
        const auto anchor_mode = khcom::HudAnchoring::instance().settings().mode;
        if (anchor_mode == khcom::HudAnchorMode::WidescreenAnchored) {
            if (in_left_margin && screen_y < 48) {
                *out_hw_x = std::clamp(output_x, 0, 80);
                return 1;
            }
            if (in_right_margin && screen_y >= 96) {
                *out_hw_x = std::clamp(output_x - extra, 140, 239);
                return 1;
            }
            if (in_margin) {
                return -1;
            }
            if (left > 0 && output_x < left + 80 && screen_y < 48) {
                return -1;
            }
            if (right > 0 && output_x >= left + 140 && screen_y >= 96) {
                return -1;
            }
            return 0;
        }

        if (in_margin) {
            return -1;
        }
        return 0;
    }

    if (bg == 2 || bg == 3) {
        if (in_left_margin) {
            *out_hw_x = 0;
            return 1;
        }
        if (in_right_margin) {
            *out_hw_x = 239;
            return 1;
        }
        return 0;
    }

    return 0;
}

int khcom_obj_attr_x_provider(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x) {
    (void)oam_index;
    (void)attr2;
    if (!out_x || !g_ws_active) {
        return 0;
    }

    const int raw_x = static_cast<int>(attr1 & 0x1FFu);
    const int sx = (raw_x >= 256) ? (raw_x - 512) : raw_x;
    const int raw_y = static_cast<int>(attr0 & 0xFFu);
    const int sy = (raw_y >= 160) ? (raw_y - 256) : raw_y;

    const auto anchor_mode = khcom::HudAnchoring::instance().settings().mode;
    if (anchor_mode == khcom::HudAnchorMode::WidescreenAnchored) {
        if (sx >= -16 && sx <= 85 && sy >= 0 && sy <= 45) {
            *out_x = sx - static_cast<int>(g_ws_extra_left);
            return 1;
        }
        if (sx >= 130 && sx <= 250 && sy >= 90 && sy <= 160) {
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
    g_ws_pillarbox = 0;
}

void khcom_update_widescreen_state() {
    g_ws_tilemap_provider = &khcom_tilemap_provider;
    g_ws_bg_x_provider = &khcom_bg_x_provider;
    g_ws_bg_x_provider_layers = 0xFu;
    g_ws_obj_attr_x_provider = &khcom_obj_attr_x_provider;
    g_ws_authored_margin_layers = 1;
    if (g_ws_active && g_ws_view_width > 240) {
        g_ws_pillarbox = 0;
    }
}
