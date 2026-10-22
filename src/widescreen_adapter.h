#pragma once

#include <cstdint>
#include "gbaimp/widescreen.h"

namespace gbarecomp { struct ViewGeometry; }

/**
 * Widescreen profile for Kingdom Hearts: Chain of Memories.
 *
 * The generic part (view queries, policy, viewport, frame counter, function-entry hook chain) is
 * gbaimp::Widescreen (gbaimp/widescreen.h). What remains here is game specific: battle detection
 * from the game's mode descriptors, the HUD-aware sprite provider and the per-function-entry work.
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
 *
 * Other code reads the view state straight from the library: gbaimp::Widescreen::instance()
 * .view_active() / presentation_active() / effective_extra_pixels() / frame_counter().
 */

extern "C" {
// Sprite provider of the profile: moves the battle HUD to the screen edges, everything else
// follows the library default.
int khcom_obj_attr_x_provider(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x);
// Vertical counterpart of the sprite provider: HUD to the top / bottom edge, overhanging world sprites.
int khcom_obj_attr_y_provider(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_y);
// Arena artwork for the extra rows of a taller view (see gbaimp/affine_margin.h).
const gbaimp::WsMarginPixel* khcom_margin_pixel_provider(int screen_y, int* left, int* width);
// Battle view selection (video.battle_view): 0 = original 160 rows, 1 = follow the window (adaptive
// view only), 2 = 4:3 (240x180), 3 = 5:4 (240x192), 4 = 10:7 (320x224).
void khcom_set_battle_view(int mode);
int khcom_get_battle_view();
// RunOptions::view_geometry: applies the selected battle view to the runtime geometry and publishes the
// extra rows to the widescreen module.
void khcom_view_geometry(gbarecomp::ViewGeometry* geometry, int drawable_width, int drawable_height);
// Per-frame sync of the extra rows from the runtime (ExtendedViewFrameInfo).
void khcom_set_vertical_extras(uint32_t top, uint32_t bottom);
// Configures the library's Widescreen instance with this game's profile (idempotent). Called by the
// entry points below and early from the SDL interposer install, before any frame is presented.
void khcom_configure_widescreen();
// Extended-view init callback of the runtime: policy + the game-side hooks.
void khcom_install_widescreen_adapter(uint32_t extra_left, uint32_t extra_right);
// Per-frame / per-poll refresh (context poll, policy, hooks).
void khcom_update_widescreen_state();
// True while the game is in a battle arena (polls the game's mode words).
bool khcom_is_battle_active();
void khcom_set_battle_active(bool active);
}
