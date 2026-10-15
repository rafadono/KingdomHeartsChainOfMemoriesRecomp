// Offline PNG dump of the HD dialogue text overlay (developer tool, no game needed).
//
//   overlay_dump --out <dir> [--scales 3,4,5.4,6.19] [--bg frame.ppm] [--bg-x 22] [--fonts <dir>]
//                [--sheets families,sizes,styles,pages]
//
// Draws synthetic pages with the real OverlayRenderer on an SDL software renderer (offscreen surface) at several
// output scales, families and sizes and writes PNG sheets (rows stacked, 6 px black separator, order printed on
// stdout). `--bg` is a binary PPM (P6) of the game frame (see tools/overlay_png_to_ppm.py); `--bg-x` is the x offset of
// the 240x160 game image inside it (22 for the 284x160 widescreen captures). The engine's own text area in the
// background is painted over with the colour of the box to the right of it.
#include "gbaimp/capture_harness.h"
#include "../src/text_overlay/overlay_fonts.h"
#include "../src/text_overlay/overlay_synth.h"
#include "../src/text_overlay/overlay_types.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace khcom::overlay;

namespace {

struct Bg {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};

bool read_ppm(const std::string& path, Bg& bg) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int w = 0, h = 0, mx = 0;
    if (std::fscanf(f, "P6 %d %d %d", &w, &h, &mx) != 3 || mx != 255 || w <= 0 || h <= 0 || w > 4096 || h > 4096) { std::fclose(f); return false; }
    std::fgetc(f);
    bg.w = w; bg.h = h;
    bg.rgb.resize(static_cast<size_t>(w) * h * 3);
    const bool ok = std::fread(bg.rgb.data(), 1, bg.rgb.size(), f) == bg.rgb.size();
    std::fclose(f);
    return ok;
}

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};

// 240x160 game image: real frame crop with the engine text painted over, or a synthetic dialogue box.
void make_game_image(const Bg* bg, int bg_x, std::vector<uint8_t>& out) {
    out.assign(240 * 160 * 3, 0);
    if (bg) {
        for (int y = 0; y < 160; ++y)
            for (int x = 0; x < 240; ++x) {
                const int sx = std::min(bg->w - 1, x + bg_x), sy = std::min(bg->h - 1, y);
                std::memcpy(&out[(y * 240 + x) * 3], &bg->rgb[(static_cast<size_t>(sy) * bg->w + sx) * 3], 3);
            }
        // cover the engine text (x 72..227, y 110..150) with the box colour sampled at x = 232 of the same row
        for (int y = 108; y < 150; ++y)
            for (int x = 70; x < 230; ++x) std::memcpy(&out[(y * 240 + x) * 3], &out[(y * 240 + 232) * 3], 3);
        return;
    }
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 240; ++x) {
            uint8_t* p = &out[(y * 240 + x) * 3];
            const bool box = x >= 60 && x < 236 && y >= 104 && y < 152;
            if (box) { p[0] = 226; p[1] = static_cast<uint8_t>(190 + (y - 104) / 2); p[2] = 120; }
            else { p[0] = 60; p[1] = static_cast<uint8_t>(90 + y / 3); p[2] = 150; }
        }
}

struct Sheet {
    int w = 0;
    std::vector<uint8_t> rgb;
    int h = 0;
    void add(const Image& im) {
        if (w == 0) w = im.w;
        if (h > 0) { rgb.insert(rgb.end(), static_cast<size_t>(w) * 6 * 3, 0); h += 6; }
        rgb.insert(rgb.end(), im.rgb.begin(), im.rgb.end());
        h += im.h;
    }
};

// Renders one page at `scale` and returns the crop around the dialogue box.
bool render_cell(const std::vector<uint8_t>& game, float scale, const OverlaySettings& st, const PageSnapshot& page, Image& out,
                 int off_x, int off_y, float* em_used = nullptr) {
    const int dw = static_cast<int>(240 * scale), dh = static_cast<int>(160 * scale);
    const int W = dw + 2 * off_x, H = dh + 2 * off_y;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return false;
    SDL_Renderer* ren = SDL_CreateSoftwareRenderer(surf);
    if (!ren) { SDL_FreeSurface(surf); return false; }
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, 240, 160);
    SDL_UpdateTexture(tex, nullptr, game.data(), 240 * 3);
    SDL_Rect dst{off_x, off_y, dw, dh};
    SDL_RenderCopy(ren, tex, nullptr, &dst);
    DrawInfo info;
    info.renderer = ren;
    info.dst_x = off_x; info.dst_y = off_y; info.dst_w = dw; info.dst_h = dh;
    OverlayRenderer& r = OverlayRenderer::instance();
    const bool drawn = r.draw(page, st, info);
    (void)em_used;
    SDL_Rect crop{off_x + static_cast<int>(56 * scale), off_y + static_cast<int>(100 * scale), static_cast<int>(184 * scale), static_cast<int>(52 * scale)};
    out.w = crop.w; out.h = crop.h;
    out.rgb.assign(static_cast<size_t>(out.w) * out.h * 3, 0);
    SDL_RenderReadPixels(ren, &crop, SDL_PIXELFORMAT_RGB24, out.rgb.data(), out.w * 3);
    r.release_gpu();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    return drawn;
}

void save(const Sheet& s, const std::string& path) {
    if (gbaimp::capture::write_png_rgb(path, s.rgb.data(), s.w, s.h, s.w * 3)) std::printf("wrote %s (%dx%d)\n", path.c_str(), s.w, s.h);
    else std::printf("FAILED to write %s\n", path.c_str());
}

std::vector<float> parse_floats(const std::string& s) {
    std::vector<float> v;
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(',', p);
        if (q == std::string::npos) q = s.size();
        v.push_back(static_cast<float>(std::atof(s.substr(p, q - p).c_str())));
        p = q + 1;
    }
    return v;
}

std::string fmt_scale(float s) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", s);
    return b;
}

} // namespace

int main(int argc, char** argv) {
    std::string out_dir = ".", bg_path, fonts_dir, sheets = "families,sizes,styles,pages";
    int bg_x = 22;
    std::vector<float> scales = {3.0f, 4.0f, 5.4f, 6.19f};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--out") out_dir = next();
        else if (a == "--bg") bg_path = next();
        else if (a == "--bg-x") bg_x = std::atoi(next().c_str());
        else if (a == "--scales") scales = parse_floats(next());
        else if (a == "--fonts") fonts_dir = next();
        else if (a == "--sheets") sheets = next();
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
    if (!fonts_dir.empty()) {
        FontLibrary::instance().set_user_font_dir(fonts_dir);
    }
    Bg bg;
    const bool have_bg = !bg_path.empty() && read_ppm(bg_path, bg);
    if (!bg_path.empty() && !have_bg) std::fprintf(stderr, "cannot read background %s (continuing synthetic)\n", bg_path.c_str());
    std::vector<uint8_t> game;
    make_game_image(have_bg ? &bg : nullptr, bg_x, game);

    OverlayRenderer& r = OverlayRenderer::instance();
    const std::vector<std::string> fams = r.family_names();
    const PageSnapshot p1 = synth_page({"One look at this castle, and I", "just knew! They're here."});
    const PageSnapshot p3 = synth_page({"I've been looking for you,", "Sora. The door to this place", "has been sealed for so long..."});
    const PageSnapshot p1r = synth_page({"One look at this castle, and I just knew! They're here."}, 72, 108);

    for (float scale : scales) {
        const std::string tag = fmt_scale(scale);
        OverlaySettings base;
        base.enabled = true;
        base.outline_width = 0.8f;
        if (sheets.find("families") != std::string::npos) {
            Sheet sh;
            for (size_t f = 0; f < fams.size(); ++f) {
                OverlaySettings st = base;
                st.family = static_cast<int>(f);
                Image im;
                render_cell(game, scale, st, p1, im, 3, 5);
                sh.add(im);
                std::printf("  families_%s row %zu = %s (em 10, shadow 0.6)\n", tag.c_str(), f, fams[f].c_str());
            }
            save(sh, out_dir + "/families_" + tag + "x.png");
        }
        if (sheets.find("sizes") != std::string::npos) {
            Sheet sh;
            for (float em : {6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f}) {
                OverlaySettings st = base;
                st.size_em = em;
                st.wrap = WrapMode::Reflow;
                Image im;
                render_cell(game, scale, st, p3, im, 3, 5);
                sh.add(im);
                std::printf("  sizes_%s row em %.0f (Humanist Sans, Reflow, auto-fit)\n", tag.c_str(), em);
            }
            save(sh, out_dir + "/sizes_" + tag + "x.png");
        }
        if (sheets.find("styles") != std::string::npos) {
            Sheet sh;
            const char* names[] = {"plain", "shadow", "outline"};
            for (int sty = 0; sty < 3; ++sty)
                for (int wr = 0; wr < 2; ++wr) {
                    OverlaySettings st = base;
                    st.style = static_cast<TextStyle>(sty);
                    st.wrap = static_cast<WrapMode>(wr);
                    st.text_rgba = sty == 0 ? 0x202020FFu : 0xFFFFFFFFu;
                    Image im;
                    render_cell(game, scale, st, p3, im, 3, 5);
                    sh.add(im);
                    std::printf("  styles_%s row %s / %s\n", tag.c_str(), names[sty], wr ? "reflow" : "keep");
                }
            save(sh, out_dir + "/styles_" + tag + "x.png");
        }
        if (sheets.find("pages") != std::string::npos) {
            Sheet sh;
            for (int rev : {0, 12, 40, 1000}) {
                PageSnapshot pg = p1;
                pg.revealed = rev == 1000 ? static_cast<int>(pg.glyphs.size()) : rev;
                OverlaySettings st = base;
                Image im;
                render_cell(game, scale, st, pg, im, 3, 5);
                sh.add(im);
                std::printf("  pages_%s row typewriter reveal %d\n", tag.c_str(), pg.revealed);
            }
            OverlaySettings st = base;
            st.wrap = WrapMode::Reflow;
            Image im;
            render_cell(game, scale, st, p1r, im, 3, 5);
            sh.add(im);
            std::printf("  pages_%s last row: single 'engine' line reflowed\n", tag.c_str());
            save(sh, out_dir + "/pages_" + tag + "x.png");
        }
    }
    r.release_gpu();
    return 0;
}
