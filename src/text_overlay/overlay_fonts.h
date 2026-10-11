#pragma once
// Font access of the HD dialogue overlay (render side). Built-in families are the embedded OFL Latin
// subsets (PT Sans / Narrow / Serif from src/text_fonts_data.cpp + the extra families of
// overlay_fonts_data.cpp); user families are the .ttf files found in the `fonts/` folder next to the
// working directory. All stb_truetype use of the overlay is confined to overlay_fonts.cpp.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace khcom {
namespace overlay {

// Sizes derived from one em in OUTPUT pixels (see docs/TEXT_OVERLAY_RENDER.md, "Vertical hint").
struct FaceScale {
    float sx = 0.0f;       // font units -> output px, horizontal (true em)
    float sy = 0.0f;       // vertical scale: x-height fitted to a whole number of output px
    int xheight_px = 0;    // the fitted x-height in output px
};

class FontFace {
public:
    FontFace();
    ~FontFace();
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    std::string name;
    bool builtin = false;
    int upem = 1000;
    float ascent = 0.0f, descent = 0.0f, line_gap = 0.0f;   // font units (descent negative)
    float xheight = 0.0f, capheight = 0.0f;                 // font units
    float size_adjust = 1.0f;                                // optical size correction applied to the em of this family
    int id = 0;                                              // index in the library (stable until a rescan)

    bool valid() const;
    int glyph_index(uint32_t cp) const;                      // 0 = missing
    float advance(int gi) const;                             // font units
    float kern(int g1, int g2) const;                        // font units (0 when the font has no kern table)
    // Vertical ink extent of a glyph in font units (y up): returns false for empty glyphs.
    bool glyph_extent(int gi, float& ymin, float& ymax) const;
    FaceScale scale_for(float em_px) const;
    // Coverage bitmap of one glyph at the given scales. (x0,y0) = offset of the bitmap's top-left from the
    // pen origin on the baseline (y down). `out` is resized to w*h (row-major, 0..255).
    bool bitmap(int gi, const FaceScale& sc, float shift_x, int& x0, int& y0, int& w, int& h, std::vector<uint8_t>& out) const;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

class FontLibrary {
public:
    static FontLibrary& instance();
    int family_count();                         // built-in first, then user fonts (lazy scan on first use)
    int builtin_count();
    const std::string& family_name(int i);      // "" for an out-of-range index
    const FontFace* face(int i);                // out-of-range -> nullptr
    int find_family(const std::string& name);   // case-insensitive exact match, -1 if absent
    void rescan_user_fonts();                   // re-reads the fonts/ folder (bumps generation())
    void set_user_font_dir(const std::string& dir);   // default "fonts" (tests)
    uint64_t generation() const { return generation_; }
    static constexpr int kMaxUserFonts = 8;
    static constexpr size_t kMaxUserFontBytes = 8u * 1024u * 1024u;

private:
    FontLibrary() = default;
    void ensure_loaded();
    void load_builtin();
    void load_user();
    std::vector<std::unique_ptr<FontFace>> faces_;
    std::vector<std::string> names_;
    int builtin_ = 0;
    bool loaded_ = false;
    uint64_t generation_ = 1;
    std::string user_dir_ = "fonts";
};

// Strict sanity check of TrueType data before it is handed to stb_truetype (which does not bounds-check
// malformed fonts): table directory inside the buffer, required tables present, loca/glyf consistent.
bool validate_truetype(const uint8_t* data, size_t size);

} // namespace overlay
} // namespace khcom
