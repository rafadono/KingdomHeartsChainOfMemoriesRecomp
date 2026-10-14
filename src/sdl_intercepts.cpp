// SDL interception glue for Kingdom Hearts: Chain of Memories.
//
// The generic handlers (game image detection, colour correction, letterboxing, screen mask,
// frame interpolation, performance HUD, audio) live in the gba-improvements library
// (gbaimp/game_host.h). This file only supplies what is specific to this game and installs it:
//   * Linux/GNU ld: GBAIMP_DEFINE_LD_WRAPS() below generates the `__wrap_SDL_X` shims for the
//     `-Wl,--wrap=SDL_X` flags in CMakeLists.txt; the handlers are registered from a static
//     initializer.
//   * Windows/MSVC: main() calls khcom_install_sdl_interposers(), which patches the Import Address
//     Table of the executable (and loaded modules) to the same handlers.
#include "gbaimp/game_host.h"
#include "capture_harness.h"
#include "dialogue_backlog.h"
#include "music_hook.h"
#include "text_overlay/engine_hooks.h"
#include "ram_overlay_dispatch.h"
#include "widescreen_adapter.h"

#include <cstdio>

#if !defined(_WIN32)
// Linux/GNU ld: __wrap_SDL_X shims + real table (see CMakeLists.txt for the link flags).
GBAIMP_DEFINE_LD_WRAPS()
#endif

namespace {

void on_poll() {
    khcom_update_widescreen_state();
    khcom_install_ram_dispatch();
}

bool on_event(SDL_Event& event) {
    return khcom::DialogueBacklog::instance().handle_event(event);
}

bool wants_native_frame() { return khcom::capture::active(); }

void on_native_frame(const void* pixels, int pitch, int w, int h) {
    khcom::capture::on_native_frame(pixels, pitch, w, h);   // raw PPU framebuffer
}

// Output-resolution dialogue overlay (engine side). It is drawn right after the game image, NOT at
// present time: gbarecomp draws its host layers (the Esc settings menu, touch pad, ...) after the
// image copy and before the present, so text drawn here stays UNDER the menu. Returns immediately
// (one branch) unless the overlay is enabled.
void after_game_image(SDL_Renderer* renderer, const SDL_Rect& dst) {
    khcom::overlay::engine::on_present(renderer, dst);
}

void render_present_overlay(SDL_Renderer* renderer, int win_w, int win_h) {
    khcom::DialogueBacklog::instance().render_sidebar(renderer, win_w, win_h);
}

void before_present(SDL_Renderer* renderer) {
    if (khcom::capture::active()) khcom::capture::on_before_present(renderer);
}

void print_stats(const char* tag) {
    const khcom::MusicHookStats m = khcom::music_hook_stats();
    std::fprintf(stderr,
                 "[%s] m4a song hook: starts=%llu hd_mapped=%llu hd_stops=%llu\n", tag,
                 static_cast<unsigned long long>(m.starts),
                 static_cast<unsigned long long>(m.mapped_triggers),
                 static_cast<unsigned long long>(m.hd_stops));
}

gbaimp::GameHostConfig make_config() {
    gbaimp::GameHostConfig c;
    c.tag = "khcom";
    c.log_env = "KHCOM_LOG_INTERPOSE";
    c.on_poll = &on_poll;
    c.on_event = &on_event;
    c.wants_native_frame = &wants_native_frame;
    c.on_native_frame = &on_native_frame;
    c.compute_viewport = &khcom_compute_effective_viewport;
    c.after_game_image = &after_game_image;
    c.on_present_begin = &khcom_widescreen_notify_present;
    c.render_present_overlay = &render_present_overlay;
    c.before_present = &before_present;
    c.print_stats = &print_stats;
    return c;
}

} // namespace

// Registers the handlers with the gba-improvements interposition layer and, on Windows, patches
// the import tables. Idempotent. Must run before the runtime starts calling SDL (call it first
// thing in main). On Linux the `--wrap` link step is the mechanism, so only the handler
// registration happens (it also runs from a static initializer, so it does not depend on main()
// calling it). Returns true when import-table patching is active.
extern "C" bool khcom_install_sdl_interposers() {
    return gbaimp::install_game_host(make_config());
}

#if !defined(_WIN32)
namespace {
const bool s_handlers_registered = (khcom_install_sdl_interposers(), true);
}
#endif
