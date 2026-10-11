// Headless capture + scripted-input harness (developer tooling, env-gated).
//
// Everything here is inert unless one of the KHCOM_* variables documented in
// docs/CAPTURE_HARNESS.md is set: the hot-path entry points start with a single
// relaxed atomic load of a cached flag and return immediately when the harness is off.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SDL_Renderer;

namespace khcom {
namespace capture {

// True when any harness feature is enabled (env parsed once on first call).
bool active();

// Called once per emulated frame from the extended_view_frame hook (arms captures, handles
// KHCOM_EXIT_FRAME). `hw_frame_count` is the PPU frame counter of the runtime. Scripted input is not
// applied here: see extra_runtime_args() (GBARECOMP_INPUT_REPLAY trace).
void on_emulated_frame(std::uint64_t hw_frame_count);

// Called from the SDL_UpdateTexture intercept for GBA-sized RGB24 textures (the PPU
// framebuffer, before colour correction). Writes a "native" capture when one is pending.
void on_native_frame(const void* pixels, int pitch, int width, int height);

// Called from the SDL_RenderPresent intercept right before the real present: applies
// KHCOM_WINDOW_SIZE, writes an "output" capture when one is pending, handles exit.
void on_before_present(SDL_Renderer* renderer);

// KHCOM_SET overrides ("key=value;key=value"), same keys as khcom_config.ini.
const std::vector<std::pair<std::string, int>>& settings_overrides();

// Live settings hook for KHCOM_CAPTURE_VARIANTS entries (ini keys, e.g. screen_mask=0).
void set_setting_callback(int (*cb)(const char* key, int value));

// Arguments to inject into argv (main.cpp): KHCOM_SAVESTATE_LOAD / KHCOM_SAVESTATE_SAVE.
// Also translates KHCOM_SAVESTATE_SAVE into GBARECOMP_ASSIST_SCRIPT.
std::vector<std::string> extra_runtime_args();

// Writes an 8-bit RGB PNG (stored deflate, no zlib dependency). Returns false on I/O error.
bool write_png_rgb(const std::string& path, const std::uint8_t* rgb, int width, int height,
                   int pitch_bytes);

}  // namespace capture
}  // namespace khcom
