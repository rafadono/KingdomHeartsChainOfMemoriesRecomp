#pragma once

namespace khcom {

// Wires the reusable gba-improvements library (namespace gbaimp) to this game:
//  - gbaimp::hooks(): save_settings, is_input_blocked, frame_counter
//  - HUD anchoring zones (health bar top-left, card deck bottom-right)
// Idempotent: safe to call more than once (zones are reset and re-registered,
// so exactly two zones exist afterwards).
void register_gbaimp_integration();

} // namespace khcom
