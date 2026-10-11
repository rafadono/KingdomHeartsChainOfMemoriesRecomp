#pragma once
// HD music trigger: follows the game's own MusicPlayer2000 ("m4a") song calls.
//
// The ROM links the stock Nintendo m4a library. Its public entry points were identified by
// disassembling the ROM (see docs/GBA_IMPROVEMENTS_INTEGRATION.md for the evidence):
//
//   0x0811FE70  m4aSongNumStart(u16 n)            R0 = song id
//   0x0811FE9C  m4aSongNumStartOrChange(u16 n)    R0 = song id
//   0x0811FEE8  m4aSongNumStartOrContinue(u16 n)  R0 = song id
//   0x0811FF3C  m4aSongNumStop(u16 n)             R0 = song id
//   0x0811FFA4  m4aMPlayAllStop()
//
// gSongTable is at 0x09D6F744 (8-byte entries: header pointer, u16 player index "ms", u16 "me")
// and gMPlayTable at 0x09D6F60C (12-byte entries). The song id is the index into gSongTable;
// the player index says which of the game's 26 music players runs it.
//
// The hook is driven from the function-entry hook that widescreen_adapter.cpp already owns and
// chains (gbarecomp resets g_runtime_fn_entry_hook at every run start, so there must be exactly
// one owner that re-installs itself); it runs on the emulation thread.
//
// What it does with gbaimp::HdAudioPlayer:
//   * a started song that has an entry in config/music_tracks.ini -> trigger_song(id)
//   * a started song WITHOUT an entry on the same player that the HD track was started for
//     (the game replaced that music) -> stop_music()
//   * m4aSongNumStop of the HD song / m4aMPlayAllStop -> stop_music()
//   * anything else (sound effects on other players, unmapped ids) leaves the HD track alone.
// The original MP2K music keeps playing underneath: see the documented limitation.
#include <cstdint>

namespace khcom {

constexpr uint32_t kM4aSongNumStart = 0x0811FE70u;
constexpr uint32_t kM4aSongNumStartOrChange = 0x0811FE9Cu;
constexpr uint32_t kM4aSongNumStartOrContinue = 0x0811FEE8u;
constexpr uint32_t kM4aSongNumStop = 0x0811FF3Cu;
constexpr uint32_t kM4aMPlayAllStop = 0x0811FFA4u;
constexpr uint32_t kM4aSongTable = 0x09D6F744u;
constexpr uint32_t kM4aSongTableEntries = 962u;   // entries until the first invalid header pointer

struct MusicHookStats {
    uint64_t starts = 0;            // m4aSongNum*Start* calls seen
    uint64_t mapped_triggers = 0;   // of those, ids that have a mapping (trigger_song was called)
    uint64_t hd_stops = 0;          // times the HD track was stopped by the hook
};

// Function-entry dispatch: cheap for every pc that is not one of the addresses above.
void music_on_fn_entry(uint32_t pc);

// The decisions, separated from the CPU so they can be unit tested.
void music_on_song_start(uint32_t song_id, uint32_t player);
void music_on_song_stop(uint32_t song_id);
void music_on_all_stop();

// Player index of `song_id` (gSongTable[id].ms), or 0xFFFFFFFF when the id is out of range.
// Test seam: replace the ROM reader (nullptr restores the bus read).
void music_set_song_player_reader(uint32_t (*reader)(uint32_t song_id));

MusicHookStats music_hook_stats();
void music_hook_reset_for_tests();

} // namespace khcom
