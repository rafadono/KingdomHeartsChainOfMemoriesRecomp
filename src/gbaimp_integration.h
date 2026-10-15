#pragma once

#include <cstdint>

namespace khcom {

// Wires the reusable gba-improvements library (namespace gbaimp) to this game:
//  - gbaimp::hooks(): save_settings, is_input_blocked, frame_counter
//  - HUD anchoring zones (health bar top-left, card deck bottom-right)
// Idempotent: safe to call more than once (zones are reset and re-registered,
// so exactly two zones exist afterwards).
void register_gbaimp_integration();

// HD music trigger (gbaimp::M4aMusicHook): follows the game's own m4a song calls and drives
// gbaimp::HdAudioPlayer (mapping in config/music_tracks.ini). The hook is fed from the function-entry
// hook of the widescreen profile (see widescreen_adapter.cpp).
//
// The m4a entry points are found in the cartridge image by their code shape; if the locator does not
// recognise them the addresses below (checked against the USA ROM) are used.
constexpr uint32_t kM4aSongNumStart = 0x0811FE70u;
constexpr uint32_t kM4aSongNumStartOrChange = 0x0811FE9Cu;
constexpr uint32_t kM4aSongNumStartOrContinue = 0x0811FEE8u;
constexpr uint32_t kM4aSongNumStop = 0x0811FF3Cu;
constexpr uint32_t kM4aMPlayAllStop = 0x0811FFA4u;
constexpr uint32_t kM4aSongTable = 0x09D6F744u;   // 8-byte entries {header, u16 player, u16 me}
constexpr uint32_t kM4aSongTableEntries = 962u;   // entries until the first invalid header pointer

// Configures the hook once the cartridge is mapped (locator, bus reader, logging). Idempotent and cheap
// once done; returns immediately while no cartridge is available yet.
void hd_music_setup();

// "[tag] m4a song hook: starts=... hd_mapped=... hd_stops=..." (the host's diagnostics line).
void hd_music_print_stats(const char* tag);

} // namespace khcom
