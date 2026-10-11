// SDL interception glue for Kingdom Hearts: Chain of Memories.
//
// The reusable gba-improvements library (namespace gbaimp) provides the generic SDL
// interposition mechanism (gbaimp/sdl_interpose.h); this file holds the game-specific
// handlers and registers them with it:
//   * Linux/GNU ld: GBAIMP_DEFINE_LD_WRAPS() below generates the `__wrap_SDL_X`
//     shims for the `-Wl,--wrap=SDL_X` flags in CMakeLists.txt; the handlers are
//     registered from a static initializer.
//   * Windows/MSVC: main() calls khcom_install_sdl_interposers(), which patches the
//     Import Address Table of the executable (and loaded modules) to the same handlers.
// The handlers call the original SDL ONLY through gbaimp::sdl_real(), so they are
// identical on both platforms. They route into the library plus the KH-specific
// systems (widescreen adapter, dialogue backlog, RAM overlay dispatch).
#include "gbaimp/perf_hud.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/screen_filters.h"
#include "gbaimp/audio_device.h"
#include "gbaimp/audio_pipeline.h"
#include "gbaimp/sdl_interpose.h"
#include "capture_harness.h"
#include "dialogue_backlog.h"
#include "music_hook.h"
#include "text_overlay/engine_hooks.h"
#include "ram_overlay_dispatch.h"
#include "widescreen_adapter.h"
#include <SDL.h>
#ifdef SDL_RenderPresent
#undef SDL_RenderPresent
#endif
#ifdef SDL_PollEvent
#undef SDL_PollEvent
#endif
#ifdef SDL_UpdateTexture
#undef SDL_UpdateTexture
#endif
#ifdef SDL_RenderCopy
#undef SDL_RenderCopy
#endif
#ifdef SDL_QueueAudio
#undef SDL_QueueAudio
#endif
#ifdef SDL_OpenAudioDevice
#undef SDL_OpenAudioDevice
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#if !defined(_WIN32)
// Linux/GNU ld: __wrap_SDL_X shims + real table (see CMakeLists.txt for the link flags).
GBAIMP_DEFINE_LD_WRAPS()
#endif

namespace {
// Per-function call counters (relaxed atomics, essentially free). Printed at exit
// when KHCOM_LOG_INTERPOSE=1, to show that the interposers are really exercised.
enum { kCntPoll, kCntUpdateTexture, kCntRenderCopy, kCntRenderPresent, kCntQueueAudio, kCntOpenAudioDevice, kCntN };
std::atomic<unsigned long long> g_calls[kCntN];
inline void count_call(int i) { g_calls[i].fetch_add(1, std::memory_order_relaxed); }
std::atomic<bool> g_log_counters{false};  // KHCOM_LOG_INTERPOSE=1
void print_interpose_counters();

bool interpose_logging() {
    const char* v = std::getenv("KHCOM_LOG_INTERPOSE");
    return v && *v && std::strcmp(v, "0") != 0;
}

void print_interpose_counters() {
    std::fprintf(stderr,
                 "[khcom] sdl intercept calls: SDL_PollEvent=%llu SDL_UpdateTexture=%llu "
                 "SDL_RenderCopy=%llu SDL_RenderPresent=%llu SDL_QueueAudio=%llu SDL_OpenAudioDevice=%llu\n",
                 g_calls[kCntPoll].load(), g_calls[kCntUpdateTexture].load(),
                 g_calls[kCntRenderCopy].load(), g_calls[kCntRenderPresent].load(),
                 g_calls[kCntQueueAudio].load(), g_calls[kCntOpenAudioDevice].load());
    const gbaimp::AudioWrapStats a = gbaimp::audio_wrap_stats();
    std::fprintf(stderr,
                 "[khcom] audio: devices_wrapped=%llu callbacks=%llu callback_frames_processed=%llu "
                 "queue_calls=%llu level rms %.1f -> %.1f peak %d -> %d (over %llu samples)\n",
                 static_cast<unsigned long long>(a.devices_wrapped),
                 static_cast<unsigned long long>(a.callbacks),
                 static_cast<unsigned long long>(a.frames_processed),
                 static_cast<unsigned long long>(a.queue_calls),
                 a.rms_before, a.rms_after, a.peak_before, a.peak_after,
                 static_cast<unsigned long long>(a.metered_samples));
    const khcom::MusicHookStats m = khcom::music_hook_stats();
    std::fprintf(stderr,
                 "[khcom] m4a song hook: starts=%llu hd_mapped=%llu hd_stops=%llu\n",
                 static_cast<unsigned long long>(m.starts),
                 static_cast<unsigned long long>(m.mapped_triggers),
                 static_cast<unsigned long long>(m.hd_stops));
}
} // namespace

extern "C" {

int khcom_poll_event_intercept(SDL_Event* event);
int khcom_update_texture_intercept(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch);
int khcom_render_copy_intercept(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect);
void khcom_render_present_intercept(SDL_Renderer* renderer);
int khcom_queue_audio_intercept(SDL_AudioDeviceID dev, const void* data, Uint32 len);
SDL_AudioDeviceID khcom_open_audio_device_intercept(const char* device, int iscapture, const SDL_AudioSpec* desired, SDL_AudioSpec* obtained, int allowed_changes);

extern "C" void khcom_update_widescreen_state();

int khcom_poll_event_intercept(SDL_Event* event) {
    count_call(kCntPoll);
    khcom_update_widescreen_state();
    khcom_install_ram_dispatch();
    while (true) {
        int res = gbaimp::sdl_real().poll_event(event);
        if (!res) {
            return 0;
        }
        if (event) {
            if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
                event->key.keysym.sym == SDLK_ESCAPE) {
                event->key.keysym.scancode = SDL_SCANCODE_ESCAPE;
            }
            if (khcom::DialogueBacklog::instance().handle_event(*event)) {
                continue;
            }
            if (gbaimp::PerfHud::instance().handle_mouse_event(*event)) {
                continue;
            }
        }
        return res;
    }
}

int khcom_update_texture_intercept(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch) {
    count_call(kCntUpdateTexture);
    if (!texture || !pixels) {
        return gbaimp::sdl_real().update_texture(texture, rect, pixels, pitch);
    }

    uint32_t format = 0;
    int access = 0, tex_w = 0, tex_h = 0;
    if (SDL_QueryTexture(texture, &format, &access, &tex_w, &tex_h) == 0 &&
        format == SDL_PIXELFORMAT_RGB24 && pitch >= tex_w * 3) {
        if (tex_h == 160 && (tex_w == 240 || tex_w == 284) && khcom::capture::active()) {
            khcom::capture::on_native_frame(pixels, pitch, tex_w, tex_h);   // raw PPU framebuffer
        }

        auto& filters = gbaimp::ScreenFilters::instance();
        if (filters.settings().color_profile != gbaimp::ColorProfile::Raw) {
            static std::vector<uint8_t> s_color_buf;
            size_t needed = static_cast<size_t>(pitch) * tex_h;
            if (s_color_buf.size() < needed) {
                s_color_buf.resize(needed);
            }
            std::memcpy(s_color_buf.data(), pixels, needed);
            filters.apply_color_correction(s_color_buf.data(), tex_w, tex_h);
            pixels = s_color_buf.data();
        }
    }

    return gbaimp::sdl_real().update_texture(texture, rect, pixels, pitch);
}

int khcom_render_copy_intercept(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect) {
    count_call(kCntRenderCopy);
    if (!renderer || !texture) {
        return gbaimp::sdl_real().render_copy(renderer, texture, srcrect, dstrect);
    }

    // Only intercept presentation to the final backbuffer; offscreen target passes straight through
    if (SDL_GetRenderTarget(renderer) != nullptr) {
        return gbaimp::sdl_real().render_copy(renderer, texture, srcrect, dstrect);
    }

    uint32_t format = 0;
    int access = 0, tex_w = 0, tex_h = 0;
    if (SDL_QueryTexture(texture, &format, &access, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
        return gbaimp::sdl_real().render_copy(renderer, texture, srcrect, dstrect);
    }

    // Identify if this is the GBA game presentation texture:
    // Either direct PPU framebuffer (RGB24 streaming, 240x160 or 284x160)
    // or sharp prescaled target (RGBA8888 target, integer scaled 240*K x 160*K or 284*K x 160*K)
    const bool is_streaming_gba = (access == SDL_TEXTUREACCESS_STREAMING &&
                                   format == SDL_PIXELFORMAT_RGB24 &&
                                   tex_h == 160 && (tex_w == 240 || tex_w == 284));
    const bool is_scaled_target = (access == SDL_TEXTUREACCESS_TARGET &&
                                   format == SDL_PIXELFORMAT_RGBA8888 &&
                                   tex_h % 160 == 0 &&
                                   ((tex_w % 284 == 0 && tex_w / 284 == tex_h / 160) ||
                                    (tex_w % 240 == 0 && tex_w / 240 == tex_h / 160)));
    const bool is_game_texture = is_streaming_gba || is_scaled_target;

    if (!is_game_texture) {
        return gbaimp::sdl_real().render_copy(renderer, texture, srcrect, dstrect);
    }

    int out_w = 0, out_h = 0;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    if (out_w <= 0 || out_h <= 0) {
        return gbaimp::sdl_real().render_copy(renderer, texture, srcrect, dstrect);
    }

    // Always normalize renderer to full-window 1:1 pixel coordinates
    int lw = 0, lh = 0;
    SDL_RenderGetLogicalSize(renderer, &lw, &lh);
    if (lw != 0 || lh != 0) {
        SDL_RenderSetLogicalSize(renderer, 0, 0);
    }
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(renderer, &sx, &sy);
    if (sx != 1.0f || sy != 1.0f) {
        SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    }
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_RenderSetViewport(renderer, nullptr);

    SDL_Rect src{};
    SDL_Rect dst{};
    int base_w = 240;
    int base_h = 160;
    khcom_compute_effective_viewport(tex_w, tex_h, out_w, out_h, &src, &dst, &base_w, &base_h);
    gbaimp::PerfHud::instance().set_game_viewport(dst);

    // Clear letterbox / pillarbox areas outside the game viewport to pure black
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (dst.x > 0) {
        SDL_Rect bar = { 0, 0, dst.x, out_h };
        SDL_RenderFillRect(renderer, &bar);
    }
    if (dst.x + dst.w < out_w) {
        SDL_Rect bar = { dst.x + dst.w, 0, out_w - (dst.x + dst.w), out_h };
        SDL_RenderFillRect(renderer, &bar);
    }
    if (dst.y > 0) {
        SDL_Rect bar = { 0, 0, out_w, dst.y };
        SDL_RenderFillRect(renderer, &bar);
    }
    if (dst.y + dst.h < out_h) {
        SDL_Rect bar = { 0, dst.y + dst.h, out_w, out_h - (dst.y + dst.h) };
        SDL_RenderFillRect(renderer, &bar);
    }

    int res = gbaimp::sdl_real().render_copy(renderer, texture, &src, &dst);

    gbaimp::ScreenFilters::instance().render_mask(renderer, &dst, base_w, base_h);

    // Output-resolution dialogue overlay (engine side). It is drawn right after the game image, NOT at
    // present time: gbarecomp draws its host layers (the Esc settings menu, touch pad, ...) after this
    // copy and before the present, so text drawn here stays UNDER the menu. Returns immediately (one
    // branch) unless the overlay is enabled; it runs once per presented frame (this is the single copy
    // of the game image to the back buffer).
    if (res == 0) khcom::overlay::engine::on_present(renderer, dst);
    return res;
}

void khcom_render_present_intercept(SDL_Renderer* renderer) {
    count_call(kCntRenderPresent);
    // With KHCOM_LOG_INTERPOSE=1 also report every 300 presents (~5 s), so the counters
    // are visible even when the process is killed instead of exiting normally.
    if (g_log_counters.load(std::memory_order_relaxed) &&
        g_calls[kCntRenderPresent].load(std::memory_order_relaxed) % 300 == 1) {
        print_interpose_counters();
    }
    if (renderer) {
        khcom_widescreen_notify_present();

        gbaimp::FrameInterpolator::instance().on_present(renderer, &gbaimp::PerfHud::instance().game_viewport());
        gbaimp::PerfHud::instance().on_frame_present(renderer);

        int win_w = 0, win_h = 0;
        SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
        khcom::DialogueBacklog::instance().render_sidebar(renderer, win_w, win_h);

        if (khcom::capture::active()) khcom::capture::on_before_present(renderer);
        gbaimp::sdl_real().render_present(renderer);
    }
}
}

extern "C" {
// Direct-queue audio (gbarecomp with GBARECOMP_AUDIO_DIRECT=1): the library processes the
// data using the channel count / rate of the device as it was actually opened (mono 65536 Hz
// for gbarecomp), not a stereo assumption.
int khcom_queue_audio_intercept(SDL_AudioDeviceID dev, const void* data, Uint32 len) {
    count_call(kCntQueueAudio);
    return gbaimp::queue_audio_processed(dev, data, len);
}

// Default audio path of gbarecomp: the device is opened with a pull callback, so SDL_QueueAudio
// never runs. The library substitutes a trampoline on the callback that runs the original
// callback and then processes the buffer it filled (HD music mix, volume, DSP) on SDL's audio
// thread. See gbaimp/audio_device.h for the real-time contract.
SDL_AudioDeviceID khcom_open_audio_device_intercept(const char* device, int iscapture,
                                                    const SDL_AudioSpec* desired,
                                                    SDL_AudioSpec* obtained, int allowed_changes) {
    count_call(kCntOpenAudioDevice);
    return gbaimp::open_audio_device_processed(device, iscapture, desired, obtained, allowed_changes);
}
}

// Registers the handlers with the gba-improvements interposition layer and, on
// Windows, patches the import tables. Idempotent. Must run before the runtime
// starts calling SDL (call it first thing in main). On Linux the `--wrap` link
// step is the mechanism, so only the handler registration happens (it also runs
// from a static initializer, so it does not depend on main() calling it).
// Returns true when import-table patching is active.
extern "C" bool khcom_install_sdl_interposers() {
    gbaimp::SdlHandlers h;
    h.poll_event = &khcom_poll_event_intercept;
    h.update_texture = &khcom_update_texture_intercept;
    h.render_copy = &khcom_render_copy_intercept;
    h.render_present = &khcom_render_present_intercept;
    h.queue_audio = &khcom_queue_audio_intercept;
    h.open_audio_device = &khcom_open_audio_device_intercept;
    gbaimp::set_sdl_handlers(h);

    static std::once_flag s_once;
    std::call_once(s_once, [] {
        if (interpose_logging()) {
            g_log_counters.store(true);
            gbaimp::set_audio_metering(true);   // before/after level meter for the log
            std::atexit(&print_interpose_counters);
        }
    });

    bool active = false;
#if defined(_WIN32)
    const bool verbose = interpose_logging();
    gbaimp::SdlInstallOptions opt;
    static bool s_verbose;
    s_verbose = verbose;
    opt.log = [](const char* line) { if (s_verbose) std::fprintf(stderr, "%s\n", line); };
    gbaimp::SdlInstallStatus st;
    active = gbaimp::install_sdl_interposers(h, &st, &opt);
    if (!active || st.slots_failed > 0 || st.slots_skipped_foreign > 0) {
        std::fprintf(stderr,
                     "[khcom] warning: SDL interposers incomplete (patched=%d failed=%d foreign=%d%s%s); "
                     "enhancements that hook SDL (filters, HUD, backlog, widescreen) may be inactive\n",
                     st.slots_patched, st.slots_failed, st.slots_skipped_foreign,
                     st.error.empty() ? "" : " error=", st.error.c_str());
    }
#endif
    return active;
}

#if !defined(_WIN32)
namespace {
const bool s_handlers_registered = (khcom_install_sdl_interposers(), true);
}
#endif

