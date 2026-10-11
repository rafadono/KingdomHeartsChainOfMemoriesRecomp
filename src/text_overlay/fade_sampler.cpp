#include "fade_sampler.h"

#include "gba/gba_ppu.h"
#include "runtime_bus_bridge.h"   // active_ppu

#include <cstring>

namespace khcom::overlay::engine {

FadeState FrameBlend::state_at(int row, bool obj_normal_blends) const {
    if (!valid) return FadeState{};
    row = row < 0 ? 0 : (row > 159 ? 159 : row);
    const BlendRegs& r = line[row];
    return fade_from_regs(r.bldcnt, r.bldalpha, r.bldy, obj_normal_blends);
}

namespace {

class Sampler final : public gba::GbaPpu::PresentationObserver {
public:
    void scanline(const gba::GbaPpu&, uint32_t y, uint16_t, const uint8_t* io, const uint8_t*, const uint8_t*,
                  const uint8_t*) override {
        if (y >= 160 || !io) return;
        BlendRegs& r = cur_.line[y];
        r.bldcnt = static_cast<uint16_t>(io[0x50] | (io[0x51] << 8));
        r.bldalpha = static_cast<uint16_t>(io[0x52] | (io[0x53] << 8));
        r.bldy = static_cast<uint16_t>(io[0x54] | (io[0x55] << 8));
    }
    void frame_ready() override {
        last_ = cur_;
        last_.valid = true;
    }
    void restored() noexcept override { last_.valid = false; }
    void feed(int y, const BlendRegs& r) { if (y >= 0 && y < 160) cur_.line[y] = r; }
    FrameBlend cur_, last_;
};

Sampler s_sampler;
bool s_attached = false;

}  // namespace

void fade_sampler_attach() {
    gba::GbaPpu* ppu = gbarecomp::active_ppu();
    if (!ppu) return;
    gba::GbaPpu::PresentationObserver* o = ppu->presentation_observer();
    if (o == &s_sampler) return;
    if (o != nullptr) return;   // somebody else (netplay) owns the observer: no fade information
    ppu->set_presentation_observer(&s_sampler);
    s_attached = true;
}

void fade_sampler_detach() {
    if (!s_attached) return;
    s_attached = false;
    gba::GbaPpu* ppu = gbarecomp::active_ppu();
    if (ppu && ppu->presentation_observer() == &s_sampler) ppu->set_presentation_observer(nullptr);
}

bool fade_sampler_last_frame(FrameBlend* out) {
    if (!s_sampler.last_.valid) return false;
    if (out) *out = s_sampler.last_;
    return true;
}

void fade_sampler_feed_line_for_tests(int y, const BlendRegs& r) { s_sampler.feed(y, r); }
void fade_sampler_frame_ready_for_tests() { s_sampler.frame_ready(); }
void fade_sampler_reset_for_tests() { s_sampler = Sampler(); }

}  // namespace khcom::overlay::engine
