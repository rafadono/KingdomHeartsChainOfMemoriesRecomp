#include "dialogue_enhancer.h"
#include "dialogue_backlog.h"
#include <cctype>
#include <sstream>

namespace khcom {

DialogueEnhancer& DialogueEnhancer::instance() {
    static DialogueEnhancer s_instance;
    return s_instance;
}

void DialogueEnhancer::set_density(FontDensity density) {
    settings_.density = density;
    switch (density) {
        case FontDensity::Original:
            settings_.max_line_width_chars = 26;
            break;
        case FontDensity::Compact:
            settings_.max_line_width_chars = 34;
            break;
        case FontDensity::HighDensity:
            settings_.max_line_width_chars = 42;
            break;
    }
}

void DialogueEnhancer::set_soft_word_wrap(bool enable) {
    settings_.soft_word_wrap = enable;
}

void DialogueEnhancer::set_case_aware_continuation(bool enable) {
    settings_.case_aware_continuation = enable;
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

    // Skip leading/trailing whitespace checks
    if (std::isspace(static_cast<unsigned char>(next_char))) {
        return false;
    }

    bool is_sentence_terminator = (prev_char == '.' || prev_char == '!' || prev_char == '?' ||
                                   prev_char == ':' || prev_char == ';');

    if (!settings_.case_aware_continuation) {
        return !is_sentence_terminator;
    }

    // Case-aware grammar rules:
    // 1. If preceding char is not sentence-ending and next is lowercase: continuation
    if (!is_sentence_terminator && std::islower(static_cast<unsigned char>(next_char))) {
        return true;
    }

    // 2. Preceding char is comma or word char, next is uppercase (proper noun or speech quote): continuation
    if ((prev_char == ',' || std::isalpha(static_cast<unsigned char>(prev_char))) &&
        std::isupper(static_cast<unsigned char>(next_char))) {
        return true;
    }

    // 3. Preceding is a sentence terminator and next is uppercase: true sentence break, preserve separation
    if (is_sentence_terminator && std::isupper(static_cast<unsigned char>(next_char))) {
        return false;
    }

    return !is_sentence_terminator;
}

std::string DialogueEnhancer::process_dialogue_text(std::string_view raw_text) {
    if (!settings_.soft_word_wrap || raw_text.empty()) {
        return std::string(raw_text);
    }

    std::string result;
    result.reserve(raw_text.size());

    int current_line_len = 0;
    const int max_len = settings_.max_line_width_chars;

    for (size_t i = 0; i < raw_text.size(); ++i) {
        char c = raw_text[i];

        if (c == '\n' || c == '\r') {
            char prev = (i > 0) ? raw_text[i - 1] : ' ';
            char next = (i + 1 < raw_text.size()) ? raw_text[i + 1] : ' ';

            if (should_merge_newline(prev, next) && current_line_len < max_len) {
                // Merge newline into space
                if (!result.empty() && result.back() != ' ') {
                    result.push_back(' ');
                    ++current_line_len;
                }
            } else {
                result.push_back('\n');
                current_line_len = 0;
            }
        } else {
            result.push_back(c);
            ++current_line_len;

            // Auto-wrap if reaching maximum line boundary on word boundaries
            if (current_line_len >= max_len && c == ' ') {
                result.back() = '\n';
                current_line_len = 0;
            }
        }
    }

    if (result.size() >= 3) {
        DialogueBacklog::instance().push_entry("Story", result);
    }

    return result;
}

} // namespace khcom
