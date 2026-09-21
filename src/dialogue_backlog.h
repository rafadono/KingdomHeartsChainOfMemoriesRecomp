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

    const std::vector<DialogueEntry>& entries() const { return entries_; }

private:
    DialogueBacklog();

    void draw_text(SDL_Renderer* renderer, int x, int y, const char* str,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a, int scale = 1);

    bool enabled_ = true;
    bool is_open_ = false;
    int scroll_offset_ = 0;
    int max_scroll_ = 0;

    std::vector<DialogueEntry> entries_;
};

} // namespace khcom
