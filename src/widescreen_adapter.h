#pragma once

#include <cstdint>
#include <SDL.h>

/**
 * Adaptive Widescreen Architecture for Kingdom Hearts: Chain of Memories
 * 
 * Note on 16:9 Scope:
 * True 16:9 widescreen expansion (284x160) is active exclusively during combat arenas:
 * 1. Battles utilize continuous cylindrical 360-degree background tilemaps (BG2/BG3)
 *    in VRAM that wrap horizontally, revealing real arena geometry without visual voids.
 * 2. Room exploration (overworld) maps are strictly authored for 240x160 with viewport-edge
 *    entity culling. Expanding the horizontal view exposes unfinished map voids, tile
 *    glitches, and spawning Heartless enemies.
 * 3. Menus, UI dialogs, and story cutscenes are choreographed for 240x160. Clean 3:2
 *    pillarboxing preserves exact 1:1 square pixel geometry and prevents sprite distortion.
 */

extern "C" {
int khcom_tilemap_provider(int bg, int hw_x, int screen_y, uint16_t* out_entry);
int khcom_bg_x_provider(int bg, int output_x, int screen_y, int* out_hw_x);
int khcom_obj_attr_x_provider(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x);
void khcom_install_widescreen_adapter(uint32_t extra_left, uint32_t extra_right);
void khcom_update_widescreen_state();
bool khcom_is_battle_active();
void khcom_set_battle_active(bool active);
void khcom_widescreen_notify_present();

// ---- View state queries (derived from the live host view; no cached copy) ----------------------
// True while the host presents the expanded 16:9 view (in-game view mode "16:9"). False in the
// native 3:2 view, in which no widescreen behaviour of any kind may apply.
bool khcom_widescreen_view_active();
// True only while the widened picture is really shown: 16:9 view AND a battle arena. This is also
// exactly when the HUD is anchored to the screen edges (HUD anchoring is not a separate option).
bool khcom_widescreen_presentation_active();
// Extra horizontal pixels currently shown beside the 240px play area: the real extra width of each
// side while presentation is active, 0/0 in 3:2, outside battle or in menus/cutscenes.
void khcom_effective_extra_pixels(int* left, int* right);
uint64_t khcom_get_frame_counter();
void khcom_compute_effective_viewport(int tex_w, int tex_h, int win_w, int win_h,
                                      SDL_Rect* out_src, SDL_Rect* out_dst,
                                      int* out_base_w, int* out_base_h);
}

