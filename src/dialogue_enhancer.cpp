#include "dialogue_enhancer.h"
#include "dialogue_backlog.h"
#include "font_resizer.h"
#include "dialogue_box_scaler.h"
#include "cutscene_dialogue_data.inl"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

extern "C" uint16_t bus_read_u16(uint32_t addr);

namespace khcom {

namespace {

static const DialogueLookupEntry* find_cutscene_entry(uint32_t addr) {
    if (addr < kCutsceneDialogues[0].text_ptr) return nullptr;
    auto it = std::upper_bound(std::begin(kCutsceneDialogues), std::end(kCutsceneDialogues), addr,
        [](uint32_t val, const DialogueLookupEntry& e) {
            return val < e.text_ptr;
        });
    if (it != std::begin(kCutsceneDialogues)) {
        --it;
        if (addr >= it->text_ptr && addr < it->text_ptr + it->length_bytes) {
            return &(*it);
        }
    }
    return nullptr;
}

// Standard GBA KH:CoM proportional width table for ASCII 32 (' ') to 126 ('~')
static const uint8_t kAsciiWidths[95] = {
    4, // ' '
    3, 4, 7, 6, 7, 7, 3, 4, 4, 5, 6, 3, 5, 3, 5, // '!' through '/'
    6, 5, 6, 6, 6, 6, 6, 6, 6, 6,                 // '0' through '9'
    3, 3, 5, 6, 5, 5, 7,                          // ':' through '@'
    7, 6, 6, 6, 6, 6, 7, 6, 4, 6, 6, 6, 8, 7, 7, // 'A' through 'O'
    6, 7, 6, 6, 6, 6, 7, 8, 7, 6, 6,              // 'P' through 'Z'
    4, 5, 4, 6, 6, 4,                             // '[' through '`'
    6, 6, 5, 6, 5, 5, 6, 6, 3, 4, 5, 3, 8, 6, 6, // 'a' through 'o'
    6, 6, 5, 5, 5, 6, 6, 8, 6, 6, 5,              // 'p' through 'z'
    4, 3, 4, 7                                    // '{' through '~'
};

static inline int get_char_px_width(uint16_t w) {
    if (w < 128) {
        return FontResizer::instance().get_char_advance_width(static_cast<char>(w));
    }
    if (w == 0x0092) return FontResizer::instance().get_char_advance_width('\'');
    if (w == 0x0085) return FontResizer::instance().get_char_advance_width('.') * 3;
    if (w == 0x0093 || w == 0x0094) return FontResizer::instance().get_char_advance_width('"');
    return FontResizer::instance().get_glyph_width();
}

static std::vector<uint16_t> rewrap_words_in_place(const std::vector<uint16_t>& in,
                                                   int max_line_len,
                                                   int max_line_px,
                                                   bool soft_wrap,
                                                   bool case_aware) {
    std::vector<uint16_t> out = in;
    if (!soft_wrap || out.empty() || max_line_len <= 0 || max_line_px <= 0) return out;

    const int space_px = FontResizer::instance().get_char_advance_width(' ');
    const size_t n = out.size();

    int cur_line_len = 0;
    int cur_line_px = 0;
    size_t i = 0;

    while (i < n) {
        uint16_t w = out[i];
        if (w == 0 || w >= 0xE000) {
            break;
        }

        if (w == 0x000A) { // Newline delimiter
            size_t delim_start = i;
            size_t newline_idx = i;
            while (i < n && (out[i] == 0x000A || out[i] == 0x0020)) {
                if (out[i] == 0x000A) newline_idx = i;
                ++i;
            }
            if (i >= n || out[i] == 0 || out[i] >= 0xE000) break;

            uint16_t prev_w = (delim_start > 0) ? out[delim_start - 1] : 0;
            uint16_t next_w = out[i];

            bool is_sentence_terminator = (prev_w == '.' || prev_w == '!' || prev_w == '?' ||
                                           prev_w == ':' || prev_w == ';');
            bool next_is_lower = (next_w >= 'a' && next_w <= 'z');
            bool prev_is_comma_or_word = (prev_w == ',' || (prev_w >= 'A' && prev_w <= 'Z') || (prev_w >= 'a' && prev_w <= 'z'));
            bool next_is_upper = (next_w >= 'A' && next_w <= 'Z');

            bool can_merge = false;
            if (!case_aware) {
                can_merge = !is_sentence_terminator;
            } else {
                if (!is_sentence_terminator && next_is_lower) {
                    can_merge = true;
                } else if (prev_is_comma_or_word && next_is_upper) {
                    can_merge = true;
                } else if (is_sentence_terminator && next_is_upper) {
                    can_merge = false;
                } else {
                    can_merge = !is_sentence_terminator;
                }
            }

            // Next word info
            size_t word_start = i;
            int next_word_len = 0;
            int next_word_px = 0;
            while (i < n && out[i] != 0x000A && out[i] != 0x0020 && out[i] != 0 && out[i] < 0xE000) {
                ++next_word_len;
                next_word_px += get_char_px_width(out[i]);
                ++i;
            }

            // Word must fit on the current line to be merged
            bool will_fit = (cur_line_len + 1 + next_word_len <= max_line_len) &&
                            (cur_line_px + space_px + next_word_px <= max_line_px);

            if (can_merge && will_fit) {
                out[newline_idx] = 0x0020; // Merge into space
                cur_line_len += 1 + next_word_len;
                cur_line_px += space_px + next_word_px;
            } else {
                // Preserve newline!
                out[newline_idx] = 0x000A;
                cur_line_len = next_word_len;
                cur_line_px = next_word_px;
            }
        } else if (w == 0x0020) {
            size_t space_idx = i;
            while (i < n && out[i] == 0x0020) ++i;
            if (i >= n || out[i] == 0 || out[i] >= 0xE000) break;

            if (out[i] == 0x000A) {
                continue;
            }

            // Next word info
            size_t word_start = i;
            int next_word_len = 0;
            int next_word_px = 0;
            while (i < n && out[i] != 0x000A && out[i] != 0x0020 && out[i] != 0 && out[i] < 0xE000) {
                ++next_word_len;
                next_word_px += get_char_px_width(out[i]);
                ++i;
            }

            if (cur_line_len > 0 &&
                (cur_line_len + 1 + next_word_len > max_line_len || cur_line_px + space_px + next_word_px > max_line_px)) {
                // Word exceeds line limit! Wrap at the space before this word!
                // Word stays completely intact on the next line!
                out[space_idx] = 0x000A;
                cur_line_len = next_word_len;
                cur_line_px = next_word_px;
            } else {
                // Word fits on current line
                out[space_idx] = 0x0020;
                cur_line_len += 1 + next_word_len;
                cur_line_px += space_px + next_word_px;
            }
        } else {
            // First word at start of string
            while (i < n && out[i] != 0x000A && out[i] != 0x0020 && out[i] != 0 && out[i] < 0xE000) {
                ++cur_line_len;
                cur_line_px += get_char_px_width(out[i]);
                ++i;
            }
        }
    }

    return out;
}

} // namespace

DialogueEnhancer& DialogueEnhancer::instance() {
    static DialogueEnhancer s_instance;
    return s_instance;
}

DialogueEnhancer::DialogueEnhancer() {
    recompute_line_width();
}

void DialogueEnhancer::recompute_line_width() {
    // 1. Calculate available text pixel width dynamically:
    // Base GBA dialogue box has 152px usable text area (28 tiles = 224px minus portrait & borders).
    // In widescreen or responsive modes, extra horizontal tiles widen the dialogue box.
    int extra_tiles = DialogueBoxScaler::instance().settings().extra_horizontal_tiles;
    int extra_px = extra_tiles * 8;
    settings_.max_line_width_px = 152 + extra_px;

    // 2. Calculate average character width dynamically based on active FontResizer metrics:
    float avg_char_w = FontResizer::instance().get_average_char_width();
    if (avg_char_w < 3.5f) avg_char_w = 3.5f;

    // 3. Dynamic max characters per line:
    int dynamic_chars = static_cast<int>(std::round(static_cast<float>(settings_.max_line_width_px + 2) / avg_char_w));

    // Fine-tune based on line capacity mode
    if (settings_.line_capacity == LineCapacityMode::Dense2Lines) {
        dynamic_chars += 2;
    } else if (settings_.line_capacity == LineCapacityMode::Expanded4Lines) {
        dynamic_chars += 4;
    }

    // Physical safety constraint:
    // In standard GBA mode (no extra tiles), the physical text window limit is 152px,
    // which corresponds to ~26-28 characters max. Exceeding 28 characters causes the GBA
    // hardware engine to hard-wrap mid-word!
    // In widescreen, capacity expands proportionally with the extra horizontal pixels.
    int max_allowed_chars = (extra_tiles == 0) ? 28 : (28 + (extra_px * 4) / 15 + 4);
    settings_.max_line_width_chars = std::clamp(dynamic_chars, 24, max_allowed_chars);
}

void DialogueEnhancer::set_density(FontDensity density) {
    settings_.density = density;
    FontResizer::instance().set_density(density);
    recompute_line_width();
    invalidate_cache();
}

void DialogueEnhancer::set_font_scale(FontScale scale) {
    settings_.scale = scale;
    FontResizer::instance().set_font_scale(scale);
    recompute_line_width();
    invalidate_cache();
}

void DialogueEnhancer::set_font_style(FontStyle style) {
    settings_.style = style;
    FontResizer::instance().set_font_style(style);
    recompute_line_width();
    invalidate_cache();
}

void DialogueEnhancer::set_line_capacity(LineCapacityMode mode) {
    settings_.line_capacity = mode;
    recompute_line_width();
    invalidate_cache();
}

void DialogueEnhancer::set_soft_word_wrap(bool enable) {
    settings_.soft_word_wrap = enable;
    invalidate_cache();
}

void DialogueEnhancer::set_case_aware_continuation(bool enable) {
    settings_.case_aware_continuation = enable;
    invalidate_cache();
}

void DialogueEnhancer::invalidate_cache() {
    cached_ptr_ = 0;
    cached_words_.clear();
}

bool DialogueEnhancer::intercept_bus_read(uint32_t addr, uint32_t width, uint32_t* out_val) {
    if (!out_val) return false;
    if (addr < 0x08FBD378u || addr > 0x08FFE504u + 1024) {
        return false;
    }

    if (settings_.density == FontDensity::Original &&
        settings_.scale == FontScale::Original100 &&
        settings_.style == FontStyle::Authentic &&
        settings_.line_capacity == LineCapacityMode::Authentic3Lines &&
        !settings_.soft_word_wrap) {
        return false;
    }

    static bool s_in_intercept = false;
    if (s_in_intercept) return false;

    // Check hit in active cached dialogue
    if (cached_ptr_ != 0 && addr >= cached_ptr_ && addr < cached_ptr_ + cached_words_.size() * 2) {
        size_t idx = (addr - cached_ptr_) / 2;
        if (width == 2) {
            *out_val = cached_words_[idx];
            return true;
        } else if (width == 1) {
            uint16_t w = cached_words_[idx];
            *out_val = (addr & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
            return true;
        } else if (width == 4) {
            uint32_t w0 = cached_words_[idx];
            uint32_t w1 = (idx + 1 < cached_words_.size()) ? cached_words_[idx + 1] : 0;
            *out_val = (w1 << 16) | (w0 & 0xFFFF);
            return true;
        }
    }

    const DialogueLookupEntry* match = find_cutscene_entry(addr);
    if (!match) return false;

    s_in_intercept = true;
    cached_ptr_ = match->text_ptr;
    cached_words_.clear();
    cached_words_.reserve(128);

    {
        ScopedInternalBusRead guard;
        for (size_t i = 0; i < 512; ++i) {
            uint16_t w = bus_read_u16(cached_ptr_ + i * 2);
            cached_words_.push_back(w);
            if (w == 0 || w >= 0xE000) break;
        }
    }

    // Dynamic line wrapping based on current font size, style, density, and box width
    recompute_line_width();
    const int bus_max_chars = settings_.max_line_width_chars;
    const int bus_max_px = settings_.max_line_width_px;
    cached_words_ = rewrap_words_in_place(cached_words_, bus_max_chars, bus_max_px,
                                          settings_.soft_word_wrap, settings_.case_aware_continuation);
    s_in_intercept = false;

    if (addr >= cached_ptr_ && addr < cached_ptr_ + cached_words_.size() * 2) {
        size_t idx = (addr - cached_ptr_) / 2;
        if (width == 2) {
            *out_val = cached_words_[idx];
            return true;
        } else if (width == 1) {
            uint16_t w = cached_words_[idx];
            *out_val = (addr & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
            return true;
        } else if (width == 4) {
            uint32_t w0 = cached_words_[idx];
            uint32_t w1 = (idx + 1 < cached_words_.size()) ? cached_words_[idx + 1] : 0;
            *out_val = (w1 << 16) | (w0 & 0xFFFF);
            return true;
        }
    }

    return false;
}

int DialogueEnhancer::get_glyph_advance_width(int base_width) const {
    switch (settings_.density) {
        case FontDensity::Compact:
            return (base_width * 4) / 5; // 80% spacing
        case FontDensity::HighDensity:
            return (base_width * 13) / 20; // 65% spacing
        case FontDensity::Original:
        default:
            return base_width;
    }
}

bool DialogueEnhancer::should_merge_newline(char prev_char, char next_char) const {
    if (!settings_.soft_word_wrap) {
        return false;
    }

    if (std::isspace(static_cast<unsigned char>(next_char))) {
        return false;
    }

    bool is_sentence_terminator = (prev_char == '.' || prev_char == '!' || prev_char == '?' ||
                                   prev_char == ':' || prev_char == ';');

    if (!settings_.case_aware_continuation) {
        return !is_sentence_terminator;
    }

    if (!is_sentence_terminator && std::islower(static_cast<unsigned char>(next_char))) {
        return true;
    }

    if ((prev_char == ',' || std::isalpha(static_cast<unsigned char>(prev_char))) &&
        std::isupper(static_cast<unsigned char>(next_char))) {
        return true;
    }

    if (is_sentence_terminator && std::isupper(static_cast<unsigned char>(next_char))) {
        return false;
    }

    return !is_sentence_terminator;
}

std::string DialogueEnhancer::process_dialogue_text(std::string_view raw_text) {
    if (!settings_.soft_word_wrap || raw_text.empty()) {
        return std::string(raw_text);
    }

    recompute_line_width();

    std::string result;
    result.reserve(raw_text.size() + 16);

    const int max_len = settings_.max_line_width_chars;
    const int max_px = settings_.max_line_width_px;
    const int space_px = FontResizer::instance().get_char_advance_width(' ');

    int cur_line_len = 0;
    int cur_line_px = 0;
    size_t i = 0;
    const size_t n = raw_text.size();

    while (i < n) {
        if (raw_text[i] == '\n' || raw_text[i] == '\r') {
            size_t delim_start = i;
            bool has_newline = false;
            while (i < n && (raw_text[i] == '\n' || raw_text[i] == '\r' || raw_text[i] == ' ')) {
                if (raw_text[i] == '\n' || raw_text[i] == '\r') has_newline = true;
                ++i;
            }
            if (i >= n) break;

            char prev_c = (delim_start > 0) ? raw_text[delim_start - 1] : ' ';
            char next_c = raw_text[i];

            // Next word
            size_t word_start = i;
            int next_word_len = 0;
            int next_word_px = 0;
            while (i < n && raw_text[i] != '\n' && raw_text[i] != '\r' && raw_text[i] != ' ') {
                ++next_word_len;
                next_word_px += get_char_px_width(static_cast<uint8_t>(raw_text[i]));
                ++i;
            }

            bool will_fit = (cur_line_len + 1 + next_word_len <= max_len) &&
                            (cur_line_px + space_px + next_word_px <= max_px);

            if (has_newline && should_merge_newline(prev_c, next_c) && will_fit) {
                if (!result.empty() && result.back() != ' ' && result.back() != '\n') {
                    result.push_back(' ');
                    cur_line_len += 1;
                    cur_line_px += space_px;
                }
            } else {
                result.push_back('\n');
                cur_line_len = 0;
                cur_line_px = 0;
            }

            result.append(raw_text.substr(word_start, next_word_len));
            cur_line_len += next_word_len;
            cur_line_px += next_word_px;
        } else if (raw_text[i] == ' ') {
            while (i < n && raw_text[i] == ' ') ++i;
            if (i >= n) break;

            if (raw_text[i] == '\n' || raw_text[i] == '\r') {
                continue;
            }

            // Next word
            size_t word_start = i;
            int next_word_len = 0;
            int next_word_px = 0;
            while (i < n && raw_text[i] != '\n' && raw_text[i] != '\r' && raw_text[i] != ' ') {
                ++next_word_len;
                next_word_px += get_char_px_width(static_cast<uint8_t>(raw_text[i]));
                ++i;
            }

            if (cur_line_len > 0 &&
                (cur_line_len + 1 + next_word_len > max_len || cur_line_px + space_px + next_word_px > max_px)) {
                result.push_back('\n');
                cur_line_len = next_word_len;
                cur_line_px = next_word_px;
            } else {
                if (!result.empty() && result.back() != ' ' && result.back() != '\n') {
                    result.push_back(' ');
                    cur_line_len += 1;
                    cur_line_px += space_px;
                }
                cur_line_len += next_word_len;
                cur_line_px += next_word_px;
            }
            result.append(raw_text.substr(word_start, next_word_len));
        } else {
            // First word at start of string
            size_t word_start = i;
            int word_len = 0;
            int word_px = 0;
            while (i < n && raw_text[i] != '\n' && raw_text[i] != '\r' && raw_text[i] != ' ') {
                ++word_len;
                word_px += get_char_px_width(static_cast<uint8_t>(raw_text[i]));
                ++i;
            }
            result.append(raw_text.substr(word_start, word_len));
            cur_line_len += word_len;
            cur_line_px += word_px;
        }
    }

    if (result.size() >= 3) {
        DialogueBacklog::instance().push_entry("Story", result);
    }

    return result;
}

std::string DialogueEnhancer::get_rewrapped_dialogue(uint32_t text_ptr) {
    if (text_ptr < 0x08FBD378u || text_ptr > 0x08FFE504u) {
        return "";
    }
    std::vector<uint16_t> words;
    words.reserve(128);
    {
        ScopedInternalBusRead guard;
        for (size_t i = 0; i < 512; ++i) {
            uint16_t w = bus_read_u16(text_ptr + i * 2);
            if (w == 0 || w >= 0xE000) break;
            words.push_back(w);
        }
    }
    recompute_line_width();
    const int bus_max_chars = settings_.max_line_width_chars;
    const int bus_max_px = settings_.max_line_width_px;
    std::vector<uint16_t> rewrapped = rewrap_words_in_place(words, bus_max_chars, bus_max_px,
                                                            settings_.soft_word_wrap, settings_.case_aware_continuation);
    std::string result;
    result.reserve(rewrapped.size() + 8);
    for (uint16_t w : rewrapped) {
        if (w == 0x000A) {
            result.push_back('\n');
        } else if (w >= 32 && w <= 126) {
            result.push_back(static_cast<char>(w));
        } else if (w == 0x85) {
            result += "...";
        } else if (w == 0x92) {
            result += "'";
        } else if (w == 0x93 || w == 0x94) {
            result += "\"";
        } else if (w == 0xE9) {
            result += "é";
        }
    }
    return result;
}

} // namespace khcom
