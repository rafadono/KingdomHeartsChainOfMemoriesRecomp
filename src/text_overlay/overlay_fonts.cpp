// Font access of the HD dialogue overlay: built-in families, user fonts (fonts/*.ttf), metrics, glyph
// rasterisation by stb_truetype (analytic area coverage, no hinting bytecode).
#include "overlay_fonts.h"

#include "overlay_fonts_data.h"
#include "../text_fonts.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../third_party/stb/stb_truetype.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <unordered_map>

namespace khcom {
namespace overlay {

// ---------------------------------------------------------------------------------------------
// FontFace
// ---------------------------------------------------------------------------------------------
struct FontFace::Impl {
    stbtt_fontinfo info;
    std::vector<uint8_t> owned;                    // user fonts: the file; built-ins point at static data
    const unsigned char* data = nullptr;
    bool ok = false;
    int ascii[256];                                // glyph index cache for cp < 256 (-1 = unknown)
    std::unordered_map<uint32_t, int> other;
    Impl() { std::memset(&info, 0, sizeof(info)); for (int& v : ascii) v = -1; }
};

FontFace::FontFace() : impl(new Impl) {}
FontFace::~FontFace() = default;

bool FontFace::valid() const { return impl && impl->ok; }

static bool init_face(FontFace& f, const unsigned char* data) {
    FontFace::Impl& m = *f.impl;
    m.data = data;
    int off = stbtt_GetFontOffsetForIndex(data, 0);
    if (off < 0 || !stbtt_InitFont(&m.info, data, off)) return false;
    int a = 0, d = 0, g = 0;
    stbtt_GetFontVMetrics(&m.info, &a, &d, &g);
    f.ascent = static_cast<float>(a);
    f.descent = static_cast<float>(d);
    f.line_gap = static_cast<float>(g);
    const float s1 = stbtt_ScaleForMappingEmToPixels(&m.info, 1.0f);
    f.upem = s1 > 0.0f ? static_cast<int>(1.0f / s1 + 0.5f) : 1000;
    if (f.upem < 16 || f.upem > 16384) return false;
    int x0, y0, x1, y1;
    f.xheight = f.upem * 0.5f;
    f.capheight = f.upem * 0.7f;
    if (stbtt_GetCodepointBox(&m.info, 'x', &x0, &y0, &x1, &y1) && y1 > 0) f.xheight = static_cast<float>(y1);
    if (stbtt_GetCodepointBox(&m.info, 'H', &x0, &y0, &x1, &y1) && y1 > 0) f.capheight = static_cast<float>(y1);
    m.ok = true;
    return true;
}

int FontFace::glyph_index(uint32_t cp) const {
    if (!valid()) return 0;
    Impl& m = *impl;
    if (cp < 256) {
        int& c = m.ascii[cp];
        if (c < 0) c = stbtt_FindGlyphIndex(&m.info, static_cast<int>(cp));
        return c;
    }
    auto it = m.other.find(cp);
    if (it != m.other.end()) return it->second;
    const int gi = stbtt_FindGlyphIndex(&m.info, static_cast<int>(cp));
    m.other.emplace(cp, gi);
    return gi;
}

float FontFace::advance(int gi) const {
    if (!valid()) return 0.0f;
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&impl->info, gi, &adv, &lsb);
    return static_cast<float>(adv);
}

float FontFace::kern(int g1, int g2) const {
    if (!valid() || !g1 || !g2) return 0.0f;
    return static_cast<float>(stbtt_GetGlyphKernAdvance(&impl->info, g1, g2));
}

bool FontFace::glyph_extent(int gi, float& ymin, float& ymax) const {
    if (!valid()) return false;
    int x0, y0, x1, y1;
    if (!stbtt_GetGlyphBox(&impl->info, gi, &x0, &y0, &x1, &y1)) return false;
    if (y1 <= y0) return false;
    ymin = static_cast<float>(y0);
    ymax = static_cast<float>(y1);
    return true;
}

FaceScale FontFace::scale_for(float em_px) const {
    FaceScale s;
    s.sx = em_px * size_adjust / static_cast<float>(upem);
    s.sy = s.sx;
    const float xh = xheight > 0.0f ? xheight : upem * 0.5f;
    const float xh_px = std::max(1.0f, std::round(xh * s.sx));
    s.xheight_px = static_cast<int>(xh_px);
    s.sy = xh_px / xh;   // light vertical hint: x-height becomes a whole number of output pixels
    return s;
}

bool FontFace::bitmap(int gi, const FaceScale& sc, float shift_x, int& x0, int& y0, int& w, int& h,
                      std::vector<uint8_t>& out) const {
    if (!valid() || gi <= 0) return false;
    int ix0, iy0, ix1, iy1;
    stbtt_GetGlyphBitmapBoxSubpixel(&impl->info, gi, sc.sx, sc.sy, shift_x, 0.0f, &ix0, &iy0, &ix1, &iy1);
    w = ix1 - ix0;
    h = iy1 - iy0;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) { w = h = 0; return false; }
    out.assign(static_cast<size_t>(w) * h, 0);
    stbtt_MakeGlyphBitmapSubpixel(&impl->info, out.data(), w, h, w, sc.sx, sc.sy, shift_x, 0.0f, gi);
    x0 = ix0;
    y0 = iy0;
    return true;
}

// ---------------------------------------------------------------------------------------------
// TrueType validation (user fonts)
// ---------------------------------------------------------------------------------------------
namespace {

struct Reader {
    const uint8_t* d;
    size_t n;
    bool u16(size_t o, uint32_t& v) const { if (o + 2 > n) return false; v = (d[o] << 8) | d[o + 1]; return true; }
    bool u32(size_t o, uint32_t& v) const { if (o + 4 > n) return false; v = (uint32_t(d[o]) << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]; return true; }
};

struct Table { uint32_t off = 0, len = 0; bool present = false; };

uint32_t tag_of(const char* t) { return (uint32_t(uint8_t(t[0])) << 24) | (uint8_t(t[1]) << 16) | (uint8_t(t[2]) << 8) | uint8_t(t[3]); }

// Validates one simple glyph; returns false when stb_truetype could read outside [g, g+len).
bool simple_glyph_ok(const uint8_t* g, uint32_t len, int ncont) {
    if (len < 10u + 2u * ncont + 2u) return false;
    uint32_t prev_end = 0;
    for (int i = 0; i < ncont; ++i) {
        const uint32_t e = (g[10 + 2 * i] << 8) | g[11 + 2 * i];
        if (i > 0 && e < prev_end) return false;
        prev_end = e;
    }
    const uint32_t npts = ncont ? prev_end + 1 : 0;
    uint32_t pos = 10 + 2 * ncont;
    const uint32_t ilen = (g[pos] << 8) | g[pos + 1];
    pos += 2;
    if (pos + ilen > len) return false;
    pos += ilen;
    // flags
    uint32_t xbytes = 0, ybytes = 0, i = 0;
    while (i < npts) {
        if (pos >= len) return false;
        const uint8_t f = g[pos++];
        uint32_t rep = 1;
        if (f & 8) {
            if (pos >= len) return false;
            rep += g[pos++];
        }
        if (i + rep > npts) return false;
        xbytes += rep * ((f & 2) ? 1u : ((f & 16) ? 0u : 2u));
        ybytes += rep * ((f & 4) ? 1u : ((f & 32) ? 0u : 2u));
        i += rep;
    }
    return static_cast<uint64_t>(pos) + xbytes + ybytes <= len;
}

bool glyph_ok(const Reader& rd, const Table& glyf, const std::vector<uint32_t>& loca, uint32_t gid, int depth, uint32_t nglyphs) {
    if (depth > 6) return false;
    const uint32_t a = loca[gid], b = loca[gid + 1];
    if (b == a) return true;                       // empty glyph
    if (b < a || b > glyf.len || b - a < 10) return false;
    const uint8_t* g = rd.d + glyf.off + a;
    const uint32_t len = b - a;
    const int nc = static_cast<int16_t>((g[0] << 8) | g[1]);
    if (nc >= 0) return simple_glyph_ok(g, len, nc);
    uint32_t pos = 10;
    for (int guard = 0; guard < 256; ++guard) {
        if (pos + 4 > len) return false;
        const uint32_t flags = (g[pos] << 8) | g[pos + 1];
        const uint32_t comp = (g[pos + 2] << 8) | g[pos + 3];
        pos += 4;
        if (comp >= nglyphs || comp == gid) return false;
        pos += (flags & 1) ? 4 : 2;
        if (flags & 0x8) pos += 2; else if (flags & 0x40) pos += 4; else if (flags & 0x80) pos += 8;
        if (pos > len) return false;
        if (!glyph_ok(rd, glyf, loca, comp, depth + 1, nglyphs)) return false;
        if (!(flags & 0x20)) return true;
    }
    return false;
}

} // namespace

bool validate_truetype(const uint8_t* data, size_t size) {
    if (!data || size < 12 || size > FontLibrary::kMaxUserFontBytes) return false;
    Reader rd{data, size};
    uint32_t ver = 0, ntab = 0;
    rd.u32(0, ver);
    if (ver != 0x00010000u && ver != tag_of("true")) return false;   // TrueType outlines only
    rd.u16(4, ntab);
    if (ntab == 0 || ntab > 64 || 12 + 16u * ntab > size) return false;
    Table cmap, glyf, head, hhea, hmtx, loca, maxp;
    for (uint32_t i = 0; i < ntab; ++i) {
        const size_t o = 12 + 16u * i;
        uint32_t tag = 0, off = 0, len = 0;
        rd.u32(o, tag);
        rd.u32(o + 8, off);
        rd.u32(o + 12, len);
        if (static_cast<uint64_t>(off) + len > size) return false;
        Table t{off, len, true};
        if (tag == tag_of("cmap")) cmap = t;
        else if (tag == tag_of("glyf")) glyf = t;
        else if (tag == tag_of("head")) head = t;
        else if (tag == tag_of("hhea")) hhea = t;
        else if (tag == tag_of("hmtx")) hmtx = t;
        else if (tag == tag_of("loca")) loca = t;
        else if (tag == tag_of("maxp")) maxp = t;
    }
    if (!cmap.present || !glyf.present || !head.present || !hhea.present || !hmtx.present || !loca.present || !maxp.present) return false;
    if (cmap.len < 4 || head.len < 54 || hhea.len < 36 || maxp.len < 6) return false;
    uint32_t upem = 0, locfmt = 0, nglyphs = 0, nhm = 0;
    rd.u16(head.off + 18, upem);
    rd.u16(head.off + 50, locfmt);
    rd.u16(maxp.off + 4, nglyphs);
    rd.u16(hhea.off + 34, nhm);
    if (upem < 16 || upem > 16384 || locfmt > 1 || nglyphs == 0 || nglyphs > 20000 || nhm == 0 || nhm > nglyphs) return false;
    if (hmtx.len < 4u * nhm + 2u * (nglyphs - nhm)) return false;
    const uint32_t esz = locfmt ? 4 : 2;
    if (loca.len < esz * (nglyphs + 1)) return false;
    std::vector<uint32_t> lo(nglyphs + 1);
    uint32_t prev = 0;
    for (uint32_t i = 0; i <= nglyphs; ++i) {
        uint32_t v = 0;
        if (locfmt) rd.u32(loca.off + 4 * i, v); else { rd.u16(loca.off + 2 * i, v); v *= 2; }
        if (v < prev || v > glyf.len) return false;
        lo[i] = prev = v;
    }
    for (uint32_t g = 0; g < nglyphs; ++g)
        if (!glyph_ok(rd, glyf, lo, g, 0, nglyphs)) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// FontLibrary
// ---------------------------------------------------------------------------------------------
FontLibrary& FontLibrary::instance() {
    static FontLibrary lib;
    return lib;
}

namespace {

struct Builtin { const char* name; int source; float adjust; };   // source: 0..2 = textfonts::Face, 100+ = overlay extra index
const Builtin kBuiltins[] = {
    {"Humanist Sans", 0, 1.0f},      // PT Sans
    {"Humanist Narrow", 1, 1.0f},    // PT Sans Narrow
    {"Humanist Serif", 2, 1.0f},     // PT Serif
    {"Legible Sans", 100, 1.0f},     // Atkinson Hyperlegible
    {"Rounded Sans", 101, 1.0f},     // Varela Round
    {"Monospace", 102, 1.0f},        // Cousine
    {"Pixel Terminal", 103, 1.2f},   // VT323
    {"Pixel Arcade", 104, 0.72f},     // Press Start 2P
};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

void FontLibrary::ensure_loaded() {
    if (loaded_) return;
    loaded_ = true;
    load_builtin();
    load_user();
}

void FontLibrary::load_builtin() {
    for (const Builtin& b : kBuiltins) {
        const unsigned char* data = nullptr;
        if (b.source < 100) data = textfonts::face_data(b.source);
        else if (b.source - 100 < extra_face_count()) data = extra_face_data(b.source - 100);
        auto f = std::make_unique<FontFace>();
        f->name = b.name;
        f->builtin = true;
        f->size_adjust = b.adjust;
        if (!data || !init_face(*f, data)) continue;   // never expected; the family list simply omits it
        f->id = static_cast<int>(faces_.size());
        names_.push_back(f->name);
        faces_.push_back(std::move(f));
    }
    builtin_ = static_cast<int>(faces_.size());
}

void FontLibrary::load_user() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(user_dir_, ec)) return;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(user_dir_, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const std::string ext = lower(it->path().extension().string());
        if (ext != ".ttf") continue;
        files.push_back(it->path());
        if (files.size() >= 64) break;
    }
    std::sort(files.begin(), files.end());
    int added = 0;
    for (const fs::path& p : files) {
        if (added >= kMaxUserFonts) break;
        std::error_code e2;
        const uintmax_t sz = fs::file_size(p, e2);
        if (e2 || sz < 64 || sz > kMaxUserFontBytes) continue;
        auto f = std::make_unique<FontFace>();
        f->impl->owned.resize(static_cast<size_t>(sz));
        FILE* fp = std::fopen(p.string().c_str(), "rb");
        if (!fp) continue;
        const size_t got = std::fread(f->impl->owned.data(), 1, f->impl->owned.size(), fp);
        std::fclose(fp);
        if (got != f->impl->owned.size()) continue;
        // stb_truetype reads past the end of malformed fonts: pad and validate first.
        if (!validate_truetype(f->impl->owned.data(), f->impl->owned.size())) continue;
        f->impl->owned.resize(f->impl->owned.size() + 16, 0);
        if (!init_face(*f, f->impl->owned.data())) continue;
        std::string nm = p.stem().string();
        if (nm.empty()) continue;
        nm += " (fonts/)";
        bool dup = false;
        for (const std::string& n : names_) if (lower(n) == lower(nm)) dup = true;
        if (dup) continue;
        f->name = nm;
        f->builtin = false;
        f->id = static_cast<int>(faces_.size());
        names_.push_back(nm);
        faces_.push_back(std::move(f));
        ++added;
    }
}

int FontLibrary::family_count() { ensure_loaded(); return static_cast<int>(faces_.size()); }
int FontLibrary::builtin_count() { ensure_loaded(); return builtin_; }

const std::string& FontLibrary::family_name(int i) {
    static const std::string empty;
    ensure_loaded();
    return (i >= 0 && i < static_cast<int>(names_.size())) ? names_[i] : empty;
}

const FontFace* FontLibrary::face(int i) {
    ensure_loaded();
    return (i >= 0 && i < static_cast<int>(faces_.size())) ? faces_[i].get() : nullptr;
}

int FontLibrary::find_family(const std::string& name) {
    ensure_loaded();
    const std::string key = lower(name);
    for (size_t i = 0; i < names_.size(); ++i)
        if (lower(names_[i]) == key) return static_cast<int>(i);
    return -1;
}

void FontLibrary::rescan_user_fonts() {
    ensure_loaded();
    faces_.resize(builtin_);
    names_.resize(builtin_);
    load_user();
    ++generation_;
}

void FontLibrary::set_user_font_dir(const std::string& dir) {
    user_dir_ = dir;
    if (loaded_) rescan_user_fonts();
}

} // namespace overlay
} // namespace khcom
