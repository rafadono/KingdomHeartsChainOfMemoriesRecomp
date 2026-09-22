#include "dialogue_backlog.h"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

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

std::vector<std::string> wrap_text_by_pixels(const std::string& text, int max_pixel_w, int char_advance) {
    std::vector<std::string> lines;
    if (text.empty() || max_pixel_w <= 0 || char_advance <= 0) return lines;

    std::istringstream stream(text);
    std::string word;
    std::string cur_line;
    int cur_line_w = 0;
    const int space_w = char_advance;

    while (stream >> word) {
        int word_w = static_cast<int>(word.size()) * char_advance;
        if (cur_line.empty()) {
            cur_line = word;
            cur_line_w = word_w;
        } else if (cur_line_w + space_w + word_w <= max_pixel_w) {
            cur_line += " " + word;
            cur_line_w += space_w + word_w;
        } else {
            lines.push_back(cur_line);
            cur_line = word;
            cur_line_w = word_w;
        }
    }
    if (!cur_line.empty()) {
        lines.push_back(cur_line);
    }
    return lines;
}

} // namespace

DialogueBacklog& DialogueBacklog::instance() {
    static DialogueBacklog s_instance;
    return s_instance;
}

DialogueBacklog::DialogueBacklog() {
    push_entry("Sora", "Where are we? Donald? Goofy?");
    push_entry("Donald", "Look over there! What is that huge building?");
    push_entry("Goofy", "Gawrsh, that sure looks spooky... Could it be Castle Oblivion?");
    push_entry("Marluxia", "Ahead lies what you seek, but to claim it, you must lose that which you hold dear.");
    push_entry("Sora", "Lose what we hold dear?! Who are you?");
    push_entry("Marluxia", "To find is to lose, and to lose is to find. That is the rule here in Castle Oblivion.");
}

void DialogueBacklog::push_entry(const std::string& speaker, const std::string& text) {
    if (text.empty()) return;
    if (!entries_.empty() && entries_.back().text == text && entries_.back().speaker == speaker) {
        return;
    }

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
        scroll_offset_ = max_scroll_;
    }
}

void DialogueBacklog::draw_text(SDL_Renderer* renderer, int x, int y, const char* str,
                                uint8_t r, uint8_t g, uint8_t b, uint8_t a, int scale) {
    if (!str || !renderer || scale <= 0) return;

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
                scroll_offset_ = std::max(0, scroll_offset_ - 36);
                return true;
            }
            if (event.key.keysym.sym == SDLK_DOWN) {
                scroll_offset_ = std::min(max_scroll_, scroll_offset_ + 36);
                return true;
            }
            if (event.key.keysym.sym == SDLK_PAGEUP) {
                scroll_offset_ = std::max(0, scroll_offset_ - 180);
                return true;
            }
            if (event.key.keysym.sym == SDLK_PAGEDOWN) {
                scroll_offset_ = std::min(max_scroll_, scroll_offset_ + 180);
                return true;
            }
            return true; // Swallow input while reading backlog
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
                scroll_offset_ = std::max(0, scroll_offset_ - 36);
                return true;
            }
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                scroll_offset_ = std::min(max_scroll_, scroll_offset_ + 36);
                return true;
            }
            return true;
        }
    } else if (event.type == SDL_MOUSEWHEEL && is_open_) {
        scroll_offset_ -= event.wheel.y * 48;
        scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_);
        return true;
    } else if (event.type == SDL_MOUSEBUTTONDOWN && is_open_) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            if (event.button.x > cached_bar_w_) {
                set_open(false);
                return true;
            }
            if (event.button.y <= 56 && event.button.x >= cached_bar_w_ - 140) {
                set_open(false);
                return true;
            }
        }
        return true;
    }

    return is_open_;
}

void DialogueBacklog::render_sidebar(SDL_Renderer* renderer, int win_w, int win_h) {
    if (!enabled_ || !is_open_ || !renderer || win_w <= 0 || win_h <= 0) return;

    // Isolate coordinates: switch out of logical coordinates into raw physical window coordinates
    int prev_lw = 0, prev_lh = 0;
    SDL_Rect prev_vp{};
    SDL_RenderGetLogicalSize(renderer, &prev_lw, &prev_lh);
    SDL_RenderGetViewport(renderer, &prev_vp);
    SDL_RenderSetLogicalSize(renderer, 0, 0);
    SDL_RenderSetViewport(renderer, nullptr);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Fullscreen dim overlay
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 130);
    SDL_Rect full_dim = { 0, 0, win_w, win_h };
    SDL_RenderFillRect(renderer, &full_dim);

    // Sidebar dimensions: 40% width, clamped between 380px and 600px
    int bar_w = std::clamp((win_w * 40) / 100, 380, 600);
    int bar_h = win_h;
    cached_win_w_ = win_w;
    cached_bar_w_ = bar_w;

    // Translucent dark glassmorphic panel
    SDL_SetRenderDrawColor(renderer, 10, 15, 26, 240);
    SDL_Rect panel_rect = { 0, 0, bar_w, bar_h };
    SDL_RenderFillRect(renderer, &panel_rect);

    // Right accent border (bright cyan / sky blue)
    SDL_SetRenderDrawColor(renderer, 56, 189, 248, 220);
    SDL_RenderDrawLine(renderer, bar_w - 1, 0, bar_w - 1, bar_h);
    SDL_SetRenderDrawColor(renderer, 14, 116, 144, 120);
    SDL_RenderDrawLine(renderer, bar_w - 2, 0, bar_w - 2, bar_h);

    // Header bar
    SDL_SetRenderDrawColor(renderer, 15, 23, 42, 250);
    SDL_Rect header_rect = { 0, 0, bar_w - 1, 56 };
    SDL_RenderFillRect(renderer, &header_rect);
    SDL_SetRenderDrawColor(renderer, 51, 65, 85, 200);
    SDL_RenderDrawLine(renderer, 0, 56, bar_w - 1, 56);

    draw_text(renderer, 20, 14, "CONVERSATION LOG", 56, 189, 248, 255, 2);
    draw_text(renderer, bar_w - 130, 20, "[ESC/B] CLOSE", 148, 163, 184, 220, 1);

    // Subheader hint
    draw_text(renderer, 20, 62, "MOUSE WHEEL / ARROWS / DPAD TO SCROLL", 100, 116, 139, 200, 1);

    // Calculate layout with exact pixel wrapping
    const int entry_start_y = 86;
    const int content_area_h = win_h - entry_start_y - 24;
    const int font_scale = (win_h >= 900) ? 2 : 1;
    const int char_advance = 6 * font_scale;
    const int line_height = 8 * font_scale + 4;
    const int max_text_w = bar_w - 48;

    struct PreparedEntry {
        std::string speaker;
        std::string timestamp;
        std::vector<std::string> lines;
        int height = 0;
    };

    std::vector<PreparedEntry> prepared;
    prepared.reserve(entries_.size());
    int total_content_h = 0;

    for (const auto& entry : entries_) {
        PreparedEntry pe;
        pe.speaker = entry.speaker;
        pe.timestamp = entry.timestamp;
        pe.lines = wrap_text_by_pixels(entry.text, max_text_w, char_advance);

        int header_h = 7 * font_scale + 8;
        int body_h = static_cast<int>(pe.lines.size()) * line_height;
        pe.height = header_h + body_h + 14;
        total_content_h += pe.height;
        prepared.push_back(std::move(pe));
    }

    max_scroll_ = std::max(0, total_content_h - content_area_h);
    scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_);

    // Empty state representation
    if (prepared.empty()) {
        draw_text(renderer, 26, entry_start_y + 40, "No dialogue recorded yet.", 148, 163, 184, 255, font_scale);
        draw_text(renderer, 26, entry_start_y + 70, "Advance conversations to read back story lines.", 100, 116, 139, 200, 1);
    }

    // Render dialogue entries
    int cur_y = entry_start_y - scroll_offset_;

    for (const auto& pe : prepared) {
        if (cur_y + pe.height > entry_start_y && cur_y < win_h - 10) {
            // Speaker badge
            uint8_t spk_r = 251, spk_g = 191, spk_b = 36; // Amber
            if (pe.speaker == "Sora") {
                spk_r = 56; spk_g = 189; spk_b = 248; // Cyan
            } else if (pe.speaker == "Marluxia") {
                spk_r = 244; spk_g = 114; spk_b = 182; // Rose pink
            }

            draw_text(renderer, 22, cur_y, pe.speaker.c_str(), spk_r, spk_g, spk_b, 255, font_scale);
            draw_text(renderer, bar_w - (font_scale == 2 ? 110 : 70), cur_y + 2, pe.timestamp.c_str(), 100, 116, 139, 180, 1);

            int text_y = cur_y + (7 * font_scale + 8);
            for (const auto& line : pe.lines) {
                if (text_y >= entry_start_y - line_height && text_y < win_h - 10) {
                    draw_text(renderer, 26, text_y, line.c_str(), 241, 245, 249, 255, font_scale);
                }
                text_y += line_height;
            }

            // Separator line
            SDL_SetRenderDrawColor(renderer, 30, 41, 59, 140);
            SDL_RenderDrawLine(renderer, 20, cur_y + pe.height - 4, bar_w - 24, cur_y + pe.height - 4);
        }

        cur_y += pe.height;
    }

    // Scrollbar indicator
    if (max_scroll_ > 0) {
        int sb_w = 6;
        int sb_x = bar_w - 10;
        int sb_track_h = content_area_h;
        int sb_thumb_h = std::max(28, (sb_track_h * content_area_h) / total_content_h);
        int sb_thumb_y = entry_start_y + (scroll_offset_ * (sb_track_h - sb_thumb_h)) / max_scroll_;

        SDL_SetRenderDrawColor(renderer, 51, 65, 85, 180);
        SDL_Rect track_rect = { sb_x, entry_start_y, sb_w, sb_track_h };
        SDL_RenderFillRect(renderer, &track_rect);

        SDL_SetRenderDrawColor(renderer, 56, 189, 248, 240);
        SDL_Rect thumb_rect = { sb_x, sb_thumb_y, sb_w, sb_thumb_h };
        SDL_RenderFillRect(renderer, &thumb_rect);
    }

    // Restore original logical size and viewport
    SDL_RenderSetLogicalSize(renderer, prev_lw, prev_lh);
    SDL_RenderSetViewport(renderer, &prev_vp);
}

} // namespace khcom
