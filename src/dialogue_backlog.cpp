#include "dialogue_backlog.h"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace khcom {

namespace {

// 5x7 ASCII bitmap font (from ASCII 32 to 126)
const uint8_t kFont5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '\''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // ')'
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ';'
    {0x08, 0x14, 0x22, 0x41, 0x00}, // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // '\'
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // '_'
    {0x00, 0x01, 0x02, 0x04, 0x00}, // '`'
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 'a'
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 'b'
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 'c'
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 'e'
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 'f'
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 'i'
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 'l'
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 'o'
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 'p'
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 'r'
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 's'
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 'x'
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 'z'
    {0x00, 0x08, 0x36, 0x41, 0x00}, // '{'
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // '|'
    {0x00, 0x41, 0x36, 0x08, 0x00}, // '}'
    {0x08, 0x08, 0x2A, 0x1C, 0x08}  // '~'
};

} // namespace

DialogueBacklog& DialogueBacklog::instance() {
    static DialogueBacklog s_instance;
    return s_instance;
}

DialogueBacklog::DialogueBacklog() {
    // Populate scene opening history so backlog has context immediately
    push_entry("Sora", "Where are we? Donald? Goofy?");
    push_entry("Donald", "Look over there! What is that huge building?");
    push_entry("Goofy", "Gawrsh, that sure looks spooky... Could it be Castle Oblivion?");
    push_entry("Marluxia", "Ahead lies what you seek, but to claim it, you must lose that which you hold dear.");
    push_entry("Sora", "Lose what we hold dear?! Who are you?");
    push_entry("Marluxia", "To find is to lose, and to lose is to find. That is the rule here in Castle Oblivion.");
}

void DialogueBacklog::push_entry(const std::string& speaker, const std::string& text) {
    if (text.empty()) return;

    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &now_time);
#else
    localtime_r(&now_time, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%H:%M:%S");

    entries_.push_back({speaker, text, ss.str()});

    // Keep ring buffer capped to last 200 dialogue lines
    if (entries_.size() > 200) {
        entries_.erase(entries_.begin());
    }
}

void DialogueBacklog::clear() {
    entries_.clear();
    scroll_offset_ = 0;
}

void DialogueBacklog::toggle_open() {
    if (!enabled_) return;
    set_open(!is_open_);
}

void DialogueBacklog::set_open(bool open) {
    is_open_ = open;
    if (open) {
        // Reset scroll position to bottom (newest messages)
        scroll_offset_ = max_scroll_;
    }
}

void DialogueBacklog::draw_text(SDL_Renderer* renderer, int x, int y, const char* str,
                                uint8_t r, uint8_t g, uint8_t b, uint8_t a, int scale) {
    if (!str || !renderer) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, r, g, b, a);

    int cur_x = x;
    while (*str) {
        char c = *str++;
        if (c < 32 || c > 126) c = ' ';

        const uint8_t* col_data = kFont5x7[c - 32];
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = col_data[col];
            for (int row = 0; row < 7; ++row) {
                if ((bits >> row) & 1) {
                    if (scale == 1) {
                        SDL_RenderDrawPoint(renderer, cur_x + col, y + row);
                    } else {
                        SDL_Rect pixel_rect = { cur_x + col * scale, y + row * scale, scale, scale };
                        SDL_RenderFillRect(renderer, &pixel_rect);
                    }
                }
            }
        }
        cur_x += 6 * scale;
    }
}

bool DialogueBacklog::handle_event(const SDL_Event& event) {
    if (!enabled_) return false;

    // Toggle hotkey check (L key, F2, or Gamepad Back/Select)
    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_l || event.key.keysym.sym == SDLK_F2) {
            toggle_open();
            return true;
        }
        if (is_open_) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                set_open(false);
                return true;
            }
            if (event.key.keysym.sym == SDLK_UP) {
                scroll_offset_ = std::max(0, scroll_offset_ - 30);
                return true;
            }
            if (event.key.keysym.sym == SDLK_DOWN) {
                scroll_offset_ = std::min(max_scroll_, scroll_offset_ + 30);
                return true;
            }
            if (event.key.keysym.sym == SDLK_PAGEUP) {
                scroll_offset_ = std::max(0, scroll_offset_ - 150);
                return true;
            }
            if (event.key.keysym.sym == SDLK_PAGEDOWN) {
                scroll_offset_ = std::min(max_scroll_, scroll_offset_ + 150);
                return true;
            }
            return true; // Swallow keyboard inputs while backlog is active
        }
    } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
            toggle_open();
            return true;
        }
        if (is_open_) {
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                set_open(false);
                return true;
            }
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                scroll_offset_ = std::max(0, scroll_offset_ - 30);
                return true;
            }
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                scroll_offset_ = std::min(max_scroll_, scroll_offset_ + 30);
                return true;
            }
            return true;
        }
    } else if (event.type == SDL_MOUSEWHEEL && is_open_) {
        scroll_offset_ -= event.wheel.y * 35;
        scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_);
        return true;
    }

    return is_open_;
}

void DialogueBacklog::render_sidebar(SDL_Renderer* renderer, int win_w, int win_h) {
    if (!enabled_ || !is_open_ || !renderer || win_w <= 0 || win_h <= 0) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Fullscreen dim overlay
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 110);
    SDL_Rect full_dim = { 0, 0, win_w, win_h };
    SDL_RenderFillRect(renderer, &full_dim);

    // Sidebar dimensions: 38% width, capped between 360px and 520px
    int bar_w = std::clamp((win_w * 38) / 100, 360, 520);
    int bar_h = win_h;

    // Translucent dark glassmorphic panel
    SDL_SetRenderDrawColor(renderer, 10, 15, 26, 235);
    SDL_Rect panel_rect = { 0, 0, bar_w, bar_h };
    SDL_RenderFillRect(renderer, &panel_rect);

    // Right accent border (bright cyan / sky blue)
    SDL_SetRenderDrawColor(renderer, 56, 189, 248, 200);
    SDL_RenderDrawLine(renderer, bar_w - 1, 0, bar_w - 1, bar_h);
    SDL_SetRenderDrawColor(renderer, 14, 116, 144, 100);
    SDL_RenderDrawLine(renderer, bar_w - 2, 0, bar_w - 2, bar_h);

    // Header bar
    SDL_SetRenderDrawColor(renderer, 15, 23, 42, 245);
    SDL_Rect header_rect = { 0, 0, bar_w - 1, 48 };
    SDL_RenderFillRect(renderer, &header_rect);
    SDL_SetRenderDrawColor(renderer, 51, 65, 85, 180);
    SDL_RenderDrawLine(renderer, 0, 48, bar_w - 1, 48);

    draw_text(renderer, 16, 14, "CONVERSATION LOG", 56, 189, 248, 255, 2);
    draw_text(renderer, bar_w - 140, 18, "[ESC/B] CLOSE", 148, 163, 184, 220, 1);

    // Subheader hint
    draw_text(renderer, 16, 54, "MOUSE WHEEL / ARROWS / DPAD TO SCROLL", 100, 116, 139, 200, 1);

    // Calculate total height of entries
    const int entry_start_y = 75;
    const int content_area_h = win_h - entry_start_y - 20;
    const int chars_per_line = std::max(20, (bar_w - 40) / 7);

    int total_content_h = 0;
    for (const auto& entry : entries_) {
        int lines = 1 + static_cast<int>(entry.text.size()) / chars_per_line;
        total_content_h += 24 + lines * 14 + 10;
    }

    max_scroll_ = std::max(0, total_content_h - content_area_h);
    scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_);

    // Render dialogue entries
    int cur_y = entry_start_y - scroll_offset_;

    for (const auto& entry : entries_) {
        int entry_h = 24 + (1 + static_cast<int>(entry.text.size()) / chars_per_line) * 14;

        if (cur_y + entry_h > entry_start_y && cur_y < win_h - 10) {
            // Speaker badge
            uint8_t spk_r = 251, spk_g = 191, spk_b = 36; // Amber
            if (entry.speaker == "Sora") {
                spk_r = 56; spk_g = 189; spk_b = 248; // Light blue
            } else if (entry.speaker == "Marluxia") {
                spk_r = 244; spk_g = 114; spk_b = 182; // Rose / pink
            }

            draw_text(renderer, 18, cur_y, entry.speaker.c_str(), spk_r, spk_g, spk_b, 255, 1);
            draw_text(renderer, bar_w - 65, cur_y, entry.timestamp.c_str(), 100, 116, 139, 180, 1);

            // Dialogue body text with word wrapping
            int text_y = cur_y + 14;
            std::string remaining = entry.text;

            while (!remaining.empty() && text_y < win_h - 10) {
                std::string line;
                if (static_cast<int>(remaining.size()) <= chars_per_line) {
                    line = remaining;
                    remaining.clear();
                } else {
                    size_t split_pos = remaining.rfind(' ', chars_per_line);
                    if (split_pos == std::string::npos || split_pos == 0) {
                        split_pos = chars_per_line;
                    }
                    line = remaining.substr(0, split_pos);
                    remaining = remaining.substr(split_pos + (remaining[split_pos] == ' ' ? 1 : 0));
                }

                if (text_y >= entry_start_y) {
                    draw_text(renderer, 24, text_y, line.c_str(), 241, 245, 249, 255, 1);
                }
                text_y += 14;
            }

            // Separator line
            SDL_SetRenderDrawColor(renderer, 30, 41, 59, 120);
            SDL_RenderDrawLine(renderer, 18, text_y + 4, bar_w - 20, text_y + 4);
        }

        cur_y += entry_h + 10;
    }

    // Scrollbar indicator
    if (max_scroll_ > 0) {
        int sb_w = 4;
        int sb_x = bar_w - 8;
        int sb_track_h = content_area_h;
        int sb_thumb_h = std::max(20, (sb_track_h * content_area_h) / total_content_h);
        int sb_thumb_y = entry_start_y + (scroll_offset_ * (sb_track_h - sb_thumb_h)) / max_scroll_;

        SDL_SetRenderDrawColor(renderer, 51, 65, 85, 180);
        SDL_Rect track_rect = { sb_x, entry_start_y, sb_w, sb_track_h };
        SDL_RenderFillRect(renderer, &track_rect);

        SDL_SetRenderDrawColor(renderer, 56, 189, 248, 230);
        SDL_Rect thumb_rect = { sb_x, sb_thumb_y, sb_w, sb_thumb_h };
        SDL_RenderFillRect(renderer, &thumb_rect);
    }
}

} // namespace khcom
