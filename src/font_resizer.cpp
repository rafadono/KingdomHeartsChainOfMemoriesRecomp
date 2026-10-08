#include "font_resizer.h"
#include "dialogue_backlog.h"
#include <algorithm>
#include <cstring>
#include <cmath>

extern "C" uint16_t bus_read_u16(uint32_t addr);

namespace khcom {

namespace {

// Authentic GBA KH:CoM Latin glyph advance widths from ROM (0x08F7D438 - 0x08F7D638)
static const uint16_t kRomLatinGlyphWidths[256] = {
    0, 4, 4, 5, 5, 6, 8, 8, 8, 6, 6, 6, 8, 8, 8, 8,
    8, 8, 7, 8, 8, 8, 8, 8, 6, 12, 0, 0, 0, 0, 0, 0,
    3, 2, 5, 8, 6, 7, 7, 3, 4, 4, 6, 6, 3, 6, 3, 5,
    6, 4, 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 5, 6, 5, 6,
    8, 6, 6, 6, 6, 5, 5, 6, 6, 4, 6, 6, 6, 7, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 8, 7, 6, 6, 3, 5, 3, 6, 6,
    3, 6, 6, 5, 6, 6, 5, 6, 6, 2, 4, 5, 3, 6, 6, 6,
    6, 6, 5, 6, 5, 6, 6, 8, 6, 6, 6, 4, 2, 4, 8, 7,
    7, 7, 3, 6, 4, 9, 6, 6, 5, 9, 6, 4, 9, 4, 6, 7,
    7, 3, 3, 5, 5, 7, 7, 8, 5, 9, 6, 4, 8, 7, 6, 6,
    0, 2, 6, 7, 5, 6, 2, 5, 4, 8, 4, 6, 6, 4, 8, 6,
    5, 6, 5, 5, 3, 7, 7, 3, 3, 4, 4, 6, 7, 7, 8, 5,
    6, 6, 6, 6, 6, 6, 9, 6, 5, 5, 5, 5, 3, 3, 4, 4,
    7, 6, 6, 6, 6, 6, 6, 7, 8, 6, 6, 6, 6, 6, 5, 6,
    6, 6, 6, 6, 6, 6, 8, 5, 6, 6, 6, 6, 3, 3, 4, 4,
    6, 6, 6, 6, 6, 6, 6, 8, 6, 6, 6, 6, 6, 6, 5, 6,
};

// 5x7 ASCII bitmap font (ASCII 32 ' ' to 126 '~')
// Bit 0 = top row, Bit 6 = bottom row
static const uint8_t kFont5x7[95][5] = {
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

// Decodes a 16x16 4bpp sprite (128 bytes, 4 tiles of 8x8) into a 16x16 pixel matrix
static void decode_16x16_sprite(const uint8_t* tile_buf, uint8_t grid[16][16]) {
    static const int kTileOffsets[4][3] = {
        {0, 0, 0},
        {32, 0, 8},
        {64, 8, 0},
        {96, 8, 8}
    };
    for (int t = 0; t < 4; ++t) {
        int toff = kTileOffsets[t][0];
        int br = kTileOffsets[t][1];
        int bc = kTileOffsets[t][2];
        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 8; c += 2) {
                uint8_t b = tile_buf[toff + r * 4 + c / 2];
                grid[br + r][bc + c] = b & 0x0F;
                grid[br + r][bc + c + 1] = (b >> 4) & 0x0F;
            }
        }
    }
}

// Encodes a 16x16 pixel matrix into GBA 16x16 4bpp sprite layout (128 bytes)
static void encode_16x16_sprite(const uint8_t grid[16][16], uint8_t* tile_buf) {
    static const int kTileOffsets[4][3] = {
        {0, 0, 0},
        {32, 0, 8},
        {64, 8, 0},
        {96, 8, 8}
    };
    for (int t = 0; t < 4; ++t) {
        int toff = kTileOffsets[t][0];
        int br = kTileOffsets[t][1];
        int bc = kTileOffsets[t][2];
        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 8; c += 2) {
                uint8_t p0 = grid[br + r][bc + c] & 0x0F;
                uint8_t p1 = grid[br + r][bc + c + 1] & 0x0F;
                tile_buf[toff + r * 4 + c / 2] = p0 | (p1 << 4);
            }
        }
    }
}

} // namespace

FontResizer& FontResizer::instance() {
    static FontResizer s_instance;
    return s_instance;
}

FontResizer::FontResizer() {
    std::memset(advance_table_, 0, sizeof(advance_table_));
    std::memset(font_width_cache_, 0, sizeof(font_width_cache_));
    std::memset(font_tile_cache_, 0, sizeof(font_tile_cache_));
    std::memset(authentic_rom_tiles_, 0, sizeof(authentic_rom_tiles_));
    recompute_cache();
}

void FontResizer::set_density(FontDensity density) {
    settings_.density = density;
    recompute_cache();
}

void FontResizer::set_font_scale(FontScale scale) {
    settings_.scale = scale;
    recompute_cache();
}

void FontResizer::set_font_style(FontStyle style) {
    settings_.style = style;
    switch (style) {
        case FontStyle::Authentic:
            settings_.edge_smoothing = false;
            settings_.kerning_adjustment = 0;
            break;
        case FontStyle::CleanModern:
            settings_.edge_smoothing = true;
            settings_.kerning_adjustment = 0;
            break;
        case FontStyle::Condensed:
            settings_.edge_smoothing = true;
            settings_.kerning_adjustment = -1;
            break;
    }
    recompute_cache();
}

void FontResizer::set_line_spacing(int spacing_px) {
    settings_.line_spacing = std::clamp(spacing_px, 8, 24);
}

void FontResizer::set_kerning_adjustment(int kerning_px) {
    settings_.kerning_adjustment = std::clamp(kerning_px, -2, 2);
    recompute_cache();
}

void FontResizer::set_edge_smoothing(bool enable) {
    settings_.edge_smoothing = enable;
    recompute_cache();
}

void FontResizer::invalidate_cache() {
    authentic_rom_loaded_ = false;
    recompute_cache();
}

int FontResizer::get_glyph_width() const {
    return glyph_w_;
}

int FontResizer::get_glyph_height() const {
    return glyph_h_;
}

int FontResizer::get_char_advance_width(char c) const {
    unsigned char uc = static_cast<unsigned char>(c);
    return advance_table_[uc];
}

float FontResizer::get_average_char_width() const {
    static const char kSample[] = "etaoinshrdlcumwfgypbvkjxqz ETAOINSHRDLCUMWFGYPBVKJXQZ.,";
    int sum = 0;
    int count = 0;
    for (size_t i = 0; i < sizeof(kSample) - 1; ++i) {
        sum += get_char_advance_width(kSample[i]);
        ++count;
    }
    return (count > 0) ? (static_cast<float>(sum) / count) : 5.8f;
}

void FontResizer::ensure_authentic_rom_loaded() {
    if (authentic_rom_loaded_) return;

    ScopedInternalBusRead guard;
    // Check if ROM is mapped by testing glyph 'A' (65)
    uint16_t sample_hw = bus_read_u16(kFontTilesBase + 65 * 128);
    if (sample_hw == 0) {
        // ROM not mapped or uninitialized
        return;
    }

    for (size_t i = 0; i < sizeof(authentic_rom_tiles_) / 2; ++i) {
        uint16_t hw = bus_read_u16(kFontTilesBase + i * 2);
        authentic_rom_tiles_[i * 2] = hw & 0xFF;
        authentic_rom_tiles_[i * 2 + 1] = (hw >> 8) & 0xFF;
    }
    authentic_rom_loaded_ = true;
}

void FontResizer::update_metrics_for_scale(FontScale scale) {
    switch (scale) {
        case FontScale::Medium85:
            glyph_w_ = 7;
            glyph_h_ = 10;
            settings_.line_spacing = 13;
            break;
        case FontScale::Compact70:
            glyph_w_ = 6;
            glyph_h_ = 8;
            settings_.line_spacing = 10;
            break;
        case FontScale::Micro55:
            glyph_w_ = 5;
            glyph_h_ = 7;
            settings_.line_spacing = 8;
            break;
        case FontScale::Original100:
        default:
            glyph_w_ = 8;
            glyph_h_ = 12;
            settings_.line_spacing = 16;
            break;
    }
}

void FontResizer::recompute_cache() {
    update_metrics_for_scale(settings_.scale);
    ensure_authentic_rom_loaded();

    float scale_factor = 1.0f;
    switch (settings_.scale) {
        case FontScale::Medium85:  scale_factor = 0.85f; break;
        case FontScale::Compact70: scale_factor = 0.70f; break;
        case FontScale::Micro55:   scale_factor = 0.55f; break;
        case FontScale::Original100:
        default:                   scale_factor = 1.0f;  break;
    }

    float density_factor = 1.0f;
    int density_kerning = 0;
    switch (settings_.density) {
        case FontDensity::Compact:
            density_factor = 0.85f;
            density_kerning = -1;
            break;
        case FontDensity::HighDensity:
            density_factor = 0.70f;
            density_kerning = -2;
            break;
        case FontDensity::Original:
        default:
            density_factor = 1.0f;
            density_kerning = 0;
            break;
    }

    std::memset(font_tile_cache_, 0, sizeof(font_tile_cache_));

    // 1. Calculate Advance Widths dynamically
    for (int i = 0; i < 256; ++i) {
        int orig_w = kRomLatinGlyphWidths[i];
        if (orig_w == 0) {
            font_width_cache_[i] = 0;
            advance_table_[i] = 0;
            continue;
        }

        // For space character (ASCII 32)
        if (i == 32) {
            int sp_w = 4;
            if (settings_.scale == FontScale::Micro55) sp_w = 2;
            else if (settings_.scale == FontScale::Compact70) sp_w = 3;
            else if (settings_.scale == FontScale::Medium85) sp_w = 3;
            else sp_w = 4;

            if (settings_.density == FontDensity::Compact || settings_.density == FontDensity::HighDensity) {
                if (sp_w > 2) sp_w -= 1;
            }
            sp_w += settings_.kerning_adjustment;
            font_width_cache_[i] = static_cast<uint16_t>(std::clamp(sp_w, 2, 8));
            advance_table_[i] = static_cast<uint8_t>(font_width_cache_[i]);
            continue;
        }

        // Safe visual width (foreground + drop shadow)
        // In GBA ROM Latin font, each glyph has foreground width and shadow.
        // The visual right edge is at most orig_w - 1 (or orig_w for wide glyphs).
        // Advance width must NEVER be less than the visible glyph width, otherwise adjacent glyphs overlap!
        int vis_w = (orig_w >= 3) ? (orig_w - 1) : orig_w;

        int adv = orig_w;
        if (settings_.density == FontDensity::Compact) {
            // Tighter spacing: remove inter-glyph padding without overlapping visible pixels
            adv = (orig_w >= 4) ? (orig_w - 1) : orig_w;
        } else if (settings_.density == FontDensity::HighDensity) {
            // High density: pack tightly against shadow boundary
            adv = vis_w;
        } else {
            // Original: standard authentic spacing
            adv = orig_w;
        }

        if (settings_.style == FontStyle::Condensed) {
            // Condensed typography slightly tightens advance where safe
            if (adv > vis_w) {
                adv -= 1;
            }
        }

        // Apply user kerning adjustment, but strictly enforce no overlapping pixels
        adv += settings_.kerning_adjustment;
        adv = std::clamp(adv, vis_w, glyph_w_);

        font_width_cache_[i] = static_cast<uint16_t>(adv);
        advance_table_[i] = static_cast<uint8_t>(font_width_cache_[i]);
    }

    // 2. Generate 16x16 4bpp Sprite Tile Data
    if (settings_.style == FontStyle::Authentic) {
        if (settings_.scale == FontScale::Original100) {
            if (authentic_rom_loaded_) {
                std::memcpy(font_tile_cache_, authentic_rom_tiles_, sizeof(font_tile_cache_));
            }
        } else {
            // Downscale authentic GBA 16x16 glyphs
            if (authentic_rom_loaded_) {
                for (int i = 0; i < 256; ++i) {
                    uint8_t src_grid[16][16];
                    decode_16x16_sprite(&authentic_rom_tiles_[i * 128], src_grid);

                    int min_r = 16, max_r = -1, min_c = 16, max_c = -1;
                    for (int r = 0; r < 16; ++r) {
                        for (int c = 0; c < 16; ++c) {
                            if (src_grid[r][c] == 1) {
                                min_r = std::min(min_r, r);
                                max_r = std::max(max_r, r);
                                min_c = std::min(min_c, c);
                                max_c = std::max(max_c, c);
                            }
                        }
                    }

                    if (max_r >= min_r && max_c >= min_c) {
                        int src_h = max_r - min_r + 1;
                        int src_w = max_c - min_c + 1;
                        int dst_h = std::clamp(static_cast<int>(std::round(src_h * scale_factor)), 1, 14);
                        int dst_w = std::clamp(static_cast<int>(std::round(src_w * scale_factor)), 1, 14);
                        int dst_top = 4 + (8 - dst_h) / 2;
                        int dst_left = 1;

                        uint8_t scaled_grid[16][16] = {};
                        for (int dy = 0; dy < dst_h; ++dy) {
                            int sy = min_r + (dy * src_h) / dst_h;
                            for (int dx = 0; dx < dst_w; ++dx) {
                                int sx = min_c + (dx * src_w) / dst_w;
                                if (src_grid[sy][sx] == 1) {
                                    scaled_grid[dst_top + dy][dst_left + dx] = 1;
                                }
                            }
                        }

                        // Add drop shadow at +1, +1
                        for (int r = 0; r < 16; ++r) {
                            for (int c = 0; c < 16; ++c) {
                                if (scaled_grid[r][c] == 1 && r + 1 < 16 && c + 1 < 16 && scaled_grid[r + 1][c + 1] == 0) {
                                    scaled_grid[r + 1][c + 1] = 2;
                                }
                            }
                        }

                        encode_16x16_sprite(scaled_grid, &font_tile_cache_[i * 128]);
                    }
                }
            }
        }
    } else {
        // CleanModern or Condensed: Render crisp high-contrast modern typography
        // Seed non-ASCII glyphs from authentic ROM tiles if available
        if (authentic_rom_loaded_) {
            std::memcpy(font_tile_cache_, authentic_rom_tiles_, sizeof(font_tile_cache_));
        }

        int dst_h = 7;
        int dst_top = 4;
        int dst_left = (settings_.style == FontStyle::Condensed) ? 0 : 1;
        switch (settings_.scale) {
            case FontScale::Original100:
                dst_h = 9;
                dst_top = 3;
                dst_left = (settings_.style == FontStyle::Condensed) ? 1 : 2;
                break;
            case FontScale::Medium85:
                dst_h = 8;
                dst_top = 4;
                dst_left = (settings_.style == FontStyle::Condensed) ? 0 : 1;
                break;
            case FontScale::Compact70:
                dst_h = 7;
                dst_top = 4;
                dst_left = (settings_.style == FontStyle::Condensed) ? 0 : 1;
                break;
            case FontScale::Micro55:
                dst_h = 6;
                dst_top = 5;
                dst_left = 0;
                break;
        }

        for (int i = 32; i <= 126; ++i) {
            if (i == 32) {
                // Space: empty tile
                std::memset(&font_tile_cache_[i * 128], 0, 128);
                continue;
            }

            const uint8_t* cols = kFont5x7[i - 32];
            uint8_t src_bitmap[7][5] = {};
            int min_c = 5, max_c = -1;
            for (int c = 0; c < 5; ++c) {
                for (int r = 0; r < 7; ++r) {
                    if (cols[c] & (1 << r)) {
                        src_bitmap[r][c] = 1;
                        min_c = std::min(min_c, c);
                        max_c = std::max(max_c, c);
                    }
                }
            }

            if (max_c < min_c) { min_c = 0; max_c = 4; }
            int src_w = max_c - min_c + 1;
            int src_h = 7;
            int dst_w = std::clamp((src_w * dst_h + 3) / src_h, 1, 14);

            uint8_t grid[16][16] = {};
            for (int dy = 0; dy < dst_h; ++dy) {
                int sy = std::min(6, (dy * src_h) / dst_h);
                for (int dx = 0; dx < dst_w; ++dx) {
                    int sx = std::min(max_c, min_c + (dx * src_w) / dst_w);
                    if (src_bitmap[sy][sx]) {
                        int gy = dst_top + dy;
                        int gx = dst_left + dx;
                        if (gy < 16 && gx < 16) {
                            grid[gy][gx] = 1;
                        }
                    }
                }
            }

            // Drop shadow
            for (int r = 0; r < 16; ++r) {
                for (int c = 0; c < 16; ++c) {
                    if (grid[r][c] == 1 && r + 1 < 16 && c + 1 < 16 && grid[r + 1][c + 1] == 0) {
                        grid[r + 1][c + 1] = 2;
                    }
                }
            }

            encode_16x16_sprite(grid, &font_tile_cache_[i * 128]);
        }
    }
}

bool FontResizer::intercept_bus_read(uint32_t addr, uint32_t width, uint32_t* out_val) {
    if (!out_val) return false;

    // 1. Advance Widths Table (0x08F7D438 - 0x08F7D638, 512 bytes = 256 halfwords)
    if (addr >= kFontWidthTableBase && addr < kFontWidthTableEnd) {
        if (settings_.style == FontStyle::Authentic &&
            settings_.scale == FontScale::Original100 &&
            settings_.density == FontDensity::Original &&
            settings_.kerning_adjustment == 0) {
            return false;
        }
        uint32_t offset = addr - kFontWidthTableBase;
        uint32_t glyph_idx = offset / 2;
        if (glyph_idx >= 256) return false;

        if (width == 2) {
            *out_val = font_width_cache_[glyph_idx];
            return true;
        } else if (width == 1) {
            uint16_t w = font_width_cache_[glyph_idx];
            *out_val = (offset & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
            return true;
        } else if (width == 4) {
            uint32_t w0 = font_width_cache_[glyph_idx];
            uint32_t w1 = (glyph_idx + 1 < 256) ? font_width_cache_[glyph_idx + 1] : 0;
            *out_val = (w1 << 16) | (w0 & 0xFFFF);
            return true;
        }
        return false;
    }

    // Note: 0x090CBFB2 in ROM contains Sprite Frame Piece Descriptors (sprites_msg_frames_2.s),
    // NOT font bitmap tiles. Intercepting or overwriting that range corrupts OAM piece shapes
    // and sizes, rendering text as squashed horizontal strips. Never intercept ROM sprite descriptors.

    return false;
}

void FontResizer::render_scaled_glyph_4bpp(const uint8_t* src_glyph_1bpp, uint8_t* dst_tile_4bpp, uint8_t fg_palette_idx) {
    if (!src_glyph_1bpp || !dst_tile_4bpp) return;

    std::memset(dst_tile_4bpp, 0, 32);

    const int target_w = glyph_w_;
    const int target_h = glyph_h_;

    int offset_x = (8 - target_w) / 2;
    int offset_y = (8 - std::min(8, target_h)) / 2;

    for (int ty = 0; ty < std::min(8, target_h); ++ty) {
        int dst_y = offset_y + ty;
        if (dst_y < 0 || dst_y >= 8) continue;

        int sy = (ty * 12) / target_h;
        sy = std::clamp(sy, 0, 11);
        uint8_t src_row_byte = src_glyph_1bpp[sy];

        for (int tx = 0; tx < target_w; ++tx) {
            int dst_x = offset_x + tx;
            if (dst_x < 0 || dst_x >= 8) continue;

            int sx = (tx * 8) / target_w;
            sx = std::clamp(sx, 0, 7);

            bool pixel_on = (src_row_byte & (0x80 >> sx)) != 0;
            if (pixel_on) {
                int byte_idx = (dst_y * 4) + (dst_x / 2);
                if (dst_x % 2 == 0) {
                    dst_tile_4bpp[byte_idx] = (dst_tile_4bpp[byte_idx] & 0xF0) | (fg_palette_idx & 0x0F);
                } else {
                    dst_tile_4bpp[byte_idx] = (dst_tile_4bpp[byte_idx] & 0x0F) | ((fg_palette_idx & 0x0F) << 4);
                }
            }
        }
    }
}

bool FontResizer::intercept_text_tile_upload(uint32_t vram_dest_addr, const uint8_t* tile_data, size_t size, uint8_t* vram_base) {
    if (settings_.scale == FontScale::Original100 || !tile_data || !vram_base || size < 32) {
        return false;
    }

    if (vram_dest_addr < 0x06000000 || vram_dest_addr >= 0x06010000) {
        return false;
    }

    uint32_t offset = vram_dest_addr - 0x06000000;
    uint8_t scaled_tile[32];
    render_scaled_glyph_4bpp(tile_data, scaled_tile);

    std::memcpy(vram_base + offset, scaled_tile, 32);
    return true;
}

} // namespace khcom
