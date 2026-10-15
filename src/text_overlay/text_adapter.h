#pragma once
// Glue between the game's dialogue engine side and the generic text engine of gba-improvements (gbaimp/text_*.h):
//   * the game's text words (Windows-1252 style, plus the colour-code words 0x4079 / 0x4000 and the pause mark 0xE000)
//     and a PageSnapshot become a gbaimp::text::TextPage;
//   * the embedded fonts are registered with gbaimp::text::FontLibrary;
//   * the host's settings object (the library keeps none) with this game's defaults and "match the game" colours.
#include "overlay_types.h"

#include "gbaimp/text_renderer.h"

#include <cstdint>

namespace khcom::overlay {

// Unicode scalar of an engine text word, 0 when the word cannot be drawn (the page then falls back to the original text).
uint32_t engine_word_to_codepoint(uint16_t word);

// One TextPage per snapshot: characters in reading order (spaces and line breaks included), colour-code words folded into
// the colour of the glyphs that follow, reveal entries and pen positions taken from the engine glyph list.
gbaimp::text::TextPage to_text_page(const PageSnapshot& snap);

// The settings object (written by the menu / config, read by the engine side). Defaults are the game's.
TextSettings& settings();

// The shared text renderer, after the embedded fonts are registered (idempotent).
gbaimp::text::TextRenderer& renderer();

// Cheap check before hiding the engine glyphs, and the draw at present time (both use settings()).
bool can_draw_page(const PageSnapshot& snap, const DrawInfo& info);
bool draw_page(const PageSnapshot& snap, const DrawInfo& info);

} // namespace khcom::overlay
