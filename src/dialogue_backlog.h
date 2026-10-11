#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;
union SDL_Event;

namespace khcom {

struct DialogueEntry {
    std::string speaker;
    std::string text;
    std::string timestamp;
};

class DialogueBacklog {
public:
    static DialogueBacklog& instance();

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enable) { enabled_ = enable; }

    bool is_open() const { return is_open_; }
    void toggle_open();
    void set_open(bool open);

    // Add entry to conversation history
    void push_entry(const std::string& speaker, const std::string& text);
    void clear();

    // Event handler for hotkeys (L / F2 / Gamepad Back) and scroll navigation
    bool handle_event(const SDL_Event& event);

    // Render the left-docked translucent glassmorphic sidebar
    void render_sidebar(SDL_Renderer* renderer, int win_w, int win_h);

    // Real-time bus read observer: captures active cutscene dialogue characters as they are rendered
    void on_bus_read_u16(uint32_t addr);

    const std::vector<DialogueEntry>& entries() const { return entries_; }

private:
    DialogueBacklog();

    void draw_text(SDL_Renderer* renderer, int x, int y, const char* str,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a, int scale = 1);

    bool enabled_ = true;
    bool is_open_ = false;
    int scroll_offset_ = 0;
    int max_scroll_ = 0;
    int cached_win_w_ = 1024;
    int cached_bar_w_ = 400;
    uint32_t last_pushed_text_ptr_ = 0;
    uint32_t pending_dialogue_ptr_ = 0;
    uint8_t pending_speaker_id_ = 0;

    std::vector<DialogueEntry> entries_;
};

void khcom_install_backlog_hook();

bool is_internal_bus_read();
void set_internal_bus_read(bool active);

struct ScopedInternalBusRead {
    ScopedInternalBusRead() { set_internal_bus_read(true); }
    ~ScopedInternalBusRead() { set_internal_bus_read(false); }
};

} // namespace khcom
