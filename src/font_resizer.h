#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace khcom {

enum class FontScale : int {
    Original100 = 0, // 8x12 glyphs, 16px line spacing (original GBA)
    Medium85    = 1, // 7x10 glyphs, 13px line spacing
    Compact70   = 2, // 6x8 glyphs, 10px line spacing
    Micro55     = 3  // 5x7 glyphs, 8px line spacing
};

enum class FontDensity : int {
    Original    = 0, // 100% standard GBA glyph spacing (~26 chars)
    Compact     = 1, // 85% compact glyph spacing (~34 chars)
    HighDensity = 2  // 70% high-density glyph spacing (~42 chars)
};

enum class FontStyle : int {
    Authentic   = 0, // Authentic standard GBA pixel font
    CleanModern = 1, // Anti-aliased high-contrast sans glyphs
    Condensed   = 2  // Space-saving condensed kerning typography
};

struct FontResizerSettings {
    FontScale scale = FontScale::Original100;
    FontDensity density = FontDensity::Original;
    FontStyle style = FontStyle::Authentic;
    int line_spacing = 16;       // Vertical line pitch in pixels (8 to 20)
    int kerning_adjustment = 0;   // Kerning offset in pixels (-2 to +2)
    bool edge_smoothing = true;   // Anti-aliased font downsampling
};

class FontResizer {
public:
    static FontResizer& instance();

    const FontResizerSettings& settings() const { return settings_; }
    FontResizerSettings& settings() { return settings_; }

    void set_density(FontDensity density);
    void set_font_scale(FontScale scale);
    void set_font_style(FontStyle style);
    void set_line_spacing(int spacing_px);
    void set_kerning_adjustment(int kerning_px);
    void set_edge_smoothing(bool enable);

    // Returns effective glyph bounding box width and height in pixels
    int get_glyph_width() const;
    int get_glyph_height() const;

    // Calculates proportional advance width for a given ASCII character
    int get_char_advance_width(char c) const;

    // Returns weighted average glyph advance width across alphanumeric characters
    float get_average_char_width() const;

    // Downsamples an 8x12 1bpp glyph into an 8x8 4bpp GBA VRAM tile
    // 4bpp GBA tile: 32 bytes (2 pixels per byte, 8 rows of 4 bytes)
    void render_scaled_glyph_4bpp(const uint8_t* src_glyph_1bpp, uint8_t* dst_tile_4bpp, uint8_t fg_palette_idx = 1);

    // Addresses of font tables in Game Boy Advance ROM
    static constexpr uint32_t kFontWidthTableBase = 0x08F7D438u;
    static constexpr uint32_t kFontWidthTableEnd  = 0x08F7D638u; // 256 halfwords = 512 bytes
    static constexpr uint32_t kFontTilesBase      = 0x090CBFB2u;
    static constexpr uint32_t kFontTilesEnd       = 0x090CBFB2u + 256 * 128u; // 32768 bytes

    // Hook to intercept bus and ROM reads for font advance widths and glyph sprite tiles
    bool intercept_bus_read(uint32_t addr, uint32_t width, uint32_t* out_val);

    // Checks if cutscene / movie subtitles are currently playing
    bool is_movie_subtitle_active() const;

    // Refreshes active text glyph tiles in GBA OBJ VRAM (0x06014000 - 0x06018000)
    // so changes to font scale, typography, and density are visible immediately in real time
    void refresh_vram_text_tiles();

    // Forces reloading authentic ROM tiles and recomputing cache
    void invalidate_cache();

    // Hook to intercept and resize text tile uploads into GBA VRAM BG Character Blocks
    bool intercept_text_tile_upload(uint32_t vram_dest_addr, const uint8_t* tile_data, size_t size, uint8_t* vram_base);

private:
    FontResizer();

    void update_metrics_for_scale(FontScale scale);
    void ensure_authentic_rom_loaded();
    void recompute_cache();

    FontResizerSettings settings_;
    int glyph_w_ = 8;
    int glyph_h_ = 12;
    uint8_t advance_table_[256];
    uint16_t font_width_cache_[256];
    uint8_t font_tile_cache_[256 * 128];
    uint8_t authentic_rom_tiles_[256 * 128];
    bool authentic_rom_loaded_ = false;
};

} // namespace khcom
