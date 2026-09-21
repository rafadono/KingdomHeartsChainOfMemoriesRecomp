#include "input_enhancements.h"
#include <cmath>
#include <algorithm>

namespace khcom {

InputEnhancements& InputEnhancements::instance() {
    static InputEnhancements s_instance;
    return s_instance;
}

void InputEnhancements::set_analog_mode(AnalogMode mode) {
    settings_.analog_mode = mode;
}

void InputEnhancements::set_deadzone(float deadzone) {
    settings_.deadzone = std::clamp(deadzone, 0.05f, 0.50f);
}

void InputEnhancements::process_axis_input(int16_t axis_x, int16_t axis_y, uint16_t& keyinput) {
    if (settings_.analog_mode == AnalogMode::DigitalOriginal) {
        return;
    }

    // Normalize coordinates to [-1.0, 1.0]
    float norm_x = axis_x / 32767.0f;
    float norm_y = axis_y / 32767.0f;
    norm_x = std::clamp(norm_x, -1.0f, 1.0f);
    norm_y = std::clamp(norm_y, -1.0f, 1.0f);

    float magnitude = std::sqrt(norm_x * norm_x + norm_y * norm_y);
    if (magnitude < settings_.deadzone) {
        return;
    }

    ++walk_frame_counter_;

    // If walk mode is active and stick is tilted gently (< walk_threshold)
    bool is_walking = (settings_.analog_mode == AnalogMode::AnalogWalkRun) &&
                      (magnitude < settings_.walk_threshold);

    if (is_walking && (walk_frame_counter_ % 2 == 1)) {
        // Drop directional input on alternate frames to cut Sora's speed by 50%
        return;
    }

    // Clear direction bits (active low: 1 = released, 0 = pressed)
    // Bit 4: Right, Bit 5: Left, Bit 6: Up, Bit 7: Down
    keyinput |= 0x00F0u;

    // Calculate angle in degrees [-180, 180]
    float angle = std::atan2(norm_y, norm_x) * (180.0f / 3.14159265f);
    if (angle < 0.0f) angle += 360.0f;

    // 8-directional sectors with 45-degree slices
    // Right: [337.5, 360) or [0, 22.5)
    // Down-Right: [22.5, 67.5)
    // Down: [67.5, 112.5)
    // Down-Left: [112.5, 157.5)
    // Left: [157.5, 202.5)
    // Up-Left: [202.5, 247.5)
    // Up: [247.5, 292.5)
    // Up-Right: [292.5, 337.5)

    if (angle >= 337.5f || angle < 22.5f) {
        keyinput &= ~0x0010u; // Right
    } else if (angle >= 22.5f && angle < 67.5f) {
        keyinput &= ~0x0010u; // Right
        keyinput &= ~0x0080u; // Down
    } else if (angle >= 67.5f && angle < 112.5f) {
        keyinput &= ~0x0080u; // Down
    } else if (angle >= 112.5f && angle < 157.5f) {
        keyinput &= ~0x0020u; // Left
        keyinput &= ~0x0080u; // Down
    } else if (angle >= 157.5f && angle < 202.5f) {
        keyinput &= ~0x0020u; // Left
    } else if (angle >= 202.5f && angle < 247.5f) {
        keyinput &= ~0x0020u; // Left
        keyinput &= ~0x0040u; // Up
    } else if (angle >= 247.5f && angle < 292.5f) {
        keyinput &= ~0x0040u; // Up
    } else if (angle >= 292.5f && angle < 337.5f) {
        keyinput &= ~0x0010u; // Right
        keyinput &= ~0x0040u; // Up
    }
}

} // namespace khcom
