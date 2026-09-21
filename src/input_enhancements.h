#pragma once

#include <cstdint>

namespace khcom {

enum class AnalogMode : int {
    DigitalOriginal = 0,
    Analog8Way = 1,
    AnalogWalkRun = 2
};

struct InputEnhancementSettings {
    AnalogMode analog_mode = AnalogMode::AnalogWalkRun;
    float deadzone = 0.18f;
    float walk_threshold = 0.55f;
};

class InputEnhancements {
public:
    static InputEnhancements& instance();

    const InputEnhancementSettings& settings() const { return settings_; }
    InputEnhancementSettings& settings() { return settings_; }

    void set_analog_mode(AnalogMode mode);
    void set_deadzone(float deadzone);

    // Filter or modulate REG_KEYINPUT based on physical analog stick coordinates
    void process_axis_input(int16_t axis_x, int16_t axis_y, uint16_t& keyinput);

private:
    InputEnhancements() = default;

    InputEnhancementSettings settings_;
    uint32_t walk_frame_counter_ = 0;
};

} // namespace khcom
