#pragma once

#include <cstdint>

extern "C" {
int khcom_tilemap_provider(int bg, int hw_x, int screen_y, uint16_t* out_entry);
int khcom_bg_x_provider(int bg, int output_x, int screen_y, int* out_hw_x);
int khcom_obj_attr_x_provider(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x);
void khcom_install_widescreen_adapter(uint32_t extra_left, uint32_t extra_right);
void khcom_update_widescreen_state();
}
