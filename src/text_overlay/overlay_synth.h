#pragma once
// Synthetic PageSnapshots for tests and the PNG dump tool (no engine needed).
#include "overlay_types.h"

#include <string>
#include <vector>

namespace khcom {
namespace overlay {

// Builds a page the way the engine would report it: one word per character, 0x0A between lines, one glyph
// entry per drawn word (spaces included), pen positions on the engine grid (~5.5 px per character).
inline PageSnapshot synth_page(const std::vector<std::string>& lines, float origin_x = 72.0f, float origin_y = 108.0f,
                               bool glyphs = true, uint32_t page_id = 1) {
    PageSnapshot p;
    p.valid = true;
    p.page_id = page_id;
    p.origin_x = origin_x;
    p.origin_y = origin_y;
    p.wrap_px = 155;
    p.line_pitch = 12;
    p.max_lines = 3;
    for (size_t li = 0; li < lines.size(); ++li) {
        float pen = 0.0f;
        for (unsigned char c : lines[li]) {
            p.text.push_back(c);
            if (glyphs) {
                PageGlyph g;
                g.word = c;
                g.gx = origin_x + pen;
                g.gy = origin_y + 12.0f * static_cast<float>(li);
                p.glyphs.push_back(g);
            }
            pen += 5.5f;
        }
        if (li + 1 < lines.size()) p.text.push_back(0x000A);
    }
    p.revealed = glyphs ? static_cast<int>(p.glyphs.size()) : 0;
    p.page_complete = true;
    return p;
}

} // namespace overlay
} // namespace khcom
