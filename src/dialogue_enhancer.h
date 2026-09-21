#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace khcom {

enum class FontDensity : int {
    Original = 0,    // 100% standard GBA glyph spacing
    Compact = 1,     // 80% compact glyph spacing
    HighDensity = 2  // 65% high-density glyph spacing
};

struct DialogueEnhancerSettings {
    FontDensity density = FontDensity::Compact;
    bool soft_word_wrap = true;
    bool case_aware_continuation = true;
    int max_line_width_chars = 38; // Default GBA is ~26 chars
};

class DialogueEnhancer {
public:
    static DialogueEnhancer& instance();

    const DialogueEnhancerSettings& settings() const { return settings_; }
    DialogueEnhancerSettings& settings() { return settings_; }

    void set_density(FontDensity density);
    void set_soft_word_wrap(bool enable);
    void set_case_aware_continuation(bool enable);

    // Calculates glyph advance width in pixels according to density setting
    int get_glyph_advance_width(int base_width = 8) const;

    // Evaluates whether a newline token should be merged into a space (soft wrap)
    // based on grammar, punctuation, and casing
    bool should_merge_newline(char prev_char, char next_char) const;

    // Formats a dialogue buffer applying soft word-wrapping and casing heuristics
    std::string process_dialogue_text(std::string_view raw_text);

private:
    DialogueEnhancer() = default;

    DialogueEnhancerSettings settings_;
};

} // namespace khcom
