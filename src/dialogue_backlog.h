#pragma once

#include <cstdint>

namespace khcom {

// Game-specific observer of the Chain of Memories cutscene dialogue. The history panel itself
// (entries, hotkeys, drawing, settings rows) is gbaimp::TextHistory; this class only recognises
// the lines the game starts to type and pushes {speaker, text} into it.
class DialogueBacklog {
public:
    static DialogueBacklog& instance();

    // Real-time bus read observer: captures active cutscene dialogue characters as they are rendered
    void on_bus_read_u16(uint32_t addr);

    // Clears the history and the observer's pending state.
    void clear();

private:
    DialogueBacklog();

    uint32_t last_pushed_text_ptr_ = 0;
    uint32_t pending_dialogue_ptr_ = 0;
    uint8_t pending_speaker_id_ = 0;
};

void khcom_install_backlog_hook();

bool is_internal_bus_read();
void set_internal_bus_read(bool active);

struct ScopedInternalBusRead {
    ScopedInternalBusRead() { set_internal_bus_read(true); }
    ~ScopedInternalBusRead() { set_internal_bus_read(false); }
};

} // namespace khcom
