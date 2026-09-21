#include "screen_filters.h"
#include <SDL.h>
#include <cmath>
#include <algorithm>
#include <vector>

namespace khcom {

ScreenFilters& ScreenFilters::instance() {
    static ScreenFilters s_instance;
    return s_instance;
}

ScreenFilters::ScreenFilters() {
    generate_lut(settings_.color_profile);
}

ScreenFilters::~ScreenFilters() {
    if (mask_texture_) {
        SDL_DestroyTexture(mask_texture_);
        mask_texture_ = nullptr;
    }
}

void ScreenFilters::set_color_profile(ColorProfile profile) {
    if (settings_.color_profile != profile) {
        settings_.color_profile = profile;
        generate_lut(profile);
    }
}

void ScreenFilters::set_mask_type(ScreenMaskType type) {
    settings_.mask_type = type;
}

void ScreenFilters::set_mask_intensity(float intensity) {
    settings_.mask_intensity = std::clamp(intensity, 0.05f, 1.0f);
}

void ScreenFilters::generate_lut(ColorProfile profile) {
    active_lut_profile_ = profile;
    for (int i = 0; i < 256; ++i) {
        float norm = i / 255.0f;
        float r = norm;
        float g = norm;
        float b = norm;

        switch (profile) {
        case ColorProfile::Agb001: {
            // AGB-001 reflective: desaturate and adjust gamma for unlit screen
            float gamma_corrected = std::pow(norm, 1.45f);
            r = std::clamp(gamma_corrected * 0.95f + 0.03f, 0.0f, 1.0f);
            g = std::clamp(gamma_corrected * 0.92f + 0.04f, 0.0f, 1.0f);
            b = std::clamp(gamma_corrected * 0.85f + 0.05f, 0.0f, 1.0f);
            break;
        }
        case ColorProfile::Ags001: {
            // AGS-001 frontlit: cooler tone with slightly elevated black floor
            float gamma_corrected = std::pow(norm, 1.70f);
            r = std::clamp(gamma_corrected * 0.88f + 0.06f, 0.0f, 1.0f);
            g = std::clamp(gamma_corrected * 0.92f + 0.06f, 0.0f, 1.0f);
            b = std::clamp(gamma_corrected * 1.00f + 0.08f, 0.0f, 1.0f);
            break;
        }
        case ColorProfile::Ags101: {
            // AGS-101 backlit: rich contrast, gamma 2.2
            float gamma_corrected = std::pow(norm, 2.20f);
            r = std::clamp(gamma_corrected * 1.02f, 0.0f, 1.0f);
            g = std::clamp(gamma_corrected * 1.00f, 0.0f, 1.0f);
            b = std::clamp(gamma_corrected * 0.98f, 0.0f, 1.0f);
            break;
        }
        case ColorProfile::MisterGamma16: {
            // MiSTer FPGA GBA 1.6 gamma mapping
            float gamma_corrected = std::pow(norm, 1.6f / 2.2f);
            r = g = b = gamma_corrected;
            break;
        }
        case ColorProfile::MisterGamma22: {
            // MiSTer FPGA GBA 2.2 gamma mapping
            float gamma_corrected = std::pow(norm, 2.2f / 2.2f);
            r = g = b = gamma_corrected;
            break;
        }
        case ColorProfile::Raw:
        default:
            r = g = b = norm;
            break;
        }

        lut_r_[i] = static_cast<uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
        lut_g_[i] = static_cast<uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
        lut_b_[i] = static_cast<uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
    }
}

void ScreenFilters::apply_color_correction(uint8_t* rgb24, int width, int height) {
    if (settings_.color_profile == ColorProfile::Raw || !rgb24) return;
    const size_t total_pixels = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < total_pixels; ++i) {
        rgb24[i * 3 + 0] = lut_r_[rgb24[i * 3 + 0]];
        rgb24[i * 3 + 1] = lut_g_[rgb24[i * 3 + 1]];
        rgb24[i * 3 + 2] = lut_b_[rgb24[i * 3 + 2]];
    }
}

void ScreenFilters::ensure_mask_texture(SDL_Renderer* renderer, ScreenMaskType type, int game_w, int game_h) {
    const float intensity = settings_.mask_intensity;
    if (mask_texture_ &&
        cached_mask_type_ == type &&
        cached_mask_w_ == game_w &&
        cached_mask_h_ == game_h &&
        std::abs(cached_mask_intensity_ - intensity) < 0.01f) {
        return;
    }

    if (mask_texture_) {
        SDL_DestroyTexture(mask_texture_);
        mask_texture_ = nullptr;
    }

    if (type == ScreenMaskType::Off) return;

    // Build a mask pattern texture
    int pattern_w = 6;
    int pattern_h = 6;

    if (type == ScreenMaskType::CrtScanlines) {
        pattern_w = 2;
        pattern_h = 4;
    } else if (type == ScreenMaskType::SubpixelRgb || type == ScreenMaskType::SubpixelBgr) {
        pattern_w = 3;
        pattern_h = 3;
    } else if (type == ScreenMaskType::LcdGrid) {
        pattern_w = 4;
        pattern_h = 4;
    }

    std::vector<uint32_t> pixels(pattern_w * pattern_h, 0xFFFFFFFF);
    const uint8_t dark_val = static_cast<uint8_t>(255.0f * (1.0f - intensity * 0.75f));

    for (int y = 0; y < pattern_h; ++y) {
        for (int x = 0; x < pattern_w; ++x) {
            uint8_t r = 255;
            uint8_t g = 255;
            uint8_t b = 255;

            switch (type) {
            case ScreenMaskType::LcdGrid:
                // Grid borders
                if (x == pattern_w - 1 || y == pattern_h - 1) {
                    r = g = b = dark_val;
                }
                break;

            case ScreenMaskType::SubpixelRgb: {
                // Vertical RGB stripes
                uint8_t dim = dark_val;
                if (x == 0) { r = 255; g = dim; b = dim; }
                else if (x == 1) { r = dim; g = 255; b = dim; }
                else { r = dim; g = dim; b = 255; }
                if (y == pattern_h - 1) {
                    r = static_cast<uint8_t>(r * 0.85f);
                    g = static_cast<uint8_t>(g * 0.85f);
                    b = static_cast<uint8_t>(b * 0.85f);
                }
                break;
            }

            case ScreenMaskType::SubpixelBgr: {
                // Vertical BGR stripes
                uint8_t dim = dark_val;
                if (x == 0) { r = dim; g = dim; b = 255; }
                else if (x == 1) { r = dim; g = 255; b = dim; }
                else { r = 255; g = dim; b = dim; }
                if (y == pattern_h - 1) {
                    r = static_cast<uint8_t>(r * 0.85f);
                    g = static_cast<uint8_t>(g * 0.85f);
                    b = static_cast<uint8_t>(b * 0.85f);
                }
                break;
            }

            case ScreenMaskType::LcdDiffusion:
                // Diagonal micro-pattern
                if ((x + y) % 2 == 0) {
                    r = g = b = dark_val;
                }
                break;

            case ScreenMaskType::CrtScanlines:
                // Scanlines on alternating rows
                if (y >= pattern_h / 2) {
                    r = g = b = dark_val;
                }
                break;

            case ScreenMaskType::CrtTrinitron:
                // Vertical wire aperture grille + scanlines
                if (x == pattern_w - 1 || y == pattern_h - 1) {
                    r = g = b = dark_val;
                } else if (x == 0) {
                    r = 255; g = dark_val; b = dark_val;
                } else if (x == 1) {
                    r = dark_val; g = 255; b = dark_val;
                } else if (x == 2) {
                    r = dark_val; g = dark_val; b = 255;
                }
                break;

            default:
                break;
            }

            // RGBA8888
            pixels[y * pattern_w + x] = (255u << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
        }
    }

    mask_texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, pattern_w, pattern_h);
    if (mask_texture_) {
        SDL_UpdateTexture(mask_texture_, nullptr, pixels.data(), pattern_w * sizeof(uint32_t));
        SDL_SetTextureBlendMode(mask_texture_, SDL_BLENDMODE_MOD);
    }

    cached_mask_type_ = type;
    cached_mask_w_ = game_w;
    cached_mask_h_ = game_h;
    cached_mask_intensity_ = intensity;
}

void ScreenFilters::render_mask(SDL_Renderer* renderer, const SDL_Rect* viewport, int game_w, int game_h) {
    if (settings_.mask_type == ScreenMaskType::Off || !renderer || !viewport) return;
    ensure_mask_texture(renderer, settings_.mask_type, game_w, game_h);
    if (!mask_texture_) return;

    SDL_RenderCopy(renderer, mask_texture_, nullptr, viewport);
}

} // namespace khcom
