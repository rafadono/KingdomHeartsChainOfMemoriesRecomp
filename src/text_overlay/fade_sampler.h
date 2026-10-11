#pragma once
// Samples the PPU blend registers per scanline (host-side presentation observer; gbarecomp is not modified) so the
// overlay can follow the screen fades of the PRESENTED frame. See docs/TEXT_OVERLAY_ENGINE.md "Screen fades".
#include "overlay_fade.h"

#include <cstdint>

namespace khcom::overlay::engine {

struct BlendRegs {
    uint16_t bldcnt = 0, bldalpha = 0, bldy = 0;
    bool operator==(const BlendRegs& o) const { return bldcnt == o.bldcnt && bldalpha == o.bldalpha && bldy == o.bldy; }
};

// Pure part (tested): per-line register table of one completed frame + the lookup the engine side uses.
struct FrameBlend {
    BlendRegs line[160];
    bool valid = false;
    // Fade of the text OBJ for the scanline `row` (clamped to 0..159); None when no frame is known.
    FadeState state_at(int row, bool obj_normal_blends = kPpuBlendsNormalObj) const;
};

// Installs / refreshes the observer on the active PPU (no-op when another observer owns it). Cheap; called per present.
void fade_sampler_attach();
void fade_sampler_detach();
// Blend registers of the last COMPLETED frame (the one in the PPU's latched framebuffer). Returns false when unknown.
bool fade_sampler_last_frame(FrameBlend* out);
// Test hooks: feed the sampler directly.
void fade_sampler_feed_line_for_tests(int y, const BlendRegs& r);
void fade_sampler_frame_ready_for_tests();
void fade_sampler_reset_for_tests();

}  // namespace khcom::overlay::engine
