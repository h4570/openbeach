#pragma once
#include "game/ui/font.h"
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace obl::font_pack {
struct Font {
    uint32_t size = 0, bold = 0;
    std::array<ui::GlyphRect, 256> glyphs{};
    std::array<int8_t, 65536> kerning{};
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
};
// Restores the byte-indexed metrics and glyph bits used by the current renderer.
bool ReadFont(const std::filesystem::path& pack, std::string_view name, Font& font, std::string& error);
} // namespace obl::font_pack
