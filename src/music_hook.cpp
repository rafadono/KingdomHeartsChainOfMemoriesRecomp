#include "music_hook.h"

#include "gbaimp/hd_audio_player.h"
#include "armv4t/runtime_arm.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" uint32_t bus_read_u32(uint32_t addr);

namespace khcom {
namespace {

std::atomic<uint64_t> s_starts{0};
std::atomic<uint64_t> s_mapped{0};
std::atomic<uint64_t> s_stops{0};
uint32_t s_hd_player = 0xFFFFFFFFu;   // player index the current HD track was started for
uint32_t (*s_reader)(uint32_t) = nullptr;

bool log_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("KHCOM_LOG_INTERPOSE");
        return v && *v && std::strcmp(v, "0") != 0;
    }();
    return on;
}

uint32_t read_song_player(uint32_t song_id) {
    if (s_reader) return s_reader(song_id);
    if (song_id >= kM4aSongTableEntries) return 0xFFFFFFFFu;
    // Entry = { u32 header; u16 ms; u16 me }: the player index is the low half of word 1.
    return bus_read_u32(kM4aSongTable + song_id * 8u + 4u) & 0xFFFFu;
}

void log_event(const char* what, uint32_t id, uint32_t player, const char* decision) {
    if (!log_enabled()) return;
    static std::atomic<int> s_lines{0};
    if (s_lines.fetch_add(1, std::memory_order_relaxed) >= 40) return;   // low-noise: first events only
    std::fprintf(stderr, "[khcom] m4a %s id=%u player=%u -> %s\n", what, id, player, decision);
}

void stop_hd(const char* why, uint32_t id, uint32_t player) {
    gbaimp::HdAudioPlayer::instance().stop_music();
    s_hd_player = 0xFFFFFFFFu;
    s_stops.fetch_add(1, std::memory_order_relaxed);
    log_event(why, id, player, "HD stop");
}

} // namespace

void music_on_song_start(uint32_t song_id, uint32_t player) {
    s_starts.fetch_add(1, std::memory_order_relaxed);
    auto& hd = gbaimp::HdAudioPlayer::instance();
    if (!hd.settings().enabled) return;

    if (hd.has_track(song_id)) {
        s_mapped.fetch_add(1, std::memory_order_relaxed);
        hd.trigger_song(song_id);
        const bool playing = hd.current_song_id() == song_id;
        if (playing) s_hd_player = player;
        log_event("start", song_id, player, playing ? "HD trigger (playing)" : "HD trigger (file unavailable, silent)");
        return;
    }
    if (hd.current_song_id() != 0 && player == s_hd_player) {
        stop_hd("start", song_id, player);   // unmapped song replaced the music the HD track belongs to
        return;
    }
    log_event("start", song_id, player, "native only");
}

void music_on_song_stop(uint32_t song_id) {
    auto& hd = gbaimp::HdAudioPlayer::instance();
    if (hd.current_song_id() != 0 && hd.current_song_id() == song_id) stop_hd("stop", song_id, s_hd_player);
}

void music_on_all_stop() {
    auto& hd = gbaimp::HdAudioPlayer::instance();
    if (hd.current_song_id() != 0) stop_hd("all-stop", 0, s_hd_player);
}

void music_on_fn_entry(uint32_t pc) {
    // Fast reject: all five entry points live in one 0x140-byte window of the m4a library.
    if (pc < kM4aSongNumStart || pc > kM4aMPlayAllStop) return;
    switch (pc) {
        case kM4aSongNumStart:
        case kM4aSongNumStartOrChange:
        case kM4aSongNumStartOrContinue: {
            const uint32_t id = g_cpu.R[0] & 0xFFFFu;   // u16 argument (the callee masks it too)
            music_on_song_start(id, read_song_player(id));
            break;
        }
        case kM4aSongNumStop:
            music_on_song_stop(g_cpu.R[0] & 0xFFFFu);
            break;
        case kM4aMPlayAllStop:
            music_on_all_stop();
            break;
        default:
            break;
    }
}

void music_set_song_player_reader(uint32_t (*reader)(uint32_t)) { s_reader = reader; }

MusicHookStats music_hook_stats() {
    MusicHookStats s;
    s.starts = s_starts.load(std::memory_order_relaxed);
    s.mapped_triggers = s_mapped.load(std::memory_order_relaxed);
    s.hd_stops = s_stops.load(std::memory_order_relaxed);
    return s;
}

void music_hook_reset_for_tests() {
    s_starts = 0;
    s_mapped = 0;
    s_stops = 0;
    s_hd_player = 0xFFFFFFFFu;
}

} // namespace khcom
